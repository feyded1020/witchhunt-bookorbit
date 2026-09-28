#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <SidecarFiles.h>
#include <Xtc.h>

#include <algorithm>

namespace {
constexpr char RECENT_BOOKS_FILE_JSON[] = "/.crosspoint/recent.json";
constexpr int MAX_RECENT_BOOKS = 10;

// "Series #3", or just "Series" without an index.
std::string seriesLabel(const Epub& epub) {
  std::string series = epub.getSeries();
  if (!series.empty() && !epub.getSeriesIndex().empty()) series += " #" + epub.getSeriesIndex();
  return series;
}
}  // namespace

RecentBooksStore RecentBooksStore::instance;

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& series, const std::string& coverBmpPath) {
  RecentBook newBook{path, title, author, series, coverBmpPath};
  // The EPUB reader passes metadata it has just loaded, sidecar applied; stamp
  // the sidecar it came from so refreshSidecarMetadata() does not redo it.
  if (FsHelpers::hasEpubExtension(path)) newBook.metadataStamp = SidecarFiles::metadataStamp(path);

  pruneMissing();

  // Remove existing entry if present, preserving its per-book overrides.
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    newBook.embeddedStyleOverride = it->embeddedStyleOverride;
    newBook.imageRenderingOverride = it->imageRenderingOverride;
    newBook.fontFamilyOverride = it->fontFamilyOverride;
    newBook.sdFontFamilyOverride = it->sdFontFamilyOverride;
    newBook.fontSizeOverride = it->fontSizeOverride;
    newBook.bionicReadingOverride = it->bionicReadingOverride;
    newBook.paragraphAlignmentOverride = it->paragraphAlignmentOverride;
    newBook.textAntiAliasingOverride = it->textAntiAliasingOverride;
    newBook.hyphenationOverride = it->hyphenationOverride;
    newBook.fontSizeNormalizationOverride = it->fontSizeNormalizationOverride;
    newBook.guideDotsOverride = it->guideDotsOverride;
    newBook.inlineFootnotePreviewsOverride = it->inlineFootnotePreviewsOverride;
    recentBooks.erase(it);
  }

  recentBooks.insert(recentBooks.begin(), std::move(newBook));

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::removeBook(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
    saveToFile();
  }
}

bool RecentBooksStore::refreshSidecarMetadata(const size_t maxBooks) {
  bool changed = false;
  size_t seen = 0;
  for (RecentBook& book : recentBooks) {
    // Counted as the home screen counts them, which skips missing books.
    if (seen >= maxBooks) break;
    if (isMissing(book)) continue;
    seen++;
    if (!FsHelpers::hasEpubExtension(book.path)) continue;
    const uint32_t stamp = SidecarFiles::metadataStamp(book.path);
    if (stamp == book.metadataStamp) continue;

    // The same load the reader's metadata comes from, so a removed sidecar
    // restores the embedded values rather than leaving the old override.
    Epub epub(book.path, "/.crosspoint");
    if (!epub.loadForMetadata()) {
      LOG_DBG("RBS", "Sidecar metadata changed but book did not load, keeping entry: %s", book.path.c_str());
      continue;  // stamp left stale: retried next time
    }
    LOG_DBG("RBS", "Sidecar metadata changed, refreshing entry: %s", book.path.c_str());
    book.title = epub.getTitle();
    book.author = epub.getAuthor();
    book.series = seriesLabel(epub);
    book.metadataStamp = stamp;
    changed = true;
  }
  if (changed) saveToFile();
  return changed;
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& series, const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.series = series;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

RecentBook RecentBooksStore::getBookByPath(const std::string& path) const {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    return *it;
  }
  return RecentBook{};
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, it->fontFamilyOverride,
                            it->sdFontFamilyOverride, it->fontSizeOverride, it->bionicReadingOverride,
                            it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const int8_t fontSizeOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  const std::string sdOverride = (fontFamilyOverride >= 0) ? std::string() : it->sdFontFamilyOverride;
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, fontFamilyOverride, sdOverride,
                            fontSizeOverride, it->bionicReadingOverride, it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const std::string& sdFontFamilyOverride, const int8_t fontSizeOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, fontFamilyOverride,
                            sdFontFamilyOverride, fontSizeOverride, it->bionicReadingOverride,
                            it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const bool bionicReadingOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, it->fontFamilyOverride,
                            it->sdFontFamilyOverride, it->fontSizeOverride, bionicReadingOverride,
                            it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const int8_t fontSizeOverride, const bool bionicReadingOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  const std::string sdOverride = (fontFamilyOverride >= 0) ? std::string() : it->sdFontFamilyOverride;
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, fontFamilyOverride, sdOverride,
                            fontSizeOverride, bionicReadingOverride, it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const std::string& sdFontFamilyOverride, const int8_t fontSizeOverride,
                                          const bool bionicReadingOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, fontFamilyOverride,
                            sdFontFamilyOverride, fontSizeOverride, bionicReadingOverride,
                            it->paragraphAlignmentOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const std::string& sdFontFamilyOverride, const int8_t fontSizeOverride,
                                          const bool bionicReadingOverride, const int8_t paragraphAlignmentOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  return setReaderOverrides(path, embeddedStyleOverride, imageRenderingOverride, fontFamilyOverride,
                            sdFontFamilyOverride, fontSizeOverride, static_cast<int8_t>(bionicReadingOverride ? 1 : 0),
                            paragraphAlignmentOverride, it->textAntiAliasingOverride, it->hyphenationOverride,
                            it->fontSizeNormalizationOverride, it->guideDotsOverride,
                            it->inlineFootnotePreviewsOverride);
}

bool RecentBooksStore::setReaderOverrides(const std::string& path, const int8_t embeddedStyleOverride,
                                          const int8_t imageRenderingOverride, const int8_t fontFamilyOverride,
                                          const std::string& sdFontFamilyOverride, const int8_t fontSizeOverride,
                                          const int8_t bionicReadingOverride, const int8_t paragraphAlignmentOverride,
                                          const int8_t textAntiAliasingOverride, const int8_t hyphenationOverride,
                                          const int8_t fontSizeNormalizationOverride, const int8_t guideDotsOverride,
                                          const int8_t inlineFootnotePreviewsOverride) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }

  it->embeddedStyleOverride = embeddedStyleOverride;
  it->imageRenderingOverride = imageRenderingOverride;
  it->fontFamilyOverride = fontFamilyOverride;
  it->sdFontFamilyOverride = sdFontFamilyOverride;
  it->fontSizeOverride = fontSizeOverride;
  it->bionicReadingOverride = bionicReadingOverride;
  it->paragraphAlignmentOverride = paragraphAlignmentOverride;
  it->textAntiAliasingOverride = textAntiAliasingOverride;
  it->hyphenationOverride = hyphenationOverride;
  it->fontSizeNormalizationOverride = fontSizeNormalizationOverride;
  it->guideDotsOverride = guideDotsOverride;
  it->inlineFootnotePreviewsOverride = inlineFootnotePreviewsOverride;
  return saveToFile();
}

bool RecentBooksStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveRecentBooks(*this, RECENT_BOOKS_FILE_JSON);
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), seriesLabel(epub), epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), "", xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{path, lastBookFileName, "", "", ""};
  }
  return RecentBook{path, "", "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  if (Storage.exists(RECENT_BOOKS_FILE_JSON)) {
    String json = Storage.readFile(RECENT_BOOKS_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadRecentBooks(*this, json.c_str());
    }
  }
  return false;
}
