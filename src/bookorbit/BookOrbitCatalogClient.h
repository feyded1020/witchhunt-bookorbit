#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// BookOrbit's KOReader-authenticated JSON catalog (browse + download). Ported from
// CrossInk-Bookorbit's BookOrbitCatalogClient (MIT) onto crosspoint::SecureHttpClient, since
// Witch Hunt's HttpDownloader cannot send the x-auth-user / x-auth-key headers.

struct BookOrbitCatalogSection {
  std::string id;
  std::string title;
};

struct BookOrbitCatalogFile {
  int64_t id = 0;
  std::string format;  // lowercase, e.g. "epub"
  size_t sizeBytes = 0;
};

struct BookOrbitCatalogBook {
  int64_t id = 0;
  std::string title;
  std::string author;           // first author only
  int progressPercentage = -1;  // -1 when the server reports none
};

struct BookOrbitFacetEntry {
  std::string id;
  std::string title;
  std::string seriesId;
  int count = 0;
};

struct BookOrbitFacetPage {
  std::vector<BookOrbitFacetEntry> entries;
  int page = 1;
  bool hasNext = false;
};

struct BookOrbitBookQuery {
  std::string sort;
  std::string query;
  std::string author;
  std::string seriesId;
  std::string series;
  std::string collectionId;
  std::string libraryId;
};

// What the detail screen shows before downloading. Field names follow BookOrbit's own
// KoreaderCatalogBookDetail (packages/types/src/koreader.ts); everything except id/title/files is
// optional and a field the server omits is simply not drawn.
struct BookOrbitBookDetail {
  int64_t id = 0;
  std::string title;
  std::string subtitle;
  std::string author;
  std::string seriesName;
  std::string seriesIndex;  // a string upstream: "3", "3.5"
  std::string publisher;
  std::string publishedYear;
  std::string description;
  std::string genres;      // first few, comma separated
  std::string readStatus;  // server's own wording ("reading", "read", ...)
  int pageCount = 0;
  int rating = 0;               // 1-5, 0 when unrated
  int progressPercentage = -1;  // -1 when the server has no progress for this book
  std::vector<BookOrbitCatalogFile> files;
};

// Per-section counts from the server dashboard (what BookOrbit's own plugin badges its
// browse tiles with). -1 = unknown (older server, or field absent).
struct BookOrbitCatalogCounts {
  int totalBooks = -1;
  int inProgress = -1;
  int libraries = -1;
  int authors = -1;
  int series = -1;
  int collections = -1;
};

struct BookOrbitBookPage {
  std::vector<BookOrbitCatalogBook> books;
  int page = 1;
  int total = 0;
  int pageSize = 0;
};

class BookOrbitCatalogClient {
 public:
  static constexpr int PAGE_SIZE = 20;
  using ProgressFn = std::function<bool(size_t downloaded, size_t total)>;

  // Set when the last failed fetch reached a server that did not answer with the catalog API
  // (HTML, 404, redirect loop): "check the server", as opposed to "check the connection".
  static bool lastFetchBadResponse;

  static bool fetchRootSections(std::vector<BookOrbitCatalogSection>& outSections);
  // Decorative: callers treat a failure as "show no counts".
  static bool fetchCatalogCounts(BookOrbitCatalogCounts& outCounts);
  static bool fetchBooks(const BookOrbitBookQuery& query, int page, BookOrbitBookPage& outPage);
  static bool fetchSectionEntries(const std::string& sectionId, int page, BookOrbitFacetPage& outPage);
  static bool fetchBookDetail(int64_t bookId, BookOrbitBookDetail& outDetail);
  // Streams the file to destPath (written to destPath + ".part" and renamed on success).
  static bool downloadFile(int64_t fileId, const std::string& destPath, const ProgressFn& progress);
};
