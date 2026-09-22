#include "BookOrbitCatalogActivity.h"

#include <BookOrbitCredentialStore.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BookProgressPresentation.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool isFacetSection(const std::string& id) {
  return id == "authors" || id == "series" || id == "collections" || id == "libraries";
}

// Title - Author.epub with characters FAT and the reader dislike replaced.
std::string fileNameFor(const std::string& title, const std::string& author) {
  std::string name = title.empty() ? std::string("book") : title;
  if (!author.empty()) name += " - " + author;
  std::string safe;
  safe.reserve(name.size());
  for (const char c : name) {
    safe.push_back((c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                    c == '|' || static_cast<unsigned char>(c) < 0x20)
                       ? '_'
                       : c);
  }
  // Keep the path comfortably inside long-filename limits, without splitting a UTF-8 sequence.
  constexpr size_t MAX_NAME = 120;
  if (safe.size() > MAX_NAME) {
    size_t cut = MAX_NAME;
    while (cut > 0 && (static_cast<unsigned char>(safe[cut]) & 0xC0) == 0x80) cut--;
    safe.resize(cut);
  }
  while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) safe.pop_back();
  return safe + ".epub";
}
// Where a catalog book downloads to (and where an earlier download of it would be).
std::string catalogPath(const std::string& title, const std::string& author) {
  return BOOKORBIT_STORE.getDownloadFolder() + "/" + fileNameFor(title, author);
}

// Best-effort "already downloaded" check: the current download folder, then the SD root.
bool onDevicePath(const std::string& title, const std::string& author, std::string& outPath) {
  outPath = catalogPath(title, author);
  if (Storage.exists(outPath.c_str())) return true;
  const std::string atRoot = "/" + fileNameFor(title, author);
  if (!BOOKORBIT_STORE.getDownloadFolder().empty() && Storage.exists(atRoot.c_str())) {
    outPath = atRoot;
    return true;
  }
  return false;
}

constexpr size_t MAX_LOCAL_ENTRIES = 300;
}  // namespace

void BookOrbitCatalogActivity::onEnter() {
  Activity::onEnter();
  stack.clear();
  View root;
  root.kind = ViewKind::ROOT;
  root.title = tr(STR_BOOKORBIT_CATALOG);
  stack.push_back(std::move(root));

  if (!BOOKORBIT_STORE.hasCredentials()) {
    fail(tr(STR_BOOKORBIT_SETUP_HINT));
    return;
  }
  wifiUsed = true;
  trimMemoryForNetworkSession(renderer, "BookOrbit");
  if (WiFi.status() == WL_CONNECTED) {
    onWifiReady(true);
    return;
  }
  state = State::WIFI;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiReady(!result.isCancelled); });
}

void BookOrbitCatalogActivity::onExit() {
  Activity::onExit();
  if (!wifiUsed) return;
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  // The network session released the secondary framebuffer; a silent reboot restores it
  // (the same trade the OPDS browser and sync screens make).
  if (openAfterExit) {
    silentRestartToReader();
  } else {
    silentRestart();
  }
}

void BookOrbitCatalogActivity::onWifiReady(const bool connected) {
  if (!connected) {
    // The local sections work without a network; offer those rather than a dead end.
    offline = true;
    loadCurrent();
    return;
  }
  // Modem sleep stays ON here, unlike the sync path: browsing is idle most of the time, and a
  // catalog request being a few hundred ms slower costs nothing next to holding the radio awake
  // for the whole session.
  HalClock::ensureUsableForTls(SETTINGS.ntpServer);
  loadCurrent();
}

void BookOrbitCatalogActivity::fail(const std::string& message) {
  RenderLock lock(*this);
  state = State::ERROR;
  errorMessage = message;
  requestUpdate(true);
}

