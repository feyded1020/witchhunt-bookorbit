#include "CoverThumbLoader.h"

#include <CooperativeAbort.h>
#include <HalStorage.h>
#include <Logging.h>

#include "components/UITheme.h"

CoverThumbLoader::~CoverThumbLoader() { reset(); }

void CoverThumbLoader::configure(const std::pair<int, int>* sizes, const int count, const bool crop) {
  reset();
  count_ = 0;
  for (int i = 0; i < count && i < MAX_SIZES; ++i) sizes_[count_++] = sizes[i];
  crop_ = crop;
  singleHeight_ = false;
}

void CoverThumbLoader::configureSingleHeight(const int height) {
  reset();
  sizes_[0] = {height * 6 / 10, height};
  count_ = 1;
  crop_ = true;
  singleHeight_ = true;
}

std::string CoverThumbLoader::thumbPath(const std::string& bookPath, const int index) const {
  const std::string placeholder = ReaderActivity::coverThumbPlaceholder(bookPath);
  if (singleHeight_) return UITheme::getCoverThumbPath(placeholder, sizes_[0].second);
  return UITheme::getCoverThumbPath(placeholder, sizes_[index].first, sizes_[index].second);
}

bool CoverThumbLoader::sizeComplete(const std::string& bookPath, const int index) const {
  return ReaderActivity::isCoverThumbComplete(thumbPath(bookPath, index), sizes_[index].first, sizes_[index].second);
}

bool CoverThumbLoader::complete(const std::string& bookPath) const {
  for (int i = 0; i < count_; ++i) {
    if (!sizeComplete(bookPath, i)) return false;
  }
  return count_ > 0;
}

bool CoverThumbLoader::writeNoCoverPlaceholders(const std::string& bookPath) const {
  bool all = true;
  for (int i = 0; i < count_; ++i) {
    if (sizeComplete(bookPath, i)) continue;
    if (!ReaderActivity::writeCoverPlaceholderBmp(thumbPath(bookPath, i))) all = false;
  }
  return all;
}

void CoverThumbLoader::begin(const std::string& bookPath, BuildArena* scratch) {
  reset();
  book_ = bookPath;
  scratch_ = scratch;
}

void CoverThumbLoader::reset() {
  // Sessions first: each may hold blocks of the scratch region and open files.
  jpeg_.reset();
  extract_.reset();
  png_.reset();
  pngFiles_.close();
  book_.clear();
  scratch_ = nullptr;
  phase_ = Phase::Start;
  afterSessionFailure_ = false;
  jpegSessionFailed_ = false;
}

CoverThumbLoader::Step CoverThumbLoader::finish(const Step result) {
  reset();
  return result;
}

