#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "bookorbit/BookOrbitCatalogClient.h"
#include "util/ButtonNavigator.h"

/**
 * Browse a BookOrbit library and download EPUBs: the server's root sections (recently added,
 * continue reading, all books, authors, series, collections, libraries), free-text search,
 * paged listings, and a download into the configured BookOrbit download folder. Built on
 * Witch Hunt's list UI; the catalog API follows CrossInk-Bookorbit's client.
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
  enum class ViewKind { ROOT, BOOKS, FACET };

  struct View {
    ViewKind kind = ViewKind::ROOT;
    std::string title;
    BookOrbitBookQuery query;  // BOOKS
    std::string sectionId;     // FACET
    int page = 1;
    int selector = 0;
  };

  State state = State::WIFI;
  std::vector<View> stack;
  ButtonNavigator buttonNavigator;
  bool wifiUsed = false;
  bool openAfterExit = false;

  // Rows of the current view. ROOT: sections + Search. BOOKS/FACET: entries + optional "More".
  std::vector<BookOrbitCatalogSection> sections;
  std::vector<BookOrbitCatalogBook> books;
  std::vector<BookOrbitFacetEntry> facets;
  bool hasMore = false;

  std::string statusMessage;
  std::string errorMessage;
  std::string downloadedPath;
  size_t downloadDone = 0;
  size_t downloadTotal = 0;

  View& current() { return stack.back(); }
  int rowCount() const;
  std::string rowLabel(int index) const;

  void onWifiReady(bool connected);
  void loadCurrent();
  void activateRow(int index);
  void goBack();
  void pushBooks(std::string title, BookOrbitBookQuery query);
  void openSearch();
  void downloadBook(const BookOrbitCatalogBook& book);
  void fail(const std::string& message);
};