void BookOrbitCatalogActivity::loadCurrent() {
  {
    RenderLock lock(*this);
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
  }
  requestUpdateAndWait();

  View& view = current();
  bool ok = true;
  std::vector<BookOrbitCatalogSection> newSections;
  BookOrbitCatalogCounts counts;
  std::vector<BookOrbitCatalogBook> newBooks;
  std::vector<bool> newOnDevice;
  std::vector<BookOrbitFacetEntry> newFacets;
  std::vector<LocalBook> newLocal;
  bool more = false;
  switch (view.kind) {
    case ViewKind::ROOT:
      if (!offline) {
        ok = BookOrbitCatalogClient::fetchRootSections(newSections);
        // Counts are decorative: an older server without the dashboard just shows none.
        if (ok) BookOrbitCatalogClient::fetchCatalogCounts(counts);
      }
      break;
    case ViewKind::BOOKS: {
      BookOrbitBookPage page;
      ok = BookOrbitCatalogClient::fetchBooks(view.query, view.page, page);
      if (ok) {
        newBooks = std::move(page.books);
        more = page.total > 0 ? view.page * BookOrbitCatalogClient::PAGE_SIZE < page.total
                              : static_cast<int>(newBooks.size()) >= BookOrbitCatalogClient::PAGE_SIZE;
        newOnDevice.reserve(newBooks.size());
        std::string path;
        for (const auto& book : newBooks) newOnDevice.push_back(onDevicePath(book.title, book.author, path));
      }
      break;
    }
    case ViewKind::FACET: {
      BookOrbitFacetPage page;
      ok = BookOrbitCatalogClient::fetchSectionEntries(view.sectionId, view.page, page);
      if (ok) {
        newFacets = std::move(page.entries);
        more = page.hasNext;
      }
      break;
    }
    case ViewKind::LOCAL:
      collectLocal(view.local, &newLocal);
      break;
  }
  if (!ok) {
    fail(BookOrbitCatalogClient::lastFetchBadResponse ? tr(STR_BOOKORBIT_SYNC_HTTP_404)
                                                      : tr(STR_KOREADER_SYNC_NETWORK_ERROR));
    return;
  }
  RenderLock lock(*this);
  if (view.kind == ViewKind::ROOT) buildRoot(newSections, counts);
  books = std::move(newBooks);
  booksOnDevice = std::move(newOnDevice);
  facets = std::move(newFacets);
  localBooks = std::move(newLocal);
  hasMore = more;
  const int rows = rowCount();
  if (view.selector >= rows) view.selector = rows > 0 ? rows - 1 : 0;
  state = State::LIST;
  requestUpdate(true);
}

void BookOrbitCatalogActivity::buildRoot(const std::vector<BookOrbitCatalogSection>& sections,
                                         const BookOrbitCatalogCounts& counts) {
  const auto countFor = [&counts](const std::string& id) {
    if (id == "all-books" || id == "recent") return counts.totalBooks;
    if (id == "continue-reading") return counts.inProgress;
    if (id == "libraries") return counts.libraries;
    if (id == "authors") return counts.authors;
    if (id == "series") return counts.series;
    if (id == "collections") return counts.collections;
    return -1;
  };
  rootRows.clear();
  rootRows.reserve(sections.size() + 3);
  for (const auto& section : sections) {
    rootRows.push_back({RootRow::Kind::SERVER, section.title, section.id, countFor(section.id)});
  }
  rootRows.push_back({RootRow::Kind::ON_DEVICE, tr(STR_BOOKORBIT_ON_DEVICE), "",
                      static_cast<int>(collectLocal(LocalKind::ON_DEVICE, nullptr))});
  rootRows.push_back({RootRow::Kind::IN_PROGRESS, tr(STR_BOOKORBIT_IN_PROGRESS), "",
                      static_cast<int>(collectLocal(LocalKind::IN_PROGRESS, nullptr))});
  if (!offline) rootRows.push_back({RootRow::Kind::SEARCH, tr(STR_SEARCH), "", -1});
}

