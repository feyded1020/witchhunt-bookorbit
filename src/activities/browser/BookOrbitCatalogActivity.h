#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "bookorbit/BookOrbitCatalogClient.h"
#include "util/ButtonNavigator.h"

/**
 * Browse a BookOrbit library and download EPUBs. The root lists the server's sections with
 * their book counts (recently added, continue reading, all books, authors, series,
 * collections, libraries), then two offline sections -- On device and In progress -- and
 * Search. Books already on the SD card are marked and open directly instead of downloading
 * again. Built on Witch Hunt's list UI; the catalog API and root layout follow
 * CrossInk-Bookorbit's browser.
 */
class BookOrbitCatalogActivity final : public Activity {
 public:
  explicit BookOrbitCatalogActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookOrbitCatalog", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class State { WIFI, LOADING, LIST, DOWNLOADING, DONE, ERROR };
  enum class ViewKind { ROOT, BOOKS, FACET, LOCAL };
  enum class LocalKind { ON_DEVICE, IN_PROGRESS };

  struct View {
    ViewKind kind = ViewKind::ROOT;
    std::string title;
    BookOrbitBookQuery query;  // BOOKS
    std::string sectionId;     // FACET
    LocalKind local = LocalKind::ON_DEVICE;
    int page = 1;
    int selector = 0;
  };

  // One root row: a server section, an offline section, or Search.
  struct RootRow {
    enum class Kind { SERVER, ON_DEVICE, IN_PROGRESS, SEARCH } kind;
    std::string title;
    std::string sectionId;
    int count = -1;
  };

  struct LocalBook {
    std::string title;
    std::string author;
    std::string path;
    int percent = -1;
  };

  State state = State::WIFI;
  std::vector<View> stack;
  ButtonNavigator buttonNavigator;
  bool wifiUsed = false;
  bool offline = false;  // no WiFi: only the local sections are offered
  bool openAfterExit = false;

  // Rows of the current view.
  std::vector<RootRow> rootRows;
  std::vector<BookOrbitCatalogBook> books;
  std::vector<bool> booksOnDevice;  // parallel to `books`
  std::vector<BookOrbitFacetEntry> facets;
  std::vector<LocalBook> localBooks;
  bool hasMore = false;

  std::string statusMessage;
  std::string errorMessage;
  std::string downloadedPath;
  size_t downloadDone = 0;
  size_t downloadTotal = 0;

  View& current() { return stack.back(); }
  int rowCount() const;
  std::string rowTitle(int index) const;
  std::string rowSubtitle(int index) const;
  std::string rowValue(int index) const;

  void onWifiReady(bool connected);
  void loadCurrent();
  void buildRoot(const std::vector<BookOrbitCatalogSection>& sections, const BookOrbitCatalogCounts& counts);
  // Offline sections. Returns the count; fills `out` when given.
  static size_t collectLocal(LocalKind kind, std::vector<LocalBook>* out);
  void activateRow(int index);
  void goBack();
  void pushBooks(std::string title, BookOrbitBookQuery query);
  void openSearch();
  void openBook(const std::string& path);
  void downloadBook(const BookOrbitCatalogBook& book);
  void fail(const std::string& message);
};
