#include "BookOrbitSyncClient.h"

#include <ArduinoJson.h>
#include <CrossPointRoots.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <esp_mac.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "BookOrbitCredentialStore.h"

// Ported from agosez/CrossInk-Bookorbit (MIT). The transport was moved onto Witch Hunt's
// crosspoint::SecureHttpClient (wolfSSL, curated root set) so BookOrbit shares the same
// certificate policy as every other network path in this firmware.

int BookOrbitSyncClient::lastHttpCode = 0;
int BookOrbitSyncClient::lastTransportError = 0;

const char* BookOrbitSyncClient::deviceId() {
  static const std::array<char, 32> id = [] {
    std::array<char, 32> value{};
    uint8_t mac[6] = {};
    // "witchreader-", not CrossInk's "crossink-": the server keys highlight/bookmark sync
    // state per device id and treats anything missing from a device's key set as deleted.
    // This firmware does not read CrossInk's local highlight/bookmark files, so reusing the
    // CrossInk id on the same reader would have its first sync delete everything CrossInk had
    // synced. A new id is a new device to the server, which instead sends those down.
    if (esp_efuse_mac_get_default(mac) != 0) {
      LOG_ERR("BookOrbit", "Could not read factory MAC; falling back to shared device id");
      snprintf(value.data(), value.size(), "witchreader-device");
      return value;
    }
    snprintf(value.data(), value.size(), "witchreader-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    return value;
  }();
  return id.data();
}

namespace {
std::string formatHttpStatusMessage(int httpCode) {
  char buffer[96];
  snprintf(buffer, sizeof(buffer), tr(STR_KOREADER_SYNC_HTTP_STATUS_FORMAT), httpCode);
  return std::string(buffer);
}

std::string networkErrorMessage() {
  switch (BookOrbitSyncClient::lastTransportError) {
    case crosspoint::SecureHttpClient::ERR_CONNECT:
    case crosspoint::SecureHttpClient::ERR_BAD_URL:
      return tr(STR_KOREADER_SYNC_NETWORK_REFUSED);
    case crosspoint::SecureHttpClient::ERR_TIMEOUT:
    case crosspoint::SecureHttpClient::ERR_TRUNCATED:
    case crosspoint::SecureHttpClient::ERR_SEND:
      return tr(STR_KOREADER_SYNC_NETWORK_TIMEOUT);
    default:
      return tr(STR_KOREADER_SYNC_NETWORK_ERROR);
  }
}

const char* classifyJsonBody(const char* body) {
  if (!body || body[0] == '\0') return "empty response";

  const char* cursor = body;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
    cursor++;
  }

  if (*cursor == '\0') return "blank response";
  if (*cursor == '<') return "HTML response";
  if (*cursor != '{' && *cursor != '[') return "non-JSON response";
  return "malformed JSON";
}

void logJsonParseFailure(const char* context, DeserializationError error, const char* body) {
  char preview[97];
  size_t i = 0;
  if (body) {
    for (; i < sizeof(preview) - 1 && body[i] != '\0'; i++) {
      const char c = body[i];
      preview[i] = (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
    }
  }
  preview[i] = '\0';

  LOG_ERR("BookOrbit", "%s JSON parse failed: %s (%s, preview=\"%s\")", context, error.c_str(), classifyJsonBody(body),
          preview);
}

BookOrbitSyncClient::Error validateAuthResponse(const char* body) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body ? body : "");
  if (error) {
    logJsonParseFailure("Auth", error, body);
    return BookOrbitSyncClient::JSON_ERROR;
  }

  if (!doc.is<JsonObject>()) {
    LOG_ERR("BookOrbit", "Auth response was not a JSON object");
    return BookOrbitSyncClient::INVALID_AUTH_RESPONSE;
  }

  const char* authorized = doc["authorized"] | nullptr;
  if (authorized && std::strcmp(authorized, "OK") != 0) {
    LOG_ERR("BookOrbit", "Auth response explicitly denied authorization");
    return BookOrbitSyncClient::INVALID_AUTH_RESPONSE;
  }

  return BookOrbitSyncClient::OK;
}

