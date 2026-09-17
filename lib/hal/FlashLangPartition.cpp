#include "FlashLangPartition.h"

#include <InflateReader.h>
#include <Logging.h>
#include <esp_partition.h>

#include <cstring>
#include <memory>

#include "FlashFontPartition.h"

namespace FlashLangPartition {

static constexpr char MAGIC[4] = {'C', 'P', 'L', 'P'};
static constexpr size_t WRITE_CHUNK = 512;

static esp_partition_mmap_handle_t s_mmapHandle = 0;
static const uint8_t* s_mmapPtr = nullptr;

// On-flash header, read and written as a unit. Packed layout is fixed by the
// comment in the header file; the struct mirrors it so the offsets stay in one
// place rather than being recomputed at each access.
struct Header {
  char magic[4];
  uint8_t version;
  uint8_t langIndex;
  uint16_t keyCount;
  uint32_t stamp;
  uint32_t tableOff;
  uint32_t dataOff;
  uint32_t dataLen;
  uint8_t reserved[8];
};
static_assert(sizeof(Header) == HEADER_BYTES, "language slot header layout changed");

static const esp_partition_t* findPartition() {
  const esp_partition_t* part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!part) LOG_ERR("FLP", "spiffs partition not found");
  return part;
}

// Byte offset of the language slot within the partition. Equal to
// FlashFontPartition's ceiling, and 64 KB aligned because the partition both
// starts on and is a multiple of 64 KB — esp_partition_mmap requires that.
static size_t slotOffset(const esp_partition_t* part) {
  if (!part || part->size <= FlashFontPartition::LANG_RESERVED_BYTES) return 0;
  return static_cast<size_t>(part->size) - FlashFontPartition::LANG_RESERVED_BYTES;
}

static bool readHeader(const esp_partition_t* part, Header& out) {
  const size_t off = slotOffset(part);
  if (off == 0) return false;
  if (esp_partition_read(part, off, &out, sizeof(Header)) != ESP_OK) return false;
  if (memcmp(out.magic, MAGIC, 4) != 0) return false;
  if (out.version != FORMAT_VERSION) return false;
  return true;
}

bool holds(const uint8_t langIndex, const uint32_t stamp) {
  const esp_partition_t* part = findPartition();
  if (!part) return false;
  Header h;
  if (!readHeader(part, h)) return false;
  return h.langIndex == langIndex && h.stamp == stamp;
}

// Walks the decompressed blob one byte at a time, filling the offset table.
//
// Kept as explicit state rather than scanning the finished blob because the
// blob is streamed to flash in chunks and never exists in RAM as a whole. The
// state lives in members, so an entry straddling a chunk boundary needs no
// special case at all.
class TableBuilder {
 public:
  TableBuilder(uint16_t* table, uint16_t keyCount, const uint16_t* enOffsets)
      : table_(table), keyCount_(keyCount), enOffsets_(enOffsets) {}

  void feed(const uint8_t* bytes, const size_t len) {
    for (size_t i = 0; i < len; i++) {
      if (atEntryStart_ && entry_ < keyCount_) {
        // An empty entry (immediate NUL) means "no override" — point the table
        // at English instead, exactly as the generator does for in-image
        // languages, so I18n::get() needs no second kind of fallback.
        table_[entry_] =
            (bytes[i] == 0) ? static_cast<uint16_t>(0x8000u | enOffsets_[entry_]) : static_cast<uint16_t>(pos_);
        atEntryStart_ = false;
      }
      if (bytes[i] == 0) {
        entry_++;
        atEntryStart_ = true;
      }
      pos_++;
    }
  }

  bool complete() const { return entry_ == keyCount_; }
  uint16_t entriesSeen() const { return entry_; }

 private:
  uint16_t* table_;
  uint16_t keyCount_;
  const uint16_t* enOffsets_;
  uint32_t pos_ = 0;
  uint16_t entry_ = 0;
  bool atEntryStart_ = true;
};