size_t BookOrbitCatalogActivity::collectLocal(const LocalKind kind, std::vector<LocalBook>* out) {
  size_t count = 0;
  if (kind == LocalKind::IN_PROGRESS) {
    // Started but not finished, from the recent-books list and each book's saved progress.
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (!FsHelpers::hasEpubExtension(book.path) || !Storage.exists(book.path.c_str())) continue;
      const int percent = BookProgressPresentation::readPercent(book);
      if (percent >= 100) continue;
      count++;
      if (out) out->push_back({book.title.empty() ? book.path : book.title, book.author, book.path, percent});
    }
    return count;
  }

  // Every EPUB in the BookOrbit download folder and the SD root.
  const auto scanDir = [&count, out](const std::string& dirPath) {
    FsFile dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) return;
    char name[128];
    FsFile file;
    while (count < MAX_LOCAL_ENTRIES && (file = dir.openNextFile())) {
      const size_t nameLen = file.isDirectory() ? 0 : file.getName(name, sizeof(name));
      file.close();
      if (nameLen > 5 && name[0] != '.' && FsHelpers::hasEpubExtension(std::string_view(name, nameLen))) {
        count++;
        if (!out) continue;
        LocalBook book;
        std::string stem(name, nameLen - 5);
        // Catalog downloads are named "Title - Author"; split that back for display.
        const size_t dash = stem.rfind(" - ");
        if (dash != std::string::npos) {
          book.title = stem.substr(0, dash);
          book.author = stem.substr(dash + 3);
        } else {
          book.title = std::move(stem);
        }
        book.path = (dirPath == "/" ? std::string("/") : dirPath + "/") + std::string(name, nameLen);
        out->push_back(std::move(book));
      }
    }
    dir.close();
  };
  const std::string& folder = BOOKORBIT_STORE.getDownloadFolder();
  if (!folder.empty()) scanDir(folder);
  scanDir("/");
  if (out) {
    std::sort(out->begin(), out->end(), [](const LocalBook& a, const LocalBook& b) {
      return FsHelpers::naturalCompare(a.title.c_str(), b.title.c_str()) < 0;
    });
  }
  return count;
}

int BookOrbitCatalogActivity::rowCount() const {
  const View& view = stack.back();
  switch (view.kind) {
    case ViewKind::ROOT:
      return static_cast<int>(rootRows.size());
    case ViewKind::BOOKS:
      return static_cast<int>(books.size()) + (hasMore ? 1 : 0);
    case ViewKind::FACET:
      return static_cast<int>(facets.size()) + (hasMore ? 1 : 0);
    case ViewKind::LOCAL:
      return static_cast<int>(localBooks.size());
  }
  return 0;
}

std::string BookOrbitCatalogActivity::rowTitle(const int index) const {
  const View& view = stack.back();
  switch (view.kind) {
    case ViewKind::ROOT: {
      const RootRow& row = rootRows[index];
      return row.count >= 0 ? row.title + " (" + std::to_string(row.count) + ")" : row.title;
    }
    case ViewKind::BOOKS:
      return index < static_cast<int>(books.size()) ? books[index].title : std::string(tr(STR_MORE_ELLIPSIS));
    case ViewKind::FACET:
      if (index < static_cast<int>(facets.size())) {
        const auto& entry = facets[index];
        return entry.count > 0 ? entry.title + " (" + std::to_string(entry.count) + ")" : entry.title;
      }
      return std::string(tr(STR_MORE_ELLIPSIS));
    case ViewKind::LOCAL:
      return localBooks[index].title;
  }
  return {};
}

std::string BookOrbitCatalogActivity::rowSubtitle(const int index) const {
  const View& view = stack.back();
  if (view.kind == ViewKind::BOOKS && index < static_cast<int>(books.size())) return books[index].author;
  if (view.kind == ViewKind::LOCAL) return localBooks[index].author;
  return {};
}

