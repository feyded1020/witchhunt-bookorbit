#pragma once

#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>

#include <memory>
#include <string>
#include <utility>

#include "Epub/CoverThumbSession.h"
#include "activities/reader/ReaderActivity.h"

class BuildArena;

// Makes one book's cover thumbnails, a small unit of work per step(). The one cover pipeline: the
// Home carousel, the Recent Books grid and Browse Files' Covers view each configure one and drive
// it from their own loop; everything below is shared.
//
// Per book, until every configured size is complete on the card:
//   1. the sizes still missing are collected;
//   2. two of them, cropped, go to ensureCoverThumbs() -- one decode of an embedded JPEG writes
//      both, sliced (the carousel's CoverThumbSession), so a press pauses it where it stands; one
//      of them goes to ensureCoverThumb(), which also converts a sidecar image or an XTC/TXT cover;
//   3. a decode that gave way to a button press (CooperativeAbort) simply runs again;
//   4. a transient failure walks the session ladder: a sliced PNG decode, or first a sliced ZIP
//      extraction of a large embedded cover; after a failed session the next failure is final;
//   5. a book with no cover at all -- a structural absence -- gets the 1x1 placeholder at every
//      size, so no screen opens it again to rediscover that. A transient failure is reported as
//      Failed and placeholders nothing: the caller decides when to stop retrying (Home keeps a
//      per-session budget and then calls writeNoCoverPlaceholders()).
//
// The caller decides how long to keep stepping -- a time budget, input waiting, a frame being
// drawn -- and every session is resumable, so stopping between steps loses nothing.
//
// `scratch`, when given, holds the inflate rings and decoder buffers instead of the heap: the
// screen's borrowed secondary framebuffer. reset() before that region goes back to the display.
class CoverThumbLoader {
 public:
  enum class Step : uint8_t {
    Working,  // more to do; call step() again
    Done,     // every size (or the no-cover placeholder at every size) is complete on the card
    Failed,   // a transient failure: no thumbnail this time, nothing recorded
  };

  static constexpr int MAX_SIZES = JpegToBmpConverter::kMaxTargets;

  CoverThumbLoader() = default;
  ~CoverThumbLoader();
  CoverThumbLoader(const CoverThumbLoader&) = delete;
  CoverThumbLoader& operator=(const CoverThumbLoader&) = delete;

  // The thumbnails to make, as "thumb_<W>x<H>.bmp": up to MAX_SIZES boxes, all cropped to fill the
  // box (the carousel's) or all fitted inside it (the cover grids'). Abandons the book in hand.
  void configure(const std::pair<int, int>* sizes, int count, bool crop);
  // The single-height form older themes use: "thumb_<H>.bmp", H*0.6 wide, cropped.
  void configureSingleHeight(int height);

  // Where size `index` of `bookPath` lives.
  std::string thumbPath(const std::string& bookPath, int index = 0) const;
  // Every configured size of `bookPath` is complete on the card (a placeholder counts).
  bool complete(const std::string& bookPath) const;
  // The no-cover placeholder at every size still missing, for a caller giving up on a book. True
  // when every size is complete afterwards.
  bool writeNoCoverPlaceholders(const std::string& bookPath) const;

  // Start on `bookPath`, abandoning whatever book was in hand.
  void begin(const std::string& bookPath, BuildArena* scratch);
  Step step();
  // Abandon the book in hand and close every session and file it had open.
  void reset();

  const std::string& book() const { return book_; }
  bool busy() const { return !book_.empty(); }

 private:
  enum class Phase : uint8_t { Start, Jpeg, Extract, Png };

  Step finish(Step result);
  bool sizeComplete(const std::string& bookPath, int index) const;
  int expectedWidth(int index) const;

  std::pair<int, int> sizes_[MAX_SIZES] = {};
  int count_ = 0;
  bool crop_ = true;
  bool singleHeight_ = false;

  std::string book_;
  BuildArena* scratch_ = nullptr;
  Phase phase_ = Phase::Start;
  // A sliced session failed for this book: the next one-shot failure is final for now, rather than
  // starting the same session again.
  bool afterSessionFailure_ = false;
  // The sliced two-size JPEG decode failed: the retry runs one-shot.
  bool jpegSessionFailed_ = false;
  int pngIndex_ = 0;  // the size the PNG session is writing
  std::unique_ptr<CoverThumbSession> jpeg_;
  std::unique_ptr<ReaderActivity::CoverExtractSession> extract_;
  std::unique_ptr<PngDecodeSession> png_;
  ReaderActivity::PngThumbFiles pngFiles_;
};