CoverThumbLoader::Step CoverThumbLoader::step() {
  if (book_.empty() || count_ == 0) return Step::Failed;

  switch (phase_) {
    case Phase::Jpeg: {
      const auto status = jpeg_->continueSteps(1);
      if (status == CoverThumbSession::Status::Running) return Step::Working;
      jpeg_.reset();
      phase_ = Phase::Start;
      if (status == CoverThumbSession::Status::Error) {
        // The session removed its partial output. The retry decodes one-shot.
        LOG_ERR("CTL", "Sliced JPEG cover failed for %s - retrying one-shot", book_.c_str());
        jpegSessionFailed_ = true;
      }
      return Step::Working;  // Start re-checks what is still missing
    }

    case Phase::Extract: {
      // 16 KB chunks with a lent region to inflate into -- a quarter of the iterations on a multi-MB
      // cover -- and 4 KB from the heap. The session shrinks the chunk itself when memory is short.
      const auto status = extract_->continueStep(scratch_ != nullptr ? 16384 : 4096);
      if (status == ReaderActivity::CoverExtractSession::Status::Running) return Step::Working;
      extract_.reset();
      // Done: cover.img is cached now, so the next attempt can convert it.
      if (status == ReaderActivity::CoverExtractSession::Status::Error) {
        LOG_ERR("CTL", "Cover extract failed for %s", book_.c_str());
        afterSessionFailure_ = true;
      }
      phase_ = Phase::Start;
      return Step::Working;
    }

    case Phase::Png: {
      constexpr uint32_t ROWS_PER_STEP = 6;
      const auto status = png_->continueRows(ROWS_PER_STEP);
      if (status == PngDecodeSession::Status::Running) return Step::Working;
      pngFiles_.close();
      png_.reset();
      phase_ = Phase::Start;
      if (status == PngDecodeSession::Status::Error) {
        LOG_ERR("CTL", "PNG cover decode failed for %s", book_.c_str());
        Storage.remove(thumbPath(book_, pngIndex_).c_str());  // the partial BMP
        afterSessionFailure_ = true;
      }
      return Step::Working;  // Start moves on to any other size still missing
    }

    case Phase::Start:
      break;
  }

  int missing[MAX_SIZES];
  int missingCount = 0;
  for (int i = 0; i < count_; ++i) {
    if (!sizeComplete(book_, i)) missing[missingCount++] = i;
  }
  if (missingCount == 0) return finish(Step::Done);

  // A one-shot conversion gives way to a button press mid-decode (CooperativeAbort) and reports
  // that as a transient failure. It is neither: the same attempt simply runs again next time.
  CooperativeAbort::clearAborted();
  ThumbResult res;
  if (!singleHeight_ && crop_ && missingCount >= 2) {
    // Every missing size from ONE decode of an embedded JPEG cover (memory audit 2026-09, R9 item
    // 2), started sliced so a press pauses it rather than throwing it away (item 3).
    std::pair<int, int> pending[MAX_SIZES];
    for (int k = 0; k < missingCount; ++k) pending[k] = sizes_[missing[k]];
    std::unique_ptr<CoverThumbSession> sliced;
    res = ReaderActivity::ensureCoverThumbs(book_, pending, missingCount, scratch_,
                                            jpegSessionFailed_ ? nullptr : &sliced);
    if (sliced) {
      jpeg_ = std::move(sliced);
      phase_ = Phase::Jpeg;
      return Step::Working;
    }
  } else if (singleHeight_) {
    res = ReaderActivity::ensureCoverThumb(book_, sizes_[0].second, scratch_);
  } else {
    const auto& size = sizes_[missing[0]];
    res = ReaderActivity::ensureCoverThumb(book_, size.first, size.second, scratch_, crop_);
  }
  if (CooperativeAbort::consumeAborted()) return Step::Working;
  if (res == ThumbResult::Ok) {
    if (complete(book_)) return finish(Step::Done);
    // Another size is still to come -- unless the one just converted is not complete either, in
    // which case reporting success again would only loop.
    return sizeComplete(book_, missing[0]) ? Step::Working : finish(Step::Failed);
  }

  // Only a TRANSIENT failure may walk the session ladder. A structural absence is permanent --
  // re-extracting the same entry yields the same undecodable bytes (the livelock this avoids was an
  // EPUB whose "cover.png" is really an AVIF).
  if (res == ThumbResult::TransientFail && !afterSessionFailure_) {
    pngIndex_ = missing[0];
    const auto& size = sizes_[pngIndex_];
    png_ = singleHeight_
               ? ReaderActivity::beginPngThumbSession(book_, size.second, pngFiles_, scratch_)
               : ReaderActivity::beginPngThumbSession(book_, size.first, size.second, pngFiles_, scratch_, crop_);
    if (png_) {
      phase_ = Phase::Png;
      return Step::Working;
    }
    extract_ = ReaderActivity::beginCoverExtractSession(book_, scratch_);
    if (extract_) {
      phase_ = Phase::Extract;
      return Step::Working;
    }
  }

  // Nothing more to try. Only a book that has no cover (a structural absence) gets the placeholder,
  // so no screen opens it again to rediscover that -- and not one with a sidecar image, a real
  // source that only failed this time. A transient failure is the caller's to count and retry.
  if (res == ThumbResult::StructurallyAbsent && ReaderActivity::sidecarCoverPath(book_).empty() &&
      writeNoCoverPlaceholders(book_)) {
    LOG_DBG("CTL", "No extractable cover for %s - wrote placeholder", book_.c_str());
    return finish(Step::Done);
  }
  return finish(Step::Failed);
}