std::string BookOrbitCatalogActivity::rowValue(const int index) const {
  const View& view = stack.back();
  // Books carry their synced progress; a dot marks one already on the SD card. Progress wins when
  // a book is both, since "42%" already implies you have it.
  if (view.kind == ViewKind::BOOKS && index < static_cast<int>(books.size())) {
    if (books[index].progressPercentage > 0) return std::to_string(books[index].progressPercentage) + "%";
    if (index < static_cast<int>(booksOnDevice.size()) && booksOnDevice[index]) return "\xE2\x80\xA2";
  }
  if (view.kind == ViewKind::LOCAL && view.local == LocalKind::IN_PROGRESS && localBooks[index].percent >= 0) {
    return std::to_string(localBooks[index].percent) + "%";
  }
  return {};
}

void BookOrbitCatalogActivity::pushBooks(std::string title, BookOrbitBookQuery query) {
  View view;
  view.kind = ViewKind::BOOKS;
  view.title = std::move(title);
  view.query = std::move(query);
  stack.push_back(std::move(view));
  loadCurrent();
}

void BookOrbitCatalogActivity::openSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), "", 64, InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        const auto& kb = std::get<KeyboardResult>(result.data);
        if (kb.text.empty()) return;
        BookOrbitBookQuery query;
        query.sort = "title";
        query.query = kb.text;
        pushBooks("\"" + kb.text + "\"", std::move(query));
      });
}

void BookOrbitCatalogActivity::activateRow(const int index) {
  View& view = current();
  view.selector = index;
  switch (view.kind) {
    case ViewKind::ROOT: {
      const RootRow& row = rootRows[index];
      if (row.kind == RootRow::Kind::SEARCH) {
        openSearch();
        return;
      }
      if (row.kind == RootRow::Kind::ON_DEVICE || row.kind == RootRow::Kind::IN_PROGRESS) {
        View local;
        local.kind = ViewKind::LOCAL;
        local.title = row.title;
        local.local = row.kind == RootRow::Kind::ON_DEVICE ? LocalKind::ON_DEVICE : LocalKind::IN_PROGRESS;
        stack.push_back(std::move(local));
        loadCurrent();
        return;
      }
      if (isFacetSection(row.sectionId)) {
        View facet;
        facet.kind = ViewKind::FACET;
        facet.title = row.title;
        facet.sectionId = row.sectionId;
        stack.push_back(std::move(facet));
        loadCurrent();
        return;
      }
      BookOrbitBookQuery query;
      query.sort = row.sectionId == "continue-reading" ? "recently_read"
                   : row.sectionId == "all-books"      ? "title"
                                                       : "recently_added";
      pushBooks(row.title, std::move(query));
      return;
    }
    case ViewKind::LOCAL:
      openBook(localBooks[index].path);
      return;
    case ViewKind::BOOKS:
      if (index >= static_cast<int>(books.size())) {
        view.page++;
        view.selector = 0;
        loadCurrent();
        return;
      }
      openDetail(books[index]);
      return;
    case ViewKind::FACET: {
      if (index >= static_cast<int>(facets.size())) {
        view.page++;
        view.selector = 0;
        loadCurrent();
        return;
      }
      const auto& entry = facets[index];
      BookOrbitBookQuery query;
      if (view.sectionId == "series") {
        query.sort = "series";
        if (!entry.seriesId.empty()) {
          query.seriesId = entry.seriesId;
        } else {
          query.series = entry.id;
        }
      } else if (view.sectionId == "collections") {
        query.sort = "title";
        query.collectionId = entry.id;
      } else if (view.sectionId == "libraries") {
        query.sort = "title";
        query.libraryId = entry.id;
      } else {
        query.sort = "title";
        query.author = entry.id;
      }
      pushBooks(entry.title, std::move(query));
      return;
    }
  }
}

void BookOrbitCatalogActivity::openBook(const std::string& path) {
  if (!wifiUsed) {
    // No network session to tear down: hand straight over to the reader.
    activityManager.goToReader(path);
    return;
  }
  APP_STATE.openEpubPath = path;
  APP_STATE.saveToFile();
  openAfterExit = true;  // onExit's silent restart lands in the reader
  finish();
}

