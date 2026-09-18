#include "BookOrbitCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

namespace {
// BookOrbit's kosync-compatible progress endpoints live under this path prefix
// (e.g. GET {server}/api/v1/koreader/users/auth), unlike a plain koreader-sync
// server which serves them at the root.
constexpr char API_PREFIX[] = "/api/v1/koreader";
}  // namespace

bool BookOrbitCredentialStore::saveToFile() const {
  JsonDocument doc;
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);
  doc["serverUrl"] = serverUrl;
  doc["downloadFolder"] = downloadFolder;
  doc["syncBehavior"] = static_cast<uint8_t>(syncBehavior);

  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  if (!Storage.writeFile(getFilePath(), json)) {
    LOG_ERR("BOS", "Failed to save BookOrbit settings");
    return false;
  }
  return true;
}

bool BookOrbitCredentialStore::loadFromFile() {
  loadAttempted = true;
  if (!Storage.exists(getFilePath())) {
    LOG_DBG("BOS", "No BookOrbit settings file");
    return false;
  }
  const String json = Storage.readFile(getFilePath());
  JsonDocument doc;
  if (json.isEmpty() || deserializeJson(doc, json.c_str())) {
    LOG_ERR("BOS", "Unreadable BookOrbit settings file");
    return false;
  }

  const char* user = doc["username"] | "";
  bool ok = true;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok && !pass.empty()) {
    LOG_ERR("BOS", "Ignoring unreadable BookOrbit password");
    pass.clear();
  }

  setCredentials(user, pass);
  setServerUrl(doc["serverUrl"] | "");
  downloadFolder = doc["downloadFolder"] | "";
  const uint8_t behavior = doc["syncBehavior"] | static_cast<uint8_t>(0);
  syncBehavior = behavior == static_cast<uint8_t>(BookOrbitSyncBehavior::SMART) ? BookOrbitSyncBehavior::SMART
                                                                                : BookOrbitSyncBehavior::ASK_EVERY_TIME;
  return true;
}

void BookOrbitCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  username = user;
  password = pass;
  LOG_DBG("BOS", "Set credentials for user: %s", user.c_str());
}

std::string BookOrbitCredentialStore::getMd5Password() const {
  if (password.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool BookOrbitCredentialStore::hasCredentials() const {
  return !username.empty() && !password.empty() && !serverUrl.empty();
}

void BookOrbitCredentialStore::clearCredentials() {
  username.clear();
  password.clear();
  saveToFile();
  LOG_DBG("BOS", "Cleared BookOrbit credentials");
}

void BookOrbitCredentialStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("BOS", "Set server URL: %s", url.empty() ? "(none)" : url.c_str());
}

std::string BookOrbitCredentialStore::getBaseUrl() const {
  if (serverUrl.empty()) {
    return "";
  }

  std::string url;
  if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add https:// if no protocol specified (BookOrbit servers are self-hosted
    // and typically run behind TLS; unlike KOReaderCredentialStore we don't default to http://
    // since there's no well-known plaintext deployment convention to fall back to).
    url = "https://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  // Strip a trailing "/api/v1" or "/api/v1/koreader" the user may have pasted from
  // BookOrbit's own settings page, then append our own canonical prefix.
  constexpr char API_V1_SUFFIX[] = "/api/v1";
  if (url.size() >= sizeof(API_PREFIX) - 1 &&
      url.compare(url.size() - (sizeof(API_PREFIX) - 1), sizeof(API_PREFIX) - 1, API_PREFIX) == 0) {
    url.resize(url.size() - (sizeof(API_PREFIX) - 1));
  } else if (url.size() >= sizeof(API_V1_SUFFIX) - 1 &&
             url.compare(url.size() - (sizeof(API_V1_SUFFIX) - 1), sizeof(API_V1_SUFFIX) - 1, API_V1_SUFFIX) == 0) {
    url.resize(url.size() - (sizeof(API_V1_SUFFIX) - 1));
  }

  return url + API_PREFIX;
}
