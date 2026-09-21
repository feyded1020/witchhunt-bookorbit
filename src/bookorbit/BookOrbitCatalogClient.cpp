#include "BookOrbitCatalogClient.h"

#include <ArduinoJson.h>
#include <BookOrbitCredentialStore.h>
#include <BookOrbitSyncClient.h>
#include <CrossPointRoots.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <cctype>
#include <cstdio>

#include "CrossPointSettings.h"

bool BookOrbitCatalogClient::lastFetchBadResponse = false;

namespace {
std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

void configure(crosspoint::SecureHttpClient& http) {
  // Same policy as sync: these requests carry the account's credentials.
  http.setCACert(SETTINGS.skipHttpsValidation ? nullptr : CROSSPOINT_ROOTS_PEM);
  http.setAllowInsecureFallback(false);
  http.setAllowCertificateDateErrors(!HalClock::isPlausibleForTls());
  http.setTimeout(20000);
  http.setUserAgent("WitchReader-BookOrbit-ESP32-" CROSSPOINT_VERSION);
  http.clearHeaders();
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");  // we cannot decompress
  http.addHeader("x-auth-user", BOOKORBIT_STORE.getUsername());
  http.addHeader("x-auth-key", BOOKORBIT_STORE.getMd5Password());
}

bool fetchJson(const std::string& url, const JsonDocument& filter, JsonDocument& outDoc) {
  BookOrbitCatalogClient::lastFetchBadResponse = false;
  int status = 0;
  std::string body;
  // Two attempts: a fetch can lose a transient allocation race during the handshake.
  for (int attempt = 0; attempt < 2; attempt++) {
    crosspoint::SecureHttpClient http;
    configure(http);
    status = http.GET(url);
    if (status == 200) {
      body = http.getBody();
      break;
    }
    LOG_ERR("BookOrbit", "Catalog fetch attempt %d failed (status=%d): %s", attempt + 1, status, url.c_str());
    if (status >= 300) break;  // an HTTP answer will not change on retry
  }
  if (status != 200) {
    if (status == 404 || status == 405 || (status >= 300 && status < 400)) {
      BookOrbitCatalogClient::lastFetchBadResponse = true;
    }
    return false;
  }
  const DeserializationError error = deserializeJson(outDoc, body, DeserializationOption::Filter(filter));
  // Lenient parsing turns an HTML page into a bare string: every catalog endpoint returns an object.
  if (error || !outDoc.is<JsonObject>()) {
    BookOrbitCatalogClient::lastFetchBadResponse = true;
    LOG_ERR("BookOrbit", "Catalog response is not the catalog API (%s, %u bytes): %.64s",
            error ? error.c_str() : "not a JSON object", (unsigned)body.size(), body.c_str());
    return false;
  }
  return true;
}

constexpr const char* SUPPORTED_SECTIONS[] = {
    "recent", "continue-reading", "all-books", "authors", "series", "collections", "libraries",
};

bool isSupportedSection(const std::string& id) {
  for (const char* s : SUPPORTED_SECTIONS) {
    if (id == s) return true;
  }
  return false;
}

// First of the given keys that carries a non-empty string.
std::string firstString(JsonObjectConst doc, const char* key, const char* fallbackKey) {
  const char* value = doc[key] | "";
  if (value && *value) return value;
  if (!fallbackKey) return "";
  const char* alt = doc[fallbackKey] | "";
  return alt ? alt : "";
}

// Same, for fields servers send as either a number or a string.
std::string numberOrString(JsonObjectConst doc, const char* key, const char* fallbackKey) {
  for (const char* k : {key, fallbackKey}) {
    if (!k || doc[k].isNull()) continue;
    if (doc[k].is<const char*>()) {
      const char* value = doc[k] | "";
      if (value && *value) return value;
    } else {
      const long long value = doc[k] | 0LL;
      if (value != 0) return std::to_string(value);
    }
  }
  return "";
}

std::string firstAuthor(JsonVariantConst authors) {
  JsonArrayConst list = authors.as<JsonArrayConst>();
  if (list.isNull() || list.size() == 0) return "";
  const char* first = list[0].as<const char*>();
  return first ? first : "";
}
}  // namespace

