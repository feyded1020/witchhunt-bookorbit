#pragma once
// Host stub for lib/hal/CacheKeyStore.h.
//
// The real one reads a device key out of NVS and a salt off the SD card, neither of which
// exists on the host. Epub.cpp includes it to encipher a protected book's cache, so the stub
// only has to answer "no key here": the constants and the derivation itself come from the real
// CacheKeys.h, which host tests link and test/cache_cipher pins.
//
// Reporting failure rather than handing back a fake key is deliberate — a test that somehow
// took the protected path fails the open loudly instead of quietly caching in the clear.

#include "CacheKeys.h"

namespace cachekeys {

inline bool loadDeviceKey(uint8_t[kDeviceKeyBytes]) { return false; }
inline bool bookKeyForCacheDir(const char*, uint8_t[kBookKeyBytes]) { return false; }

}  // namespace cachekeys