void BookOrbitCatalogActivity::goBack() {
  if (stack.size() <= 1) {
    finish();
    return;
  }
  stack.pop_back();
  loadCurrent();
}

void BookOrbitCatalogActivity::openDetail(const BookOrbitCatalogBook& book) {
  {
    RenderLock lock(*this);
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
  }
  requestUpdateAndWait();

  if (!BookOrbitCatalogClient::fetchBookDetail(book.id, detail)) {
    fail(tr(STR_KOREADER_SYNC_NETWORK_ERROR));
    return;
  }
  // The listing already knows these; a server that omits them in the detail keeps the row's copy.
  if (detail.title.empty()) detail.title = book.title;
  if (detail.author.empty()) detail.author = book.author;
  detailOnDevice = onDevicePath(detail.title, detail.author, detailPath);
  detailScroll = 0;
  detailLines.clear();

  RenderLock lock(*this);
  state = State::DETAIL;
  requestUpdate(true);
}

void BookOrbitCatalogActivity::downloadBook(const BookOrbitCatalogBook& book) {
  {
    RenderLock lock(*this);
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
  }
  requestUpdateAndWait();

  BookOrbitBookDetail detail;
  if (!BookOrbitCatalogClient::fetchBookDetail(book.id, detail)) {
    fail(tr(STR_KOREADER_SYNC_NETWORK_ERROR));
    return;
  }
  const BookOrbitCatalogFile* epubFile = nullptr;
  for (const auto& file : detail.files) {
    if (file.format == "epub") {
      epubFile = &file;
      break;
    }
  }
  if (!epubFile) {
    fail(tr(STR_BOOKORBIT_NO_EPUB));
    return;
  }

  const std::string folder = BOOKORBIT_STORE.getDownloadFolder();
  if (!folder.empty() && !Storage.exists(folder.c_str())) Storage.mkdir(folder.c_str());
  const std::string path = catalogPath(detail.title.empty() ? book.title : detail.title,
                                       detail.author.empty() ? book.author : detail.author);
  if (Storage.exists(path.c_str())) {
    RenderLock lock(*this);
    downloadedPath = path;
    state = State::DONE;
    statusMessage = tr(STR_BOOKORBIT_ALREADY_ON_DEVICE);
    requestUpdate(true);
    return;
  }

  {
    RenderLock lock(*this);
    state = State::DOWNLOADING;
    statusMessage = detail.title.empty() ? book.title : detail.title;
    downloadDone = 0;
    downloadTotal = epubFile->sizeBytes;
  }
  requestUpdate(true);

  size_t lastShownTenth = 0;
  const bool ok = BookOrbitCatalogClient::downloadFile(epubFile->id, path, [&](size_t done, size_t total) {
    if (total == 0) total = epubFile->sizeBytes;
    const size_t tenth = total > 0 ? done * 10 / total : 0;
    if (tenth != lastShownTenth) {  // a panel refresh per 10% keeps the transfer fast
      lastShownTenth = tenth;
      {
        RenderLock lock(*this);
        downloadDone = done;
        downloadTotal = total;
      }
      requestUpdate();
    }
    return true;
  });
  if (!ok) {
    fail(tr(STR_DOWNLOAD_FAILED));
    return;
  }
  RenderLock lock(*this);
  downloadedPath = path;
  state = State::DONE;
  statusMessage = tr(STR_DOWNLOAD_COMPLETE);
  requestUpdate(true);
}

