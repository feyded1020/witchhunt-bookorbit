#pragma once

#include <EpdFontFamily.h>

#include <cstddef>
#include <string>

// Fitting a UI label into a fixed width, ellipsised.
//
// Split out of GfxRenderer so it can be exercised on the host: it needs a font and nothing else
// — no framebuffer, no display HAL — and the host build stubs GfxRenderer.h wholesale, so a
// member function there would be untestable. See test/text_truncation/.
namespace textTruncation {

// U+2026 HORIZONTAL ELLIPSIS, as UTF-8 and as a codepoint.
inline constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";
inline constexpr uint32_t ELLIPSIS_CP = 0x2026;

// Whether this font can be truncated at all, i.e. whether it carries U+2026. Asked separately so
// truncateToWidth() does not need a sentinel return that an empty input could also produce.
bool canTruncate(const EpdFontFamily& font, EpdFontFamily::Style style);

// Byte length of the longest prefix of `text` whose rendered width, with the ellipsis appended,
// stays under maxWidth; 0 when not even one character does. Requires canTruncate().
size_t prefixFittingWidth(const EpdFontFamily& font, const char* text, int maxWidth, EpdFontFamily::Style style);

// `text` if it fits in maxWidth, otherwise its longest fitting prefix plus an ellipsis.
// Requires canTruncate(); the caller keeps its own fallback for fonts without an ellipsis.
//
// One forward pass, replacing a loop that dropped the last character and then re-measured the
// WHOLE remaining string, allocating a fresh std::string each step.
//
// Counted in character-measurements for a 39-character row trimmed by ten: the old loop paid
// 39 + (39+38+...+30) = 384; this pays 39 for the fits check plus ~44 for a walk to the cut
// (each step doing one extra kern and glyph lookup for the speculative ellipsis), so ~83.
// Roughly 4-5x, and it widens the more there is to trim — a 60-character row trimmed by thirty
// goes from ~1425 to ~105, about 13x. On an X3 a 39-character row measures in 181 us
// (env:bench_font), so that first case is ~1.9 ms down to ~400 us, per row, on screens where
// most rows overflow.
//
// The fits check is kept deliberately even though the walk could answer it: "fits unchanged" is
// the common case on every screen, and it should be decided by the real measurement rather than
// by this walk's approximation of it. That caps the speed-up and is worth the cap.
//
// Models the advance chain and ink bounding box the way EpdFont::getTextBounds() does, so the cut
// normally lands where the old loop put it. Two things it does not model: SMALL_CAPS folding,
// which scales every metric (callers must not pass it — GfxRenderer::truncatedText() keeps the
// old loop for that), and combining-mark centering, which it treats as an ordinary advance. A
// combining mark therefore reads a pixel or two wide rather than zero — an OVER-estimate, so it
// can only cut earlier, never overflow the box.
//
// Approximating is safe by construction, and is the whole reason this is cheap: every caller of
// GfxRenderer::truncatedText() is in src/activities or src/components/themes, and nothing in
// lib/Epub, lib/Txt, lib/Md or lib/Xtc uses it. The result reaches no cache key, no pagination
// and no file format, so a cut differing by one character is invisible. **Do not reuse this for
// reader layout**, where the metrics walk has to stay byte-exact against the page cache.
std::string truncateToWidth(const EpdFontFamily& font, const char* text, int maxWidth, EpdFontFamily::Style style);

}  // namespace textTruncation
