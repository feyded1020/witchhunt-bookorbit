#include "TextTruncation.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>

namespace textTruncation {

bool canTruncate(const EpdFontFamily& font, const EpdFontFamily::Style style) {
  return font.getGlyph(ELLIPSIS_CP, style) != nullptr;
}

size_t prefixFittingWidth(const EpdFontFamily& font, const char* text, const int maxWidth,
                          const EpdFontFamily::Style style) {
  if (!text || !*text) return 0;

  const EpdGlyph* ellipsisGlyph = font.getGlyph(ELLIPSIS_CP, style);
  if (!ellipsisGlyph) return 0;

  const char* cursor = text;
  int penX = 0;  // pen position of the current glyph, whole pixels, as getTextBounds tracks it
  int minX = 0, maxX = 0;
  int32_t prevAdvanceFP = 0;  // 12.4, snapped together with the next kern as a single step
  uint32_t prevCp = 0;
  size_t bestCut = 0;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    cp = font.applyLigatures(cp, cursor, style);

    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (!glyph) {
      // Mirror getTextBounds(): flush the pending advance and restart the kern chain.
      penX += fp4::toPixel(prevAdvanceFP);
      prevCp = 0;
      prevAdvanceFP = 0;
      continue;
    }

    if (prevCp != 0) {
      const auto kernFP = static_cast<int32_t>(font.getKerning(prevCp, cp, style));
      penX += fp4::toPixel(prevAdvanceFP + kernFP);
    }

    minX = std::min(minX, penX + glyph->left);
    maxX = std::max(maxX, penX + glyph->left + glyph->width);

    // The bare prefix is already too wide, so nothing longer can fit either — and neither can
    // this one once an ellipsis is added. Checked before the speculative step so a string whose
    // very first glyph overflows still reports a cut of 0.
    if (maxX - minX >= maxWidth) break;

    // Would the prefix ending here still fit with the ellipsis appended? One speculative step,
    // not a re-measurement: place the ellipsis after this glyph and take its ink.
    const auto ellipsisKernFP = static_cast<int32_t>(font.getKerning(cp, ELLIPSIS_CP, style));
    const int ellipsisPenX = penX + fp4::toPixel(static_cast<int32_t>(glyph->advanceX) + ellipsisKernFP);
    const int withEllipsis = std::max(maxX, ellipsisPenX + ellipsisGlyph->left + ellipsisGlyph->width) -
                             std::min(minX, ellipsisPenX + ellipsisGlyph->left);

    // Strictly less than, matching the condition the old loop trimmed against. Appending more
    // characters only widens the box, so the first prefix that does not fit ends the search.
    if (withEllipsis >= maxWidth) break;
    bestCut = static_cast<size_t>(cursor - text);

    prevAdvanceFP = glyph->advanceX;
    prevCp = cp;
  }

  return bestCut;
}

std::string truncateToWidth(const EpdFontFamily& font, const char* text, const int maxWidth,
                            const EpdFontFamily::Style style) {
  if (!text || maxWidth <= 0) return "";

  // Ask the font itself whether the whole string fits, rather than trusting the walk below to
  // agree with getTextDimensions() to the pixel. The two use the same arithmetic but the walk
  // skips combining-mark centring, and "fits unchanged" is the common case on every screen — it
  // should be decided by the real measurement, not by an approximation of it.
  int w = 0, h = 0;
  font.getTextDimensions(text, &w, &h, style);
  if (w <= maxWidth) return text;

  const size_t cut = prefixFittingWidth(font, text, maxWidth, style);
  if (cut == 0) return ELLIPSIS_UTF8;
  // Structurally rule out returning something WIDER than the input. The walk should never keep
  // every character once getTextDimensions() has said the string overflows -- appending the
  // ellipsis can only widen it further -- but the two differ on combining-mark centring, so
  // rather than reason about whether that gap can ever invert, make the bad outcome impossible.
  if (cut >= strlen(text)) return text;
  return std::string(text, cut) + ELLIPSIS_UTF8;
}

}  // namespace textTruncation
