#include "FontPreviewCache.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* MODULE = "FPRVC";
constexpr const char* CACHE_DIR = "/.crosspoint/fontprev";

constexpr char MAGIC[4] = {'C', 'P', 'F', 'P'};
// Bumped to 2 when the sample stopped being drawn in the theme's title weight
// and became plain regular. The key describes the font and the rectangle, not
// how the sample is drawn, so a change to the drawing has to invalidate every
// stored strip through this field -- otherwise cards keep serving strips in the
// old weight next to freshly rendered ones. Bump it for any future change to
// what the preview draws.
constexpr uint16_t FORMAT_VERSION = 2;

// magic[4] | version u16 | width u16 | height u16 | pointSize u8 | language u8 |
// sourceSize u32 | reserved u32
constexpr size_t HEADER_BYTES = 20;

// One packed 1bpp row. The widest panel this firmware drives is 960 px, so 128
// bytes covers any strip and the buffer stays on the stack — the whole reason
// for streaming row by row instead of materialising the strip.
constexpr size_t MAX_ROW_BYTES = 128;

// Family names come from SD directory names, so they are already FAT-legal;
// this only bounds the length, matching the 32-byte field used elsewhere.
constexpr int MAX_FAMILY_CHARS = 31;

size_t rowBytesFor(const uint16_t width) { return (static_cast<size_t>(width) + 7) / 8; }

void buildPath(const FontPreviewCache::Key& key, char* out, const size_t outSize) {
  snprintf(out, outSize, "%s/%.*s_%u_%ux%u_%u.fpv", CACHE_DIR, MAX_FAMILY_CHARS, key.familyName,
           static_cast<unsigned>(key.pointSize), static_cast<unsigned>(key.width), static_cast<unsigned>(key.height),
           static_cast<unsigned>(key.language));
}

// memcpy rather than a struct cast: the ESP32-C3 faults on unaligned wide loads
// and a byte buffer carries no alignment guarantee.
void writeU16(uint8_t* dst, const uint16_t value) { memcpy(dst, &value, sizeof(value)); }
void writeU32(uint8_t* dst, const uint32_t value) { memcpy(dst, &value, sizeof(value)); }
uint16_t readU16(const uint8_t* src) {
  uint16_t value;
  memcpy(&value, src, sizeof(value));
  return value;
}
uint32_t readU32(const uint8_t* src) {
  uint32_t value;
  memcpy(&value, src, sizeof(value));
  return value;
}

void fillHeader(uint8_t* header, const FontPreviewCache::Key& key) {
  memset(header, 0, HEADER_BYTES);
  memcpy(header, MAGIC, sizeof(MAGIC));
  writeU16(header + 4, FORMAT_VERSION);
  writeU16(header + 6, key.width);
  writeU16(header + 8, key.height);
  header[10] = key.pointSize;
  header[11] = key.language;
  writeU32(header + 12, key.sourceSize);
}

bool headerMatches(const uint8_t* header, const FontPreviewCache::Key& key) {
  return memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && readU16(header + 4) == FORMAT_VERSION &&
         readU16(header + 6) == key.width && readU16(header + 8) == key.height && header[10] == key.pointSize &&
         header[11] == key.language && readU32(header + 12) == key.sourceSize;
}

// A key is only usable if it names a family, has a drawable rectangle, and
// carries a source size — a zero source size means the .cpfont could not be
// measured, and an entry that cannot be invalidated is worse than no entry.
bool keyUsable(const FontPreviewCache::Key& key) {
  return key.familyName && key.familyName[0] != '\0' && key.width > 0 && key.height > 0 && key.sourceSize != 0 &&
         rowBytesFor(key.width) <= MAX_ROW_BYTES;
}

size_t expectedFileSize(const FontPreviewCache::Key& key) {
  return HEADER_BYTES + rowBytesFor(key.width) * static_cast<size_t>(key.height);
}

}  // namespace

bool FontPreviewCache::available(const Key& key) {
  if (!keyUsable(key)) return false;

  char path[112];
  buildPath(key, path, sizeof(path));

  // A missing entry is the normal cold-cache state, not a fault. Going straight
  // to openFileForRead() made it log an error per family per probe, so a first
  // run looked like six I/O failures.
  if (!Storage.exists(path)) return false;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file) || !file) return false;

  uint8_t header[HEADER_BYTES];
  const bool ok = file.read(header, HEADER_BYTES) == static_cast<int>(HEADER_BYTES) && headerMatches(header, key) &&
                  file.fileSize() == expectedFileSize(key);
  file.close();
  return ok;
}

bool FontPreviewCache::restore(const GfxRenderer& renderer, const Key& key, const int x, const int y) {
  if (!keyUsable(key)) return false;

  char path[112];
  buildPath(key, path, sizeof(path));

  if (!Storage.exists(path)) return false;  // see the note in available()

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file) || !file) return false;

  uint8_t header[HEADER_BYTES];
  if (file.read(header, HEADER_BYTES) != static_cast<int>(HEADER_BYTES) || !headerMatches(header, key) ||
      file.fileSize() != expectedFileSize(key)) {
    file.close();
    return false;
  }

  const size_t rowBytes = rowBytesFor(key.width);
  uint8_t row[MAX_ROW_BYTES];
  for (uint16_t r = 0; r < key.height; r++) {
    if (file.read(row, rowBytes) != static_cast<int>(rowBytes)) {
      LOG_ERR(MODULE, "Short read at row %u of %s", static_cast<unsigned>(r), path);
      file.close();
      return false;
    }
    renderer.writeFramebufferRegion(x, y + r, key.width, 1, row);
  }
  file.close();
  return true;
}

bool FontPreviewCache::store(const GfxRenderer& renderer, const Key& key, const int x, const int y) {
  if (!keyUsable(key)) return false;

  // mkdir() fails when the directory is already there, so it must only be called
  // when it is not -- the guard the rest of the codebase uses. Without the
  // exists() check every store after the first one failed silently and nothing
  // was ever cached.
  if (!Storage.exists(CACHE_DIR) && !Storage.mkdir(CACHE_DIR)) {
    LOG_ERR(MODULE, "Cannot create %s", CACHE_DIR);
    return false;
  }

  char path[112];
  buildPath(key, path, sizeof(path));

  HalFile file;
  if (!Storage.openFileForWrite(MODULE, path, file) || !file) return false;

  uint8_t header[HEADER_BYTES];
  fillHeader(header, key);

  const size_t rowBytes = rowBytesFor(key.width);
  bool ok = file.write(header, HEADER_BYTES) == HEADER_BYTES;
  uint8_t row[MAX_ROW_BYTES];
  for (uint16_t r = 0; ok && r < key.height; r++) {
    ok = renderer.readFramebufferRegion(x, y + r, key.width, 1, row, sizeof(row)) == rowBytes &&
         file.write(row, rowBytes) == rowBytes;
  }
  file.close();

  if (!ok) {
    // A truncated entry would be rejected on the next read anyway, but leaving
    // it on the card wastes space and hides the failure.
    LOG_ERR(MODULE, "Write failed for %s; discarding", path);
    Storage.remove(path);
    return false;
  }

  LOG_DBG(MODULE, "Stored preview %s (%ux%u)", path, static_cast<unsigned>(key.width),
          static_cast<unsigned>(key.height));
  return true;
}

void FontPreviewCache::clear() {
  if (!Storage.exists(CACHE_DIR)) return;
  if (!Storage.removeDir(CACHE_DIR)) LOG_ERR(MODULE, "Failed to remove %s", CACHE_DIR);
}
