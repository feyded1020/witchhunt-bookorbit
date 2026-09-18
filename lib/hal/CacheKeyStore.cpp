#include "CacheKeyStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_system.h>

#include <cstring>
#include <string>

namespace {

// NVS namespace and key. Same pattern as HalClock's persisted epoch: a short-lived
// Preferences handle opened around the access, not a long-lived one.
constexpr const char* kNvsNamespace = "cachecipher";
constexpr const char* kNvsDeviceKey = "devkey";

}  // namespace

namespace cachekeys {

bool loadDeviceKey(uint8_t out[kDeviceKeyBytes]) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    LOG_ERR("CKEY", "NVS namespace unavailable - cannot supply a cache key");
    return false;
  }

  const size_t stored = prefs.getBytesLength(kNvsDeviceKey);
  if (stored == kDeviceKeyBytes && prefs.getBytes(kNvsDeviceKey, out, kDeviceKeyBytes) == kDeviceKeyBytes) {
    prefs.end();
    return true;
  }

  // First use on this device, or a truncated entry. Generating a fresh key orphans any cache
  // enciphered under an old one, which is harmless: those files decipher to noise, fail their
  // header checks, and get rebuilt.
  esp_fill_random(out, kDeviceKeyBytes);
  const size_t written = prefs.putBytes(kNvsDeviceKey, out, kDeviceKeyBytes);
  prefs.end();

  if (written != kDeviceKeyBytes) {
    LOG_ERR("CKEY", "Failed to persist device key (wrote %u of %u)", static_cast<unsigned>(written),
            static_cast<unsigned>(kDeviceKeyBytes));
    memset(out, 0, kDeviceKeyBytes);
    return false;
  }
  LOG_INF("CKEY", "Generated a new cache device key");
  return true;
}

bool bookKeyForCacheDir(const char* cacheDir, uint8_t out[kBookKeyBytes]) {
  if (cacheDir == nullptr || *cacheDir == '\0') return false;

  uint8_t deviceKey[kDeviceKeyBytes];
  if (!loadDeviceKey(deviceKey)) return false;

  const std::string saltPath = std::string(cacheDir) + "/" + kSaltFileName;
  uint8_t salt[kSaltBytes];
  bool haveSalt = false;

  if (HalFile existing = Storage.open(saltPath.c_str(), O_RDONLY)) {
    haveSalt = existing.read(salt, kSaltBytes) == static_cast<int>(kSaltBytes);
    if (!haveSalt) {
      LOG_ERR("CKEY", "Salt file %s is short - regenerating", saltPath.c_str());
    }
  }

  if (!haveSalt) {
    esp_fill_random(salt, kSaltBytes);
    // The cache directory may not exist yet on a first open; mkdir is recursive.
    Storage.mkdir(cacheDir);
    HalFile file;
    if (!Storage.openFileForWrite("CKEY", saltPath, file) ||
        file.write(salt, kSaltBytes) != static_cast<size_t>(kSaltBytes)) {
      LOG_ERR("CKEY", "Failed to write %s - refusing to cache this book", saltPath.c_str());
      memset(deviceKey, 0, sizeof(deviceKey));
      return false;
    }
  }

  deriveBookKey(deviceKey, salt, out);
  // The device key has no business staying on the stack after this.
  memset(deviceKey, 0, sizeof(deviceKey));
  return true;
}

}  // namespace cachekeys