// See the identical comment in KOReaderSyncClient.cpp: TLS handshakes on the ESP32-C3
// collectively consume tens of KB of heap, so we refuse to even attempt one below this
// floor rather than risk an aggregate-exhaustion allocation failure mid-handshake.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;

// A request that reuses the Session's established connection pays no handshake, and the
// full floor would double-count the memory that live TLS session already holds -- it
// refused affordable uploads whenever a request followed another in the same session
// (stats after the progress fetch, bookmarks after highlights: measured 54804 free
// against the 55000 floor). Such a request only needs its JSON body and framing.
//
// This floor bounds the connection's cost, NOT the body's: a caller that builds a body
// too large for what is left still has to fail gracefully, which is why bodies are
// measured and allocated without throwing (see JsonBody). Callers keep their batches
// small for the same reason -- 25 stat events, 8 annotations.
constexpr uint32_t MIN_HEAP_FOR_KEPT_SESSION = 20000;

// True from the first completed request on the shared session until the Session ends.
// Never set on the simulator, which keeps the full floor. Blind spot: a server dropping
// the keep-alive mid-session makes the next request re-handshake under the small floor;
// wolfSSL fails an unaffordable handshake cleanly, so that degrades to NETWORK_ERROR
// and a retry on the next sync rather than a crash.
bool s_sessionHandshakePaid = false;

uint32_t requiredHeapFloor() { return s_sessionHandshakePaid ? MIN_HEAP_FOR_KEPT_SESSION : MIN_HEAP_FOR_TLS; }

/**
 * One exactly-sized request body, allocated without throwing.
 *
 * Serializing into a std::string costs more than the body: its growth doubles, so
 * the last reallocation holds both halves at once, and with exceptions disabled a
 * failed allocation inside it calls abort() rather than returning -- the firmware
 * dies where it could have reported a failure. That is the measured crash on a
 * 100-event stats batch: ~8KB of body plus the doubling, admitted by the
 * kept-session floor and then unaffordable.
 *
 * Measuring first costs one pass over the document and turns the same situation
 * into a LOW_MEMORY return. Build inside the JsonDocument's scope so the node pool
 * is freed before the request runs; only this buffer stays alive through it.
 */
