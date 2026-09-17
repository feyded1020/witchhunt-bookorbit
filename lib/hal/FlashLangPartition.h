#pragma once

#include <cstddef>
#include <cstdint>

// The UI language pack: the active language's strings, decompressed once into
// the tail of the spiffs partition and read back through mmap.
//
// Why this exists. Shipping all 24 languages as rodata cost 408,668 B of the app
// image, of which any one reader uses exactly one. The image now carries English
// uncompressed plus a deflate-compressed blob per language (~6 KB each); this
// materialises whichever one is selected so I18n::get() can still hand out a
// plain `const char*` with no RAM standing behind it. An mmap'd flash pointer
// reads exactly like rodata — that equivalence is the whole point, and it is why
// the alternative (decompressing into a heap buffer) was rejected: ~30 KB
// resident on a device whose steady-state free heap at Home is under 40 KB.
//
// Slot layout, at (partition size - FlashFontPartition::LANG_RESERVED_BYTES):
//
//   [ 4 B] magic "CPLP"
//   [ 1 B] format version
//   [ 1 B] language index
//   [ 2 B] key count
//   [ 4 B] stamp — identifies which firmware build's pack this is
//   [ 4 B] table offset, from slot start
//   [ 4 B] data offset
//   [ 4 B] data length
//   [ 8 B] reserved
//   [table] keyCount x uint16_t — offsets into data; bit 15 set means "fall back
//           to English", with the low 15 bits being the offset into the English
//           blob, matching what the generator emits for in-image languages
//   [data]  keyCount NUL-terminated strings, in StrId order. An EMPTY string
//           means the language does not override English for that key; the
//           table entry carries the English offset instead, so the empty byte
//           is never read.
//
// The header is written LAST. A power cut part-way through build() therefore
// leaves no magic, and the slot reads as absent rather than as plausible
// garbage — the reader falls back to English and rebuilds on the next boot.

namespace FlashLangPartition {

static constexpr size_t HEADER_BYTES = 32;
static constexpr uint8_t FORMAT_VERSION = 1;

// The largest back-reference distance the generator's deflate encoder may use.
// It compresses with a 4 KB window precisely so the ring below can be 4 KB
// instead of uzlib's default 32 KB; the two numbers must agree or long matches
// decode to garbage. See gen_i18n.py's DEFLATE_WINDOW_BITS.
static constexpr size_t INFLATE_RING_BYTES = 4096;

struct Mapped {
  const uint16_t* offsets;
  const char* data;
};

// True when the slot already holds exactly this (language, stamp) pair, i.e.
// there is nothing to rebuild.
bool holds(uint8_t langIndex, uint32_t stamp);

// Decompress `compressed` into the slot and derive the offset table from it.
// `enOffsets` supplies the English offset for every entry the blob leaves empty.
// Returns false and leaves the slot without a valid header on any failure, so a
// failed build degrades to English rather than to corruption.
//
// Transient cost while running: a 4 KB inflate ring, a 512 B write staging
// buffer and keyCount x 2 B for the table being derived (~6.3 KB at 887 keys).
// Nothing is retained afterwards.
bool build(uint8_t langIndex, uint32_t stamp, const uint8_t* compressed, size_t compressedLen, size_t uncompressedLen,
           uint16_t keyCount, const uint16_t* enOffsets);

// Map the slot for reading. Fails if it is absent, belongs to another language,
// carries a different stamp, or is malformed.
bool map(uint8_t langIndex, uint32_t stamp, uint16_t keyCount, Mapped* out);

// Release the mapping. No-op if not mapped.
void unmap();

bool isMapped();

}  // namespace FlashLangPartition