bool BookOrbitCatalogClient::fetchRootSections(std::vector<BookOrbitCatalogSection>& outSections) {
  outSections.clear();
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  JsonDocument filter;
  filter["sections"][0]["section"] = true;
  filter["sections"][0]["title"] = true;
  JsonDocument doc;
  if (!fetchJson(BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/root", filter, doc)) return false;
  for (JsonObjectConst section : doc["sections"].as<JsonArrayConst>()) {
    const char* id = section["section"] | "";
    if (!isSupportedSection(id)) continue;
    outSections.push_back({id, std::string(section["title"] | id)});
  }
  return true;
}

bool BookOrbitCatalogClient::fetchCatalogCounts(BookOrbitCatalogCounts& outCounts) {
  outCounts = BookOrbitCatalogCounts{};
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  // The dashboard also carries book lists; the filter keeps only the counts in memory.
  JsonDocument filter;
  filter["totalBooks"] = true;
  filter["browseCounts"]["inProgress"] = true;
  filter["browseCounts"]["libraries"] = true;
  filter["browseCounts"]["authors"] = true;
  filter["browseCounts"]["series"] = true;
  filter["browseCounts"]["collections"] = true;
  JsonDocument doc;
  if (!fetchJson(BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/dashboard", filter, doc)) return false;
  outCounts.totalBooks = doc["totalBooks"] | -1;
  outCounts.inProgress = doc["browseCounts"]["inProgress"] | -1;
  outCounts.libraries = doc["browseCounts"]["libraries"] | -1;
  outCounts.authors = doc["browseCounts"]["authors"] | -1;
  outCounts.series = doc["browseCounts"]["series"] | -1;
  outCounts.collections = doc["browseCounts"]["collections"] | -1;
  return true;
}

bool BookOrbitCatalogClient::fetchBooks(const BookOrbitBookQuery& query, const int page, BookOrbitBookPage& outPage) {
  outPage = BookOrbitBookPage{};
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/books?page=" + std::to_string(page) +
                    "&size=" + std::to_string(PAGE_SIZE);
  if (!query.sort.empty()) url += "&sort=" + urlEncode(query.sort);
  if (!query.query.empty()) url += "&q=" + urlEncode(query.query);
  if (!query.author.empty()) url += "&author=" + urlEncode(query.author);
  // As BookOrbit's own plugin: numeric series id preferred, name as the fallback.
  if (!query.seriesId.empty()) {
    url += "&seriesId=" + urlEncode(query.seriesId);
  } else if (!query.series.empty()) {
    url += "&series=" + urlEncode(query.series);
  }
  if (!query.collectionId.empty()) url += "&collectionId=" + urlEncode(query.collectionId);
  if (!query.libraryId.empty()) url += "&libraryId=" + urlEncode(query.libraryId);

  JsonDocument filter;
  filter["page"] = true;
  filter["total"] = true;
  filter["size"] = true;
  filter["items"][0]["id"] = true;
  filter["items"][0]["title"] = true;
  filter["items"][0]["authors"] = true;
  JsonDocument doc;
  if (!fetchJson(url, filter, doc)) return false;
  outPage.page = doc["page"] | page;
  outPage.total = doc["total"] | 0;
  outPage.pageSize = doc["size"] | PAGE_SIZE;
  outPage.books.reserve(doc["items"].size());
  for (JsonObjectConst item : doc["items"].as<JsonArrayConst>()) {
    BookOrbitCatalogBook book;
    book.id = item["id"] | 0;
    book.title = std::string(item["title"] | "");
    book.author = firstAuthor(item["authors"]);
    outPage.books.push_back(std::move(book));
  }
  return true;
}

bool BookOrbitCatalogClient::fetchSectionEntries(const std::string& sectionId, const int page,
                                                 BookOrbitFacetPage& outPage) {
  outPage = BookOrbitFacetPage{};
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/sections/" + urlEncode(sectionId);
  if (page > 1) url += "?page=" + std::to_string(page);

  JsonDocument filter;
  filter["page"] = true;
  filter["hasNext"] = true;
  filter["items"][0]["id"] = true;
  filter["items"][0]["title"] = true;
  filter["items"][0]["count"] = true;
  filter["items"][0]["seriesId"] = true;
  JsonDocument doc;
  if (!fetchJson(url, filter, doc)) return false;
  outPage.page = doc["page"] | page;
  outPage.hasNext = doc["hasNext"] | false;
  outPage.entries.reserve(doc["items"].size());
  for (JsonObjectConst item : doc["items"].as<JsonArrayConst>()) {
    BookOrbitFacetEntry entry;
    // Ids are strings on some server versions and numbers on others.
    if (item["id"].is<const char*>()) {
      entry.id = std::string(item["id"] | "");
    } else if (!item["id"].isNull()) {
      entry.id = std::to_string(item["id"] | 0LL);
    }
    entry.title = std::string(item["title"] | entry.id.c_str());
    entry.count = item["count"] | 0;
    if (item["seriesId"].is<const char*>()) {
      entry.seriesId = std::string(item["seriesId"] | "");
    } else if (!item["seriesId"].isNull()) {
      entry.seriesId = std::to_string(item["seriesId"] | 0LL);
    }
    outPage.entries.push_back(std::move(entry));
  }
  return true;
}

bool BookOrbitCatalogClient::fetchBookDetail(const int64_t bookId, BookOrbitBookDetail& outDetail) {
  outDetail = BookOrbitBookDetail{};
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  // BookOrbit's plugin sends a deviceId with detail requests (per-device read state).
  const std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/books/" + std::to_string(bookId) +
                          "?deviceId=" + BookOrbitSyncClient::deviceId();
  JsonDocument filter;
  filter["id"] = true;
  filter["title"] = true;
  filter["authors"] = true;
  // Optional metadata, in the spellings different BookOrbit versions use. A filter entry for a
  // key the server never sends costs nothing.
  filter["description"] = true;
  filter["summary"] = true;
  filter["series"] = true;
  filter["seriesIndex"] = true;
  filter["seriesNumber"] = true;
  filter["publisher"] = true;
  filter["publishedYear"] = true;
  filter["publishDate"] = true;
  filter["year"] = true;
  filter["pageCount"] = true;
  filter["pages"] = true;
  filter["files"][0]["id"] = true;
  filter["files"][0]["format"] = true;
  filter["files"][0]["sizeBytes"] = true;
  JsonDocument doc;
  if (!fetchJson(url, filter, doc)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  outDetail.id = doc["id"] | bookId;
  outDetail.title = std::string(doc["title"] | "");
  outDetail.author = firstAuthor(doc["authors"]);
  outDetail.description = firstString(root, "description", "summary");
  outDetail.series = firstString(root, "series", nullptr);
  outDetail.seriesIndex = numberOrString(root, "seriesIndex", "seriesNumber");
  outDetail.publisher = firstString(root, "publisher", nullptr);
  outDetail.published = numberOrString(root, "publishedYear", "year");
  if (outDetail.published.empty()) outDetail.published = firstString(root, "publishDate", nullptr);
  // A date like "1937-09-21" is only worth its year on a screen this size.
  if (outDetail.published.size() > 4) outDetail.published.resize(4);
  outDetail.pageCount = doc["pageCount"] | doc["pages"] | 0;
  for (JsonObjectConst file : doc["files"].as<JsonArrayConst>()) {
    BookOrbitCatalogFile entry;
    entry.id = file["id"] | 0;
    entry.format = std::string(file["format"] | "");
    for (auto& c : entry.format) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    entry.sizeBytes = file["sizeBytes"] | 0;
    outDetail.files.push_back(std::move(entry));
  }
  return true;
}

bool BookOrbitCatalogClient::downloadFile(const int64_t fileId, const std::string& destPath,
                                          const ProgressFn& progress) {
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  const std::string url =
      BOOKORBIT_STORE.getBaseUrl() + "/plugin/catalog/files/" + std::to_string(fileId) + "/download";
  const std::string partPath = destPath + ".part";
  FsFile out;
  if (!Storage.openFileForWrite("BookOrbit", partPath, out)) {
    LOG_ERR("BookOrbit", "Cannot create %s", partPath.c_str());
    return false;
  }
  crosspoint::SecureHttpClient http;
  configure(http);
  http.clearHeaders();  // the download is not JSON
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("x-auth-user", BOOKORBIT_STORE.getUsername());
  http.addHeader("x-auth-key", BOOKORBIT_STORE.getMd5Password());
  bool writeOk = true;
  const int status = http.get(
      url,
      [&](const uint8_t* data, const size_t len) {
        writeOk = out.write(data, len) == len;
        return writeOk;
      },
      [&](const size_t done, const size_t total) { return progress ? progress(done, total) : true; });
  out.close();
  if (status != 200 || !writeOk) {
    LOG_ERR("BookOrbit", "Download failed (status=%d, write=%d): %s", status, writeOk ? 1 : 0, url.c_str());
    Storage.remove(partPath.c_str());
    return false;
  }
  if (Storage.exists(destPath.c_str())) Storage.remove(destPath.c_str());
  if (!Storage.rename(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("BookOrbit", "Could not move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return false;
  }
  return true;
}