class JsonBody {
 public:
  bool build(const JsonDocument& doc) {
    const size_t length = measureJson(doc);
    data_ = makeUniqueNoThrow<char[]>(length + 1);
    if (!data_) {
      LOG_ERR("BookOrbit", "Out of memory for a %u byte request body (heap: %u free, %u max alloc)",
              (unsigned)(length + 1), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
      return false;
    }
    length_ = serializeJson(doc, data_.get(), length + 1);
    return true;
  }

  const char* c_str() const { return data_ ? data_.get() : ""; }
  const uint8_t* bytes() const { return reinterpret_cast<const uint8_t*>(c_str()); }
  size_t length() const { return length_; }

  // Frees the body once it has been sent: the response still has to be parsed, and
  // an exchange's request and response should not be held at once on this hardware.
  void release() {
    data_.reset();
    length_ = 0;
  }

 private:
  std::unique_ptr<char[]> data_;
  size_t length_ = 0;
};

void logHeapStats(const char* phase, const char* url = nullptr) {
  LOG_DBG("BookOrbit", "%s%s%s heap: free=%u min=%u max_alloc=%u", phase, url ? " " : "", url ? url : "",
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

bool s_skipTlsValidation = false;

// Non-null while a BookOrbitSyncClient::Session is alive; requests then share its socket
// (SecureHttpClient keeps the connection when the next request targets the same host).
std::unique_ptr<crosspoint::SecureHttpClient> s_session;

void configureClient(crosspoint::SecureHttpClient& http) {
  // Same policy as KOReaderSyncClient: verify against the curated roots and fail closed,
  // because every request carries the account's credentials. The user's "skip HTTPS
  // validation" setting (no roots) is the escape hatch for a private CA.
  http.setCACert(s_skipTlsValidation ? nullptr : CROSSPOINT_ROOTS_PEM);
  http.setAllowInsecureFallback(false);
  http.setAllowCertificateDateErrors(!HalClock::isPlausibleForTls());
  http.setTimeout(15000);
  http.setUserAgent("WitchReader-BookOrbit-ESP32-" CROSSPOINT_VERSION);
}

// Returns the HTTP status, or a negative SecureHttpClient error when the transport itself
// failed. The response body, if any, lands in outBody.
int sendBookOrbitRequest(const char* method, const std::string& url, const JsonBody* payload, std::string& outBody) {
  outBody.clear();
  const bool pooled = s_session != nullptr;
  crosspoint::SecureHttpClient oneShot;
  crosspoint::SecureHttpClient& http = pooled ? *s_session : oneShot;
  configureClient(http);
  logHeapStats("Before request", url.c_str());
  http.clearHeaders();
  // BookOrbit's kosync-compatible auth headers. Accept is plain application/json to mirror
  // BookOrbit's own KOReader plugin (not the kosync vendor type).
  http.addHeader("Accept", "application/json");
  http.addHeader("x-auth-user", BOOKORBIT_STORE.getUsername());
  http.addHeader("x-auth-key", BOOKORBIT_STORE.getMd5Password());
  if (payload != nullptr) {
    http.addHeader("Content-Type", "application/json");
  }
  const int code = http.request(method, url, payload != nullptr ? std::string(payload->c_str(), payload->length()) : "");
  if (code >= 100) {
    outBody = http.getBody();
  } else {
    LOG_ERR("BookOrbit", "%s %s failed (transport rc=%d)", method, url.c_str(), code);
  }
  // An HTTP status, even an error one, proves the session's handshake is up and paid
  // for; a transport failure may have closed the socket, so the next request assumes
  // it pays a new one.
  if (pooled) {
    s_sessionHandshakePaid = code >= 100;
  } else {
    http.close();
  }
  logHeapStats("After request");
  return code >= 100 ? code : (code < 0 ? code : -1);
}

}  // namespace

void BookOrbitSyncClient::setSkipTlsValidation(const bool skip) { s_skipTlsValidation = skip; }

const char* BookOrbitSyncClient::lastFailureDetail() {
  static char detail[32];
  if (lastTransportError != 0) {
    snprintf(detail, sizeof(detail), "transport %d", lastTransportError);
  } else if (lastHttpCode >= 400) {
    snprintf(detail, sizeof(detail), "HTTP %d", lastHttpCode);
  } else {
    detail[0] = '\0';
  }
  return detail;
}

BookOrbitSyncClient::Session::Session() {
  s_sessionHandshakePaid = false;
  s_session = makeUniqueNoThrow<crosspoint::SecureHttpClient>();
  if (!s_session) {
    LOG_ERR("BookOrbit", "Session allocation failed; requests will open their own connections");
    return;
  }
  LOG_DBG("BookOrbit", "Sync session open: requests share one TLS connection");
}

BookOrbitSyncClient::Session::~Session() {
  s_sessionHandshakePaid = false;
  if (s_session) {
    s_session->close();
    s_session.reset();
    LOG_DBG("BookOrbit", "Sync session closed");
  }
}

BookOrbitSyncClient::Error BookOrbitSyncClient::authenticate() {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/users/auth";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Authenticating: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  std::string body;
  const int httpCode = sendBookOrbitRequest("GET", url, nullptr, body);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_DBG("BookOrbit", "Auth response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 200) return validateAuthResponse(body.c_str());
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::getProgress(const std::string& documentHash,
                                                            KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Getting progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  std::string body;
  const int httpCode = sendBookOrbitRequest("GET", url, nullptr, body);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_DBG("BookOrbit", "Get progress response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;

  if (httpCode == 200 && !body.empty()) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, body);

    if (error) {
      logJsonParseFailure("Get progress", error, body.c_str());
      return JSON_ERROR;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    LOG_DBG("BookOrbit", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/syncs/progress";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Updating progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  // Build JSON body. BookOrbit additionally understands "timestamp" (unlike a plain
  // koreader-sync server) to break ties when two devices report the same percentage;
  // callers populate progress.timestamp from wall-clock time after an NTP sync.
  JsonDocument doc;
  doc["document"] = progress.document;
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = progress.device;
  doc["device_id"] = deviceId();
  if (progress.timestamp > 0) {
    doc["timestamp"] = progress.timestamp;
  }

  JsonBody body;
  if (!body.build(doc)) return LOW_MEMORY;

  LOG_DBG("BookOrbit", "Request body: %s", body.c_str());

  LOG_DBG("BookOrbit", "PUT body bytes=%u", static_cast<unsigned>(body.length()));
  std::string response;
  const int httpCode = sendBookOrbitRequest("PUT", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_DBG("BookOrbit", "Update progress response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 200 || httpCode == 202) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::uploadPageStats(const std::string& documentHash,
                                                                const std::string& deviceModel,
                                                                const BookOrbitStatEvent* events, size_t count) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }
  if (!events || count == 0) {
    return OK;
  }

  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/page-stats";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Uploading %u stat events: %s (heap: %u)", (unsigned)count, url.c_str(), (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  // Body shape mirrors BookOrbit's own KOReader plugin: device fields at the top
  // level plus books[].hash and books[].events[].{page,startTime,durationSeconds,totalPages}.
  // pluginVersion: the server caps this field at 20 characters and rejects the whole
  // upload with HTTP 400 beyond that (verified against a live server by the samfoy
  // fork). Never build it from CROSSINK_VERSION — variant/branch builds overflow
  // (e.g. "crossink-1.4.0-xlarge" is 21 chars). Bump the numeric suffix when the
  // payload shape changes.
  // The JsonDocument is scoped so its node pool is freed before the TLS session
  // starts: only the serialized body stays alive through the handshake.
  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";
    char deviceTime[20] = {};
    bookOrbitFormatDatetime(static_cast<uint32_t>(time(nullptr)), deviceTime);
    doc["deviceTime"] = deviceTime;

    JsonArray books = doc["books"].to<JsonArray>();
    JsonObject book = books.add<JsonObject>();
    book["hash"] = documentHash;
    JsonArray jsonEvents = book["events"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) {
      JsonObject event = jsonEvents.add<JsonObject>();
      event["page"] = events[i].page;
      event["startTime"] = events[i].startTime;
      event["durationSeconds"] = events[i].durationSeconds;
      event["totalPages"] = events[i].totalPages;
    }
    if (!body.build(doc)) return LOW_MEMORY;
  }

  LOG_DBG("BookOrbit", "POST body bytes=%u", static_cast<unsigned>(body.length()));
  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_DBG("BookOrbit", "Upload stats response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::completeSweep(const std::string& deviceModel,
                                                              const uint32_t booksMatched,
                                                              const uint32_t pageStatsUploaded,
                                                              const uint32_t annotationsUpserted) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/sweeps";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Recording sweep: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  // See uploadPageStats for why pluginVersion is a literal and the JsonDocument is scoped.
  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";
    char deviceTime[20] = {};
    if (bookOrbitFormatDatetime(static_cast<uint32_t>(time(nullptr)), deviceTime)) {
      // Optional server-side, but an empty string fails its format validation with HTTP 400
      // and takes the whole sweep down with it: better omitted when the clock is implausible.
      doc["deviceTime"] = deviceTime;
    }
    doc["booksMatched"] = booksMatched;
    doc["pageStatsUploaded"] = pageStatsUploaded;
    doc["annotationsUpserted"] = annotationsUpserted;
    if (!body.build(doc)) return LOW_MEMORY;
  }

  LOG_DBG("BookOrbit", "POST body bytes=%u", static_cast<unsigned>(body.length()));
  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_DBG("BookOrbit", "Sweep response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::exchangeAnnotations(
    const std::string& documentHash, const std::string& deviceModel, const BookOrbitAnnotationKeys& keys,
    const BookOrbitAnnotation* changes, const size_t changeCount, bool& outUnmatched,
    std::vector<BookOrbitIncomingAnnotation>* outIncoming, bool* outMorePending) {
  lastHttpCode = 0;
  lastTransportError = 0;
  outUnmatched = false;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }
  // No early return for an empty payload. This request is the only thing that brings the
  // server's own highlight changes down, so skipping it when the device has nothing to say is
  // exactly how a device stops hearing about deletions and web-created highlights.

  const std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/annotations/exchange";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Exchanging %u annotations, %u keys (heap: %u)", (unsigned)changeCount, (unsigned)keys.count,
          (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  // Scoped so the node pool is freed before the TLS session opens: the key list alone can
  // reach a few hundred entries, and only the serialized body has to survive the handshake.
  // See uploadPageStats for why pluginVersion is a literal.
  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";

    JsonArray books = doc["books"].to<JsonArray>();
    JsonObject book = books.add<JsonObject>();
    book["hash"] = documentHash;
    book["keysComplete"] = keys.complete;
    // Objects, not bare digests: the server validates {k, dt} per key and rejects the whole
    // request with HTTP 400 otherwise.
    JsonArray jsonKeys = book["keys"].to<JsonArray>();
    for (size_t i = 0; keys.rows && i < keys.count; i++) {
      JsonObject key = jsonKeys.add<JsonObject>();
      key["k"] = keys.rows[i].k;
      key["dt"] = keys.rows[i].dt;
    }

    JsonArray jsonChanges = book["changes"].to<JsonArray>();
    for (size_t i = 0; changes && i < changeCount; i++) {
      const BookOrbitAnnotation& annotation = changes[i];
      JsonObject entry = jsonChanges.add<JsonObject>();
      entry["datetime"] = annotation.datetime;
      // Required: an entry without a drawer is a position-only bookmark to the server, and
      // bookmarks travel through their own endpoint. Clippings carry no style, so the
      // plainest highlight is the honest choice.
      entry["drawer"] = "lighten";
      entry["posFormat"] = "xpointer";
      entry["pos0"] = annotation.pos0;
      entry["pos1"] = annotation.pos1;
      entry["text"] = annotation.text;
      if (!annotation.chapter.empty()) entry["chapter"] = annotation.chapter;
      if (annotation.pageno > 0) entry["pageno"] = annotation.pageno;
    }
    if (!body.build(doc)) return LOW_MEMORY;
  }

  LOG_DBG("BookOrbit", "POST body bytes=%u", static_cast<unsigned>(body.length()));
  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  body.release();
  LOG_DBG("BookOrbit", "Annotation exchange response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode < 200 || httpCode >= 300) return SERVER_ERROR;

  // Filtered so the node pool only holds what is actually used: an exchange response can carry
  // far more per entry (colors, notes, styles) than a device that draws one kind of highlight
  // needs, and the pool competes with the socket that is still open.
  JsonDocument filter;
  filter["unmatched"] = true;
  filter["results"][0]["more"] = true;
  filter["results"][0]["skippedNoPosition"] = true;
  JsonObject entryFilter = filter["results"][0]["toApply"]["add"].add<JsonObject>();
  entryFilter["serverId"] = true;
  entryFilter["version"] = true;
  entryFilter["datetime"] = true;
  entryFilter["pos0"] = true;
  entryFilter["text"] = true;
  entryFilter["chapter"] = true;
  // A deletion names its target by key and carries neither position nor text.
  JsonObject deleteFilter = filter["results"][0]["toApply"]["delete"].add<JsonObject>();
  deleteFilter["serverId"] = true;
  deleteFilter["key"] = true;
  deleteFilter["datetime"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    LOG_ERR("BookOrbit", "Failed to parse annotation exchange response");
    return JSON_ERROR;
  }
  for (JsonVariantConst hash : doc["unmatched"].as<JsonArrayConst>()) {
    if (documentHash == hash.as<const char*>()) {
      LOG_INF("BookOrbit", "Server does not know this document; annotations not exchanged");
      outUnmatched = true;
      return OK;
    }
  }

  if (outMorePending) {
    // `more` says the push-down page was full; skippedNoPosition counts web highlights whose
    // positions the server converted this round and can only send on the NEXT request.
    *outMorePending = (doc["results"][0]["more"] | false) || (doc["results"][0]["skippedNoPosition"] | 0) > 0;
  }
  if (!outIncoming) return OK;

  JsonObjectConst toApply = doc["results"][0]["toApply"].as<JsonObjectConst>();
  const auto collect = [&](const char* field, const bool deleted) {
    for (JsonObjectConst entry : toApply[field].as<JsonArrayConst>()) {
      // Bounded on purpose: each entry carries its text, and the rest stays queued server-side
      // because nothing acknowledges it. The next sync brings it back.
      if (outIncoming->size() >= BOOKORBIT_ANNOTATION_BATCH) {
        LOG_INF("BookOrbit", "More server-side changes remain; they will arrive on the next sync");
        return;
      }
      const uint32_t serverId = entry["serverId"] | 0U;
      // The server re-offers everything not yet acknowledged, so a later pull round repeats the
      // earlier rounds' entries; appending them again would apply and ack each one twice.
      const bool alreadyCollected =
          std::any_of(outIncoming->begin(), outIncoming->end(),
                      [&](const BookOrbitIncomingAnnotation& seen) { return seen.serverId == serverId; });
      if (alreadyCollected) continue;
      const char* datetime = entry["datetime"] | "";
      const char* pos0 = entry["pos0"] | "";
      const char* key = entry["key"] | "";
      // Each kind carries what it needs and nothing more: a deletion has a key, an add has a
      // position. Demanding a position of both is what dropped every deletion on the floor.
      if (serverId == 0 || (deleted ? !*key : !*pos0)) continue;

      BookOrbitIncomingAnnotation incoming;
      incoming.serverId = serverId;
      incoming.version = deleted ? 0U : (entry["version"] | 0U);
      snprintf(incoming.datetime, sizeof(incoming.datetime), "%s", datetime);
      incoming.key = key;
      incoming.pos0 = pos0;
      incoming.text = entry["text"] | "";
      if (incoming.text.size() > BOOKORBIT_ANNOTATION_TEXT_MAX) incoming.text.resize(BOOKORBIT_ANNOTATION_TEXT_MAX);
      incoming.chapter = entry["chapter"] | "";
      incoming.deleted = deleted;
      outIncoming->push_back(std::move(incoming));
    }
  };
  collect("add", false);
  collect("delete", true);
  if (!outIncoming->empty()) {
    LOG_INF("BookOrbit", "Server sent %u annotation change(s)", (unsigned)outIncoming->size());
  }
  return OK;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::ackAnnotations(const std::string& documentHash,
                                                               const std::string& deviceModel,
                                                               const std::vector<BookOrbitAckEntry>& applied,
                                                               const std::vector<BookOrbitAckEntry>& deleted) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) return NO_CREDENTIALS;
  if (applied.empty() && deleted.empty()) return OK;

  const std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/annotations/exchange-ack";
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";
    JsonArray books = doc["books"].to<JsonArray>();
    JsonObject book = books.add<JsonObject>();
    book["hash"] = documentHash;
    JsonArray appliedArray = book["applied"].to<JsonArray>();
    for (const BookOrbitAckEntry& ack : applied) {
      JsonObject entry = appliedArray.add<JsonObject>();
      entry["serverId"] = ack.serverId;
      entry["version"] = ack.version;
      entry["status"] = "applied";
    }
    JsonArray deletedArray = book["deleted"].to<JsonArray>();
    for (const BookOrbitAckEntry& ack : deleted) {
      JsonObject entry = deletedArray.add<JsonObject>();
      entry["serverId"] = ack.serverId;
      // No version here: the deletion ack schema does not define the field.
      entry["status"] = "applied";
    }
    if (!body.build(doc)) return LOW_MEMORY;
  }

  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_INF("BookOrbit", "Annotation ack response: %d (%u applied, %u deleted)", httpCode, (unsigned)applied.size(),
          (unsigned)deleted.size());

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode >= 200 && httpCode < 300) return OK;
  return SERVER_ERROR;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::exchangeBookmarks(
    const std::string& documentHash, const std::string& deviceModel, const BookOrbitAnnotationKeys& keys,
    const BookOrbitBookmark* changes, const size_t changeCount, bool& outUnmatched,
    std::vector<BookOrbitIncomingBookmark>* outIncoming, bool* outMorePending) {
  lastHttpCode = 0;
  lastTransportError = 0;
  outUnmatched = false;
  if (!BOOKORBIT_STORE.hasCredentials()) {
    LOG_DBG("BookOrbit", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/bookmarks/exchange";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("BookOrbit", "Exchanging %u bookmarks, %u keys (heap: %u)", (unsigned)changeCount, (unsigned)keys.count,
          (unsigned)freeHeap);
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";

    JsonArray books = doc["books"].to<JsonArray>();
    JsonObject book = books.add<JsonObject>();
    book["hash"] = documentHash;
    book["keysComplete"] = keys.complete;
    JsonArray jsonKeys = book["keys"].to<JsonArray>();
    for (size_t i = 0; keys.rows && i < keys.count; i++) {
      JsonObject key = jsonKeys.add<JsonObject>();
      key["k"] = keys.rows[i].k;
      key["dt"] = keys.rows[i].dt;
    }

    JsonArray jsonChanges = book["changes"].to<JsonArray>();
    for (size_t i = 0; changes && i < changeCount; i++) {
      const BookOrbitBookmark& bookmark = changes[i];
      JsonObject entry = jsonChanges.add<JsonObject>();
      entry["datetime"] = bookmark.datetime;
      entry["pos"] = bookmark.pos;
      if (!bookmark.chapter.empty()) entry["chapter"] = bookmark.chapter;
      if (bookmark.pageno > 0) entry["pageno"] = bookmark.pageno;
    }
    if (!body.build(doc)) return LOW_MEMORY;
  }

  LOG_DBG("BookOrbit", "POST body bytes=%u", static_cast<unsigned>(body.length()));
  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  body.release();
  LOG_DBG("BookOrbit", "Bookmark exchange response: %d", httpCode);

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode < 200 || httpCode >= 300) return SERVER_ERROR;

  JsonDocument filter;
  filter["unmatched"] = true;
  filter["results"][0]["more"] = true;
  filter["results"][0]["skippedNoPosition"] = true;
  JsonObject addFilter = filter["results"][0]["toApply"]["add"].add<JsonObject>();
  addFilter["serverId"] = true;
  addFilter["pos"] = true;
  addFilter["title"] = true;
  addFilter["pageno"] = true;
  JsonObject deleteFilter = filter["results"][0]["toApply"]["delete"].add<JsonObject>();
  deleteFilter["serverId"] = true;
  deleteFilter["key"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    LOG_ERR("BookOrbit", "Failed to parse bookmark exchange response");
    return JSON_ERROR;
  }
  for (JsonVariantConst hash : doc["unmatched"].as<JsonArrayConst>()) {
    if (documentHash == hash.as<const char*>()) {
      LOG_INF("BookOrbit", "Server does not know this document; bookmarks not exchanged");
      outUnmatched = true;
      return OK;
    }
  }
  if (outMorePending) {
    *outMorePending = (doc["results"][0]["more"] | false) || (doc["results"][0]["skippedNoPosition"] | 0) > 0;
  }
  if (!outIncoming) return OK;

  JsonObjectConst toApply = doc["results"][0]["toApply"].as<JsonObjectConst>();
  const auto collect = [&](const char* field, const bool deleted) {
    for (JsonObjectConst entry : toApply[field].as<JsonArrayConst>()) {
      if (outIncoming->size() >= BOOKORBIT_BOOKMARK_BATCH) {
        LOG_INF("BookOrbit", "More server-side bookmark changes remain; they arrive next sync");
        return;
      }
      const uint32_t serverId = entry["serverId"] | 0U;
      // Later pull rounds repeat entries not yet acknowledged.
      const bool alreadyCollected =
          std::any_of(outIncoming->begin(), outIncoming->end(),
                      [&](const BookOrbitIncomingBookmark& seen) { return seen.serverId == serverId; });
      if (alreadyCollected) continue;
      const char* pos = entry["pos"] | "";
      const char* key = entry["key"] | "";
      if (serverId == 0 || (deleted ? !*key : !*pos)) continue;

      BookOrbitIncomingBookmark incoming;
      incoming.serverId = serverId;
      incoming.key = key;
      incoming.pos = pos;
      incoming.title = entry["title"] | "";
      incoming.pageno = entry["pageno"] | 0;
      incoming.deleted = deleted;
      outIncoming->push_back(std::move(incoming));
    }
  };
  collect("add", false);
  collect("delete", true);
  if (!outIncoming->empty()) {
    LOG_INF("BookOrbit", "Server sent %u bookmark change(s)", (unsigned)outIncoming->size());
  }
  return OK;
}

BookOrbitSyncClient::Error BookOrbitSyncClient::ackBookmarks(const std::string& documentHash,
                                                             const std::string& deviceModel,
                                                             const std::vector<BookOrbitBookmarkAck>& applied,
                                                             const std::vector<uint32_t>& deletedServerIds) {
  lastHttpCode = 0;
  lastTransportError = 0;
  if (!BOOKORBIT_STORE.hasCredentials()) return NO_CREDENTIALS;
  if (applied.empty() && deletedServerIds.empty()) return OK;

  const std::string url = BOOKORBIT_STORE.getBaseUrl() + "/plugin/bookmarks/exchange-ack";
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t heapFloor = requiredHeapFloor();
  if (freeHeap < heapFloor) {
    LOG_ERR("BookOrbit", "Insufficient heap for sync request: %u bytes free (need %u)", freeHeap, heapFloor);
    return LOW_MEMORY;
  }

  JsonBody body;
  {
    JsonDocument doc;
    doc["deviceId"] = deviceId();
    doc["deviceModel"] = deviceModel;
    doc["pluginVersion"] = "crossink-bo-1";
    JsonArray books = doc["books"].to<JsonArray>();
    JsonObject book = books.add<JsonObject>();
    book["hash"] = documentHash;
    JsonArray appliedArray = book["applied"].to<JsonArray>();
    for (const BookOrbitBookmarkAck& ack : applied) {
      JsonObject entry = appliedArray.add<JsonObject>();
      entry["serverId"] = ack.serverId;
      entry["status"] = ack.failed ? "failed" : "applied";
      // The local identity this device minted; it is what links the server's bookmark here.
      if (!ack.failed && ack.key[0] != '\0') {
        entry["key"] = ack.key;
        entry["datetime"] = ack.datetime;
        entry["pos"] = ack.pos;
      }
    }
    JsonArray deletedArray = book["deleted"].to<JsonArray>();
    for (const uint32_t serverId : deletedServerIds) {
      JsonObject entry = deletedArray.add<JsonObject>();
      entry["serverId"] = serverId;
      entry["status"] = "applied";
    }
    if (!body.build(doc)) return LOW_MEMORY;
  }

  std::string response;
  const int httpCode = sendBookOrbitRequest("POST", url, &body, response);
  lastHttpCode = httpCode > 0 ? httpCode : 0;
  lastTransportError = httpCode < 0 ? httpCode : 0;
  LOG_INF("BookOrbit", "Bookmark ack response: %d (%u applied, %u deleted)", httpCode, (unsigned)applied.size(),
          (unsigned)deletedServerIds.size());

  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode >= 200 && httpCode < 300) return OK;
  return SERVER_ERROR;
}

std::string BookOrbitSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return tr(STR_BOOKORBIT_SETUP_HINT);
    case NETWORK_ERROR:
      return networkErrorMessage();
    case AUTH_FAILED:
      return tr(STR_KOREADER_SYNC_AUTH_REJECTED);
    case SERVER_ERROR:
      if (lastHttpCode == 404) return tr(STR_BOOKORBIT_SYNC_HTTP_404);
      if (lastHttpCode > 0) return formatHttpStatusMessage(lastHttpCode);
      return tr(STR_KOREADER_SYNC_SERVER_ERROR);
    case JSON_ERROR:
      return tr(STR_KOREADER_SYNC_BAD_RESPONSE);
    case NOT_FOUND:
      return tr(STR_NO_REMOTE_MSG);
    case INVALID_AUTH_RESPONSE:
      return tr(STR_KOREADER_SYNC_BAD_RESPONSE);
    case LOW_MEMORY:
      return tr(STR_KOREADER_SYNC_LOW_MEMORY);
    default:
      return tr(STR_UNKNOWN_ERROR);
  }
}