bool build(const uint8_t langIndex, const uint32_t stamp, const uint8_t* compressed, const size_t compressedLen,
           const size_t uncompressedLen, const uint16_t keyCount, const uint16_t* enOffsets) {
  if (!compressed || compressedLen == 0 || keyCount == 0 || !enOffsets) return false;

  // TableBuilder narrows blob positions to uint16_t, and bit 15 is the English
  // fallback flag, so a blob at or past 32 KB would alias offsets onto that
  // flag. gen_i18n.py refuses to emit one (MAX_BLOB_BYTES) — this is the
  // matching check on the consuming side, so the two cannot drift apart
  // silently if the generator is ever changed.
  if (uncompressedLen >= 0x8000u) {
    LOG_ERR("FLP", "language %u blob is %u B, past the 15-bit offset limit", langIndex,
            static_cast<unsigned>(uncompressedLen));
    return false;
  }

  const esp_partition_t* part = findPartition();
  if (!part) return false;
  const size_t slot = slotOffset(part);
  if (slot == 0) {
    LOG_ERR("FLP", "partition too small for a language slot");
    return false;
  }

  // A live mapping would go stale under the erase below.
  unmap();

  // 4-byte align the table length so the data that follows starts aligned too,
  // matching FlashFontPartition's own convention for flash writes. An odd key
  // count would otherwise leave every data write 2-byte aligned.
  const size_t tableOff = HEADER_BYTES;
  const size_t tableBytes = (static_cast<size_t>(keyCount) * sizeof(uint16_t) + 3u) & ~static_cast<size_t>(3u);
  const size_t tableWords = tableBytes / sizeof(uint16_t);
  const size_t dataOff = tableOff + tableBytes;
  if (dataOff + uncompressedLen > FlashFontPartition::LANG_RESERVED_BYTES) {
    LOG_ERR("FLP", "language %u needs %u B, slot is %u B", langIndex, static_cast<unsigned>(dataOff + uncompressedLen),
            static_cast<unsigned>(FlashFontPartition::LANG_RESERVED_BYTES));
    return false;
  }

  if (esp_partition_erase_range(part, slot, FlashFontPartition::LANG_RESERVED_BYTES) != ESP_OK) {
    LOG_ERR("FLP", "erase failed");
    return false;
  }

  // All three buffers are transient — freed before this function returns.
  auto ring = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[INFLATE_RING_BYTES]);
  auto chunk = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[WRITE_CHUNK]);
  // Value-initialised: the alignment pad past keyCount must be written as a
  // known value, not whatever the heap held.
  auto table = std::unique_ptr<uint16_t[]>(new (std::nothrow) uint16_t[tableWords]());
  if (!ring || !chunk || !table) {
    LOG_ERR("FLP", "out of memory building language %u", langIndex);
    return false;
  }

  InflateReader inflate;
  if (!inflate.initWithExternalRing(ring.get(), INFLATE_RING_BYTES)) {
    LOG_ERR("FLP", "inflate init failed");
    return false;
  }
  inflate.setSource(compressed, compressedLen);

  TableBuilder builder(table.get(), keyCount, enOffsets);
  size_t written = 0;
  bool done = false;
  while (!done) {
    size_t produced = 0;
    const InflateStatus st = inflate.readAtMost(chunk.get(), WRITE_CHUNK, &produced);
    if (st == InflateStatus::Error) {
      LOG_ERR("FLP", "inflate failed at %u B", static_cast<unsigned>(written));
      return false;
    }
    if (produced > 0) {
      if (written + produced > uncompressedLen) {
        LOG_ERR("FLP", "stream longer than declared (%u B)", static_cast<unsigned>(uncompressedLen));
        return false;
      }
      if (esp_partition_write(part, slot + dataOff + written, chunk.get(), produced) != ESP_OK) {
        LOG_ERR("FLP", "write failed at %u B", static_cast<unsigned>(written));
        return false;
      }
      builder.feed(chunk.get(), produced);
      written += produced;
    }
    done = (st == InflateStatus::Done);
    if (!done && produced == 0) {
      LOG_ERR("FLP", "inflate stalled at %u B", static_cast<unsigned>(written));
      return false;
    }
  }

  if (written != uncompressedLen || !builder.complete()) {
    LOG_ERR("FLP", "blob mismatch: %u/%u B, %u/%u entries", static_cast<unsigned>(written),
            static_cast<unsigned>(uncompressedLen), builder.entriesSeen(), keyCount);
    return false;
  }

  if (esp_partition_write(part, slot + tableOff, table.get(), tableBytes) != ESP_OK) {
    LOG_ERR("FLP", "table write failed");
    return false;
  }

  // Header last: until this lands the slot reads as absent.
  Header h = {};
  memcpy(h.magic, MAGIC, 4);
  h.version = FORMAT_VERSION;
  h.langIndex = langIndex;
  h.keyCount = keyCount;
  h.stamp = stamp;
  h.tableOff = static_cast<uint32_t>(tableOff);
  h.dataOff = static_cast<uint32_t>(dataOff);
  h.dataLen = static_cast<uint32_t>(written);
  if (esp_partition_write(part, slot, &h, sizeof(h)) != ESP_OK) {
    LOG_ERR("FLP", "header write failed");
    return false;
  }

  LOG_INF("FLP", "language %u built: %u B compressed -> %u B, %u entries", langIndex,
          static_cast<unsigned>(compressedLen), static_cast<unsigned>(written), keyCount);
  return true;
}

bool map(const uint8_t langIndex, const uint32_t stamp, const uint16_t keyCount, Mapped* out) {
  if (!out) return false;

  const esp_partition_t* part = findPartition();
  if (!part) return false;
  const size_t slot = slotOffset(part);
  if (slot == 0) return false;

  Header h;
  if (!readHeader(part, h)) return false;
  if (h.langIndex != langIndex || h.stamp != stamp || h.keyCount != keyCount) return false;
  if (h.dataOff + h.dataLen > FlashFontPartition::LANG_RESERVED_BYTES) return false;

  if (!s_mmapPtr) {
    const void* raw = nullptr;
    // The slot is 64 KB and starts on a 64 KB boundary, which is what
    // esp_partition_mmap requires of both arguments.
    if (esp_partition_mmap(part, slot, FlashFontPartition::LANG_RESERVED_BYTES, ESP_PARTITION_MMAP_DATA, &raw,
                           &s_mmapHandle) != ESP_OK) {
      LOG_ERR("FLP", "mmap failed");
      return false;
    }
    s_mmapPtr = static_cast<const uint8_t*>(raw);
  }

  out->offsets = reinterpret_cast<const uint16_t*>(s_mmapPtr + h.tableOff);
  out->data = reinterpret_cast<const char*>(s_mmapPtr + h.dataOff);
  LOG_DBG("FLP", "language %u mapped: table=%p data=%p", langIndex, out->offsets, out->data);
  return true;
}

void unmap() {
  if (!s_mmapPtr) return;
  esp_partition_munmap(s_mmapHandle);
  s_mmapHandle = 0;
  s_mmapPtr = nullptr;
}

bool isMapped() { return s_mmapPtr != nullptr; }

}  // namespace FlashLangPartition