void BookOrbitCatalogActivity::loop() {
  if (state == State::WIFI || state == State::LOADING || state == State::DOWNLOADING) return;

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (stack.size() > 1) {
        goBack();
      } else {
        finish();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && BOOKORBIT_STORE.hasCredentials() &&
               !offline && WiFi.status() == WL_CONNECTED) {
      loadCurrent();  // retry
    }
    return;
  }

  if (state == State::DONE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !downloadedPath.empty()) {
      openBook(downloadedPath);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      RenderLock lock(*this);
      state = State::LIST;
      requestUpdate(true);
    }
    return;
  }

  if (state == State::DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      RenderLock lock(*this);
      state = State::LIST;
      requestUpdate(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (detailOnDevice && !detailPath.empty()) {
        openBook(detailPath);
      } else {
        BookOrbitCatalogBook book;
        book.id = detail.id;
        book.title = detail.title;
        book.author = detail.author;
        downloadBook(book);
      }
      return;
    }
    // Up/Down scroll the description rather than moving a selection.
    const int lastLine = std::max(0, static_cast<int>(detailLines.size()) - 1);
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Down) && detailScroll < lastLine) {
      detailScroll++;
      requestUpdate();
    } else if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Up) && detailScroll > 0) {
      detailScroll--;
      requestUpdate();
    }
    return;
  }

  // LIST
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  const int rows = rowCount();
  if (rows == 0) return;
  int& selector = current().selector;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow(selector);
    return;
  }
  buttonNavigator.onNextList(ButtonNavigator::getStepNextButtons(), selector, rows, [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(ButtonNavigator::getStepPreviousButtons(), selector, rows,
                                 [this] { requestUpdate(); });
}

void BookOrbitCatalogActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  const std::string& title = stack.empty() ? std::string(tr(STR_BOOKORBIT_CATALOG)) : stack.back().title;
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 title.c_str());
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;
  const int midY = contentTop + contentHeight / 2;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const char* back = tr(STR_BACK);
  const char* confirm = "";
  switch (state) {
    case State::WIFI:
    case State::LOADING:
      renderer.drawCenteredText(UI_10_FONT_ID, midY, statusMessage.empty() ? tr(STR_LOADING) : statusMessage.c_str());
      back = "";
      break;
    case State::DOWNLOADING: {
      renderer.drawCenteredText(UI_10_FONT_ID, midY - lineHeight * 2, tr(STR_DOWNLOADING), true, EpdFontFamily::BOLD);
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, statusMessage.c_str(), contentRect.width - 40, 2);
      int y = midY - lineHeight;
      for (const auto& line : lines) {
        renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
        y += lineHeight;
      }
      const int barWidth = contentRect.width - 80;
      const int barX = contentRect.x + 40;
      const int barY = y + 10;
      renderer.drawRect(barX, barY, barWidth, 14, true);
      if (downloadTotal > 0) {
        const int filled = static_cast<int>(static_cast<uint64_t>(barWidth - 4) *
                                            std::min(downloadDone, downloadTotal) / downloadTotal);
        renderer.fillRect(barX + 2, barY + 2, filled, 10, true);
      }
      back = "";
      break;
    }
    case State::DONE:
      renderer.drawCenteredText(UI_10_FONT_ID, midY - lineHeight, statusMessage.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 4, tr(STR_BOOKORBIT_OPEN_PROMPT));
      confirm = tr(STR_OPEN);
      break;
    case State::ERROR: {
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, errorMessage.c_str(), contentRect.width - 40, 4);
      int y = midY - lineHeight * static_cast<int>(lines.size()) / 2;
      for (const auto& line : lines) {
        renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
        y += lineHeight;
      }
      if (BOOKORBIT_STORE.hasCredentials()) confirm = tr(STR_RETRY);
      break;
    }
    case State::DETAIL:
      renderDetail(contentRect, contentTop, contentHeight);
      back = tr(STR_BACK);
      confirm = detailOnDevice ? tr(STR_OPEN) : tr(STR_DOWNLOAD);
      break;
    case State::LIST: {
      const int rows = rowCount();
      if (rows == 0) {
        renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                          tr(STR_NO_ENTRIES));
      } else {
        const ViewKind kind = stack.back().kind;
        const bool twoLine = kind == ViewKind::BOOKS || kind == ViewKind::LOCAL;
        GUI.drawList(
            renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, rows, stack.back().selector,
            [this](int index) { return rowTitle(index); },
            twoLine ? std::function<std::string(int)>([this](int index) { return rowSubtitle(index); }) : nullptr,
            nullptr, [this](int index) { return rowValue(index); });
        confirm = tr(STR_SELECT);
      }
      break;
    }
  }
  const auto hints = mappedInput.mapHints(back, confirm, "", "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
  renderer.displayBuffer();
}

