#include "FontScalingTestActivity.h"

#include <CrossPointSettings.h>
#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Short, because the panel is 480 px wide in portrait and 24 pt runs about 23 px per character.
// Every string here was measured against the shipped advances rather than eyeballed -- the
// 37-character pangram this started with came to 926 px, nearly twice the screen, and would have
// been silently clipped at the right edge.
//
// Chosen for the glyphs resampling handles worst rather than for meaning: narrow stems (i, l),
// diagonals (W, x, v), descenders (j, g, p, q) and a feature a few pixels across (.). Between the
// specimen and the prose below, all ten are covered.
constexpr const char* kPangram = "Vex jolly quiz";
// Running text, for judging a paragraph rather than a specimen line: weight and rhythm across a
// block are what a reader sees, where an isolated word is judged as a shape.
constexpr const char* kProse[] = {
    "The sun had not",
    "yet risen; the",
    "grey sea was a",
    "rumpled cloth.",
};
constexpr int kProseLines = sizeof(kProse) / sizeof(kProse[0]);

struct StyleRow {
  EpdFontFamily::Style style;
  const char* label;
};
// All four, because italic and bold-italic carry the steepest diagonals and the thinnest joins in
// the family — if scaling shows anywhere, it shows there first.
constexpr StyleRow kStyles[] = {
    {EpdFontFamily::REGULAR, "Regular"},
    {EpdFontFamily::BOLD, "Bold"},
    {EpdFontFamily::ITALIC, "Italic"},
    {EpdFontFamily::BOLD_ITALIC, "Bold Italic"},
};
constexpr int kStyleCount = sizeof(kStyles) / sizeof(kStyles[0]);

}  // namespace

// 24 pt is the pair we can compare directly, because both a real 24 pt face and an 18 pt master
// ship today. 12-from-24 is the reduction case, included because it is the one Jens' reading
// experience says deteriorates — small text needs sharp edges — and it is the claim the
// coverage metric is least able to see.
const FontScalingTestActivity::Page FontScalingTestActivity::kPages[] = {
    {"Bookerly 24pt: real vs scaled from 18pt", BOOKERLY_24_FONT_ID, BOOKERLY_18_FONT_ID, 24.0f / 18.0f, false},
    {"Noto Sans 24pt: real vs scaled from 18pt", NOTOSANS_24_FONT_ID, NOTOSANS_18_FONT_ID, 24.0f / 18.0f, false},
    {"Bookerly 24pt in running text", BOOKERLY_24_FONT_ID, BOOKERLY_18_FONT_ID, 24.0f / 18.0f, true},
    {"Bookerly 12pt: real vs REDUCED from 24pt", BOOKERLY_12_FONT_ID, BOOKERLY_24_FONT_ID, 12.0f / 24.0f, false},
    {"Bookerly 12pt reduced, running text", BOOKERLY_12_FONT_ID, BOOKERLY_24_FONT_ID, 12.0f / 24.0f, true},
};
const uint8_t FontScalingTestActivity::kPageCount = sizeof(kPages) / sizeof(kPages[0]);

void FontScalingTestActivity::onEnter() {
  Activity::onEnter();
  page_ = 0;
  requestUpdate();
}

void FontScalingTestActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    page_ = static_cast<uint8_t>((page_ + 1) % kPageCount);
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    page_ = static_cast<uint8_t>((page_ + kPageCount - 1) % kPageCount);
    requestUpdate();
  }
}

void FontScalingTestActivity::renderContent() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  const Page& p = kPages[page_];

  GUI.drawHeader(renderer,
                 Rect{contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight},
                 p.title, nullptr);

  const int leftX = contentRect.x + metrics.verticalSpacing * 2;
  int y = contentRect.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int bottom = contentRect.y + contentRect.height;

  // The real face's line height sets the rhythm for both rows of a pair, so a size difference
  // between them shows as a difference in the GLYPHS rather than in the leading.
  const int lineH = renderer.getLineHeight(p.realFontId);
  const int labelH = renderer.getLineHeight(UI_10_FONT_ID);

  if (p.runningText) {
    // Interleaved, real line then scaled line, so the eye compares adjacent baselines instead of
    // holding one block in memory while looking at another.
    for (int i = 0; i < kProseLines && y + lineH * 2 + labelH < bottom; ++i) {
      renderer.drawText(UI_10_FONT_ID, leftX, y + labelH, "real", true);
      renderer.drawText(p.realFontId, leftX + contentRect.width / 6, y + lineH, kProse[i], true);
      y += lineH;
      renderer.drawText(UI_10_FONT_ID, leftX, y + labelH, "scaled", true);
      renderer.drawTextScaled(p.masterFontId, leftX + contentRect.width / 6, y + lineH, kProse[i], true,
                              EpdFontFamily::REGULAR, p.scale);
      y += lineH + metrics.verticalSpacing;
    }
    return;
  }

  for (int s = 0; s < kStyleCount && y + lineH * 2 + labelH < bottom; ++s) {
    renderer.drawText(UI_10_FONT_ID, leftX, y + labelH, kStyles[s].label, true);
    y += labelH;
    renderer.drawText(p.realFontId, leftX, y + lineH, kPangram, true, kStyles[s].style);
    y += lineH;
    renderer.drawTextScaled(p.masterFontId, leftX, y + lineH, kPangram, true, kStyles[s].style, p.scale);
    y += lineH + metrics.verticalSpacing;
  }
}

void FontScalingTestActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderContent();
  renderer.displayBuffer();

  // The whole point is to judge anti-aliased output, so the grayscale passes run unconditionally
  // here rather than behind SETTINGS.textAntiAliasing: a comparison made with AA off would say
  // nothing about how the reader will look for anyone who leaves it on, which is the default.
  renderer.setFastGrayscaleLut(SETTINGS.fastAntiAliasing);
  renderer.renderGrayscalePlanesSequential([this](GfxRenderer::RenderMode) { renderContent(); },
                                           [] { return false; });
}
