#include "BookOrbitCatalogActivity.h"

#include <BookOrbitCredentialStore.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
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
    fail(tr(STR_WIFI_CONN_FAILED));
    return;
  }
  WiFi.setSleep(false);
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
  bool ok = false;
  std::vector<BookOrbitCatalogSection> newSections;
  std::vector<BookOrbitCatalogBook> newBooks;
  std::vector<BookOrbitFacetEntry> newFacets;
  bool more = false;
  switch (view.kind) {
    case ViewKind::ROOT:
      ok = BookOrbitCatalogClient::fetchRootSections(newSections);
      break;
    case ViewKind::BOOKS: {
      BookOrbitBookPage page;
      ok = BookOrbitCatalogClient::fetchBooks(view.query, view.page, page);
      if (ok) {
        newBooks = std::move(page.books);
        more = page.total > 0 ? view.page * BookOrbitCatalogClient::PAGE_SIZE < page.total
                              : static_cast<int>(newBooks.size()) >= BookOrbitCatalogClient::PAGE_SIZE;
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
  }
  if (!ok) {
    fail(BookOrbitCatalogClient::lastFetchBadResponse ? tr(STR_BOOKORBIT_SYNC_HTTP_404)
                                                      : tr(STR_KOREADER_SYNC_NETWORK_ERROR));
    return;
  }
  RenderLock lock(*this);
  sections = std::move(newSections);
  books = std::move(newBooks);
  facets = std::move(newFacets);
  hasMore = more;
  const int rows = rowCount();
  if (view.selector >= rows) view.selector = rows > 0 ? rows - 1 : 0;
  state = State::LIST;
  requestUpdate(true);
}

int BookOrbitCatalogActivity::rowCount() const {
  const View& view = stack.back();
  switch (view.kind) {
    case ViewKind::ROOT:
      return static_cast<int>(sections.size()) + 1;  // + Search
    case ViewKind::BOOKS:
      return static_cast<int>(books.size()) + (hasMore ? 1 : 0);
    case ViewKind::FACET:
      return static_cast<int>(facets.size()) + (hasMore ? 1 : 0);
  }
  return 0;
}

std::string BookOrbitCatalogActivity::rowLabel(const int index) const {
  const View& view = stack.back();
  switch (view.kind) {
    case ViewKind::ROOT:
      return index < static_cast<int>(sections.size()) ? sections[index].title : std::string(tr(STR_SEARCH));
    case ViewKind::BOOKS:
      if (index < static_cast<int>(books.size())) {
        const auto& book = books[index];
        return book.author.empty() ? book.title : book.title + " - " + book.author;
      }
      return std::string(tr(STR_MORE_ELLIPSIS));
    case ViewKind::FACET:
      if (index < static_cast<int>(facets.size())) {
        const auto& entry = facets[index];
        return entry.count > 0 ? entry.title + " (" + std::to_string(entry.count) + ")" : entry.title;
      }
      return std::string(tr(STR_MORE_ELLIPSIS));
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
      if (index >= static_cast<int>(sections.size())) {
        openSearch();
        return;
      }
      const auto& section = sections[index];
      if (isFacetSection(section.id)) {
        View facet;
        facet.kind = ViewKind::FACET;
        facet.title = section.title;
        facet.sectionId = section.id;
        stack.push_back(std::move(facet));
        loadCurrent();
        return;
      }
      BookOrbitBookQuery query;
      query.sort = section.id == "continue-reading" ? "recently_read"
                   : section.id == "all-books"      ? "title"
                                                    : "recently_added";
      pushBooks(section.title, std::move(query));
      return;
    }
    case ViewKind::BOOKS:
      if (index >= static_cast<int>(books.size())) {
        view.page++;
        view.selector = 0;
        loadCurrent();
        return;
      }
      downloadBook(books[index]);
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

void BookOrbitCatalogActivity::goBack() {
  if (stack.size() <= 1) {
    finish();
    return;
  }
  stack.pop_back();
  loadCurrent();
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
  const std::string path = folder + "/" +
                           fileNameFor(detail.title.empty() ? book.title : detail.title,
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
               WiFi.status() == WL_CONNECTED) {
      loadCurrent();  // retry
    }
    return;
  }

  if (state == State::DONE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !downloadedPath.empty()) {
      APP_STATE.openEpubPath = downloadedPath;
      APP_STATE.saveToFile();
      openAfterExit = true;
      finish();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      RenderLock lock(*this);
      state = State::LIST;
      requestUpdate(true);
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
        const int filled = static_cast<int>(static_cast<uint64_t>(barWidth - 4) * std::min(downloadDone, downloadTotal) /
                                            downloadTotal);
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
    case State::LIST: {
      const int rows = rowCount();
      if (rows == 0) {
        renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                          tr(STR_NO_ENTRIES));
      } else {
        GUI.drawList(renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, rows,
                     stack.back().selector, [this](int index) { return rowLabel(index); });
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

ListRowTap::Result BookOrbitCatalogActivity::selectListRow(const int index) {
  if (state != State::LIST || stack.empty()) return ListRowTap::Result::Rejected;
  return ListRowTap::apply(index, rowCount(), stack.back().selector);
}