void BookOrbitCatalogActivity::renderDetail(const Rect& contentRect, const int contentTop, const int contentHeight) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int x = contentRect.x + UITheme::getInstance().getMetrics().contentSidePadding;
  const int width = contentRect.width - (x - contentRect.x) * 2;
  int y = contentTop;

  if (!detail.author.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, renderer.truncatedText(UI_10_FONT_ID, detail.author.c_str(), width).c_str(),
                      true, EpdFontFamily::BOLD);
    y += lineHeight;
  }
  // Series, year, publisher and length on one line each only when the server sent them.
  if (!detail.subtitle.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y,
                      renderer.truncatedText(UI_10_FONT_ID, detail.subtitle.c_str(), width).c_str());
    y += lineHeight;
  }
  if (!detail.seriesName.empty()) {
    std::string line = detail.seriesName;
    if (!detail.seriesIndex.empty()) line += " #" + detail.seriesIndex;
    renderer.drawText(UI_10_FONT_ID, x, y, renderer.truncatedText(UI_10_FONT_ID, line.c_str(), width).c_str());
    y += lineHeight;
  }
  std::string facts;
  if (!detail.publishedYear.empty()) facts = detail.publishedYear;
  if (detail.pageCount > 0) {
    if (!facts.empty()) facts += " - ";
    facts += std::to_string(detail.pageCount) + tr(STR_PAGES_SUFFIX);  // the string carries its own space
  }
  if (!detail.publisher.empty()) {
    if (!facts.empty()) facts += " - ";
    facts += detail.publisher;
  }
  if (detailOnDevice) {
    if (!facts.empty()) facts += " - ";
    facts += tr(STR_BOOKORBIT_ON_DEVICE);
  }
  if (!facts.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, renderer.truncatedText(UI_10_FONT_ID, facts.c_str(), width).c_str());
    y += lineHeight;
  }
  // Where the server thinks you are in it, and what you made of it.
  std::string status;
  if (detail.progressPercentage > 0) status = std::to_string(detail.progressPercentage) + "% read";
  if (detail.rating > 0) {
    if (!status.empty()) status += " - ";
    status += std::string(static_cast<size_t>(detail.rating), '*');
  }
  if (!status.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, status.c_str());
    y += lineHeight;
  }
  if (!detail.genres.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, renderer.truncatedText(UI_10_FONT_ID, detail.genres.c_str(), width).c_str());
    y += lineHeight;
  }
  y += lineHeight / 2;

  // Wrapped once per book, not per frame: the reader scrolls a window over these lines.
  const int roomForLines = std::max(1, (contentTop + contentHeight - y) / lineHeight);
  if (detailLines.empty() && !detail.description.empty()) {
    detailLines = renderer.wrappedText(UI_10_FONT_ID, detail.description.c_str(), width, 120);
  }
  if (detailLines.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_BOOKORBIT_NO_DESCRIPTION));
    return;
  }
  for (int i = 0; i < roomForLines && detailScroll + i < static_cast<int>(detailLines.size()); i++) {
    renderer.drawText(UI_10_FONT_ID, x, y + i * lineHeight, detailLines[detailScroll + i].c_str());
  }
}

ListRowTap::Result BookOrbitCatalogActivity::selectListRow(const int index) {
  if (state != State::LIST || stack.empty()) return ListRowTap::Result::Rejected;
  return ListRowTap::apply(index, rowCount(), stack.back().selector);
}
