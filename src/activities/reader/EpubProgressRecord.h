#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// The EPUB reader's progress.bin record, little-endian:
//   spine(2) page(2) pageCount(2) percent(1) [paragraph(2)]
//
// The paragraph is the page's entry in the section's paragraph LUT (the same index KOReader sync
// anchors on). It is written when the reader closes a book, so a book reopened under a different
// layout -- settings changed outside the book, or a firmware whose section layout version moved --
// lands on the paragraph it was left at instead of a page number rescaled by page count.
//
// Older files are shorter, and every other reader of the file (sleep screen, home progress badge)
// reads a fixed prefix, so the optional tail is invisible to them.
struct EpubProgressRecord {
  int spineIndex = 0;
  int page = 0;
  int pageCount = 0;  // 0: unknown (written mid-build or by a sync restore)
  uint8_t percent = 0;
  std::optional<uint16_t> paragraph;

  static constexpr size_t kMaxSize = 9;

  // Returns the number of bytes written to `out`: 9 with a paragraph, 7 without.
  size_t encode(uint8_t (&out)[kMaxSize]) const {
    put16(out, spineIndex);
    put16(out + 2, page);
    put16(out + 4, pageCount);
    out[6] = percent;
    if (!paragraph || *paragraph == 0) return 7;
    put16(out + 7, *paragraph);
    return kMaxSize;
  }

  // Accepts every size the reader has ever written: 4 (spine, page), 6 (+ pageCount),
  // 7 (+ percent) and 9 (+ paragraph). A 5-byte file is a torn write and is rejected, as it always
  // was; an 8-byte one keeps its first 7.
  static std::optional<EpubProgressRecord> decode(const uint8_t* data, const size_t size) {
    if (size < 4 || size == 5) return std::nullopt;
    EpubProgressRecord r;
    r.spineIndex = get16(data);
    r.page = get16(data + 2);
    if (size >= 6) r.pageCount = get16(data + 4);
    if (size >= 7) r.percent = data[6];
    // 0 is "no paragraph opened yet" in the LUT, never an anchor (Section::getParagraphIndexForPage).
    if (size >= 9 && get16(data + 7) != 0) r.paragraph = get16(data + 7);
    return r;
  }

 private:
  static void put16(uint8_t* out, const int value) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  }
  static uint16_t get16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }
};
