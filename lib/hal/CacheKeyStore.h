#pragma once

// Device-side plumbing behind CacheKeys — docs/protected-content-plan.md §3, step 3.
//
// Supplies the two secrets the derivation needs:
//
//   * the **device key**, 32 random bytes generated once and kept in NVS. NVS is internal
//     flash, so it never leaves with the card: lift the SD out of a device and the enciphered
//     caches on it are noise, on any machine including another reader.
//   * the **per-cache-directory salt**, 12 random bytes written into the directory itself when
//     it is first used. It costs nothing to lose — deleting a cache regenerates it — and it is
//     what makes a rebuilt cache differ from the one it replaced.
//
// Device-only (NVS + SD); the pure derivation it feeds lives in CacheKeys.h and is what the
// host tests cover.

#include <cstdint>

#include "CacheKeys.h"

namespace cachekeys {

// Reads the device key, generating and persisting one on first use.
// False means NVS is unavailable or the write failed — the caller must then refuse to cache a
// protected book rather than fall back to writing it in the clear.
bool loadDeviceKey(uint8_t out[kDeviceKeyBytes]);

// The book key for one cache directory, reading or creating that directory's salt file.
// `cacheDir` is an absolute path on the card ("/.crosspoint/epub_12345678").
bool bookKeyForCacheDir(const char* cacheDir, uint8_t out[kBookKeyBytes]);

// Name of the salt file inside a cache directory. Dot-prefixed so the web file manager will
// not serve it: harmless if it leaks (a salt is not a secret), but there is no reason to hand
// it out either.
inline constexpr const char* kSaltFileName = ".csalt";

}  // namespace cachekeys
