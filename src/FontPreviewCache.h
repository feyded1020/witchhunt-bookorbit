#pragma once

#include <cstdint>

class GfxRenderer;

// Side-car cache of rendered font preview strips, on the SD card.
//
// Drawing a font preview means loading that family's .cpfont: tens of KB of SD
// reads plus heap for intervals and kern tables, all to render one line of
// sample text. Browsing the font list paid that on every cursor move. The strip
// it produces is small by comparison — a 460x207 preview is 11.6 KB packed
// 1bpp — so keeping the pixels beats re-deriving them.
//
// An entry is keyed by everything that changes those pixels: the family, the
// point size actually loaded, the rectangle it was rendered into (which covers
// panel and orientation), the UI language (the sample string is translated) and
// the size of the source .cpfont, so replacing a font invalidates its preview
// even when it was copied onto the card by hand.
//
// Rows are streamed one at a time in both directions, so the largest buffer
// involved is one packed row (at most 128 bytes, on the stack). Materialising a
// whole strip would mean a ~12 KB heap block on a device where placement of a
// block that size matters more than its size.
namespace FontPreviewCache {

struct Key {
  const char* familyName = nullptr;
  uint32_t sourceSize = 0;  // .cpfont byte count — the staleness check
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t pointSize = 0;
  uint8_t language = 0;
};

// Whether a stored strip matches `key` exactly. Callers probe before laying the
// screen out, because a hit means the sample text is not drawn at all.
bool available(const Key& key);

// Blit a stored strip into the framebuffer at (x, y). Returns false if the
// entry turned out to be unusable after all, in which case part of the
// rectangle may already have been overwritten and the caller must re-render.
// Must run before displayBuffer(), like any other framebuffer write.
bool restore(const GfxRenderer& renderer, const Key& key, int x, int y);

// Capture the rectangle at (x, y) out of the write framebuffer and store it.
// Call once the preview has been rendered and before displayBuffer(), which
// swaps the buffer and leaves the write side holding an older frame.
bool store(const GfxRenderer& renderer, const Key& key, int x, int y);

// Drop every cached strip. For the cache-clearing UI, and for anything that
// installs or removes fonts in bulk.
void clear();

}  // namespace FontPreviewCache
