#include "FontScalingTestActivity.h"

#include <CrossPointSettings.h>
#include <GfxRenderer.h>

#include <cstdio>

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

// Every size the ladder would advertise, in one column, so the progression can be judged as a
// reader meets it rather than one pair at a time. The question this page answers is not "are
// these two glyphs alike" but "does the synthesised size sit naturally between its real
// neighbours" -- a size that is individually defensible can still read as a step out of place.
//
// realId 0 means the size has no face of its own and is drawn by scaling `masterId`. That is
// today's tree, not the plan: 20 pt is scaled here because no 20 pt face exists yet, whereas the
// plan makes it real and synthesises 22/24/26 from it. So the ratios below (x1.111, x1.222,
// x1.444 from 18 pt) are LARGER than the ones the plan would use (x1.10, x1.20, x1.30 from 20 pt).
// This page is therefore the pessimistic case, which is the useful direction to be wrong in.
struct LadderRow {
  uint8_t pt;
  int realId;
  int masterId;
};
// ONE LADDER PER FAMILY. The first version of this page carried only Bookerly, which made it
// look as though a Noto artifact seen on the per-style page had gone away -- it had not, the page
// simply never drew Noto. A comparison screen that silently covers one of the two things being
// compared is worse than no screen.
constexpr LadderRow kLadderBookerly[] = {
    {10, BOOKERLY_10_FONT_ID, 0},
    {12, BOOKERLY_12_FONT_ID, 0},
    {14, BOOKERLY_14_FONT_ID, 0},
    {16, BOOKERLY_16_FONT_ID, 0},
    {18, BOOKERLY_18_FONT_ID, 0},
    {20, 0, BOOKERLY_18_FONT_ID},
    {22, 0, BOOKERLY_18_FONT_ID},
    {24, BOOKERLY_24_FONT_ID, 0},
    {26, 0, BOOKERLY_18_FONT_ID},
};
constexpr LadderRow kLadderNotoSans[] = {
    {10, NOTOSANS_10_FONT_ID, 0},
    {12, NOTOSANS_12_FONT_ID, 0},
    {14, NOTOSANS_14_FONT_ID, 0},
    {16, NOTOSANS_16_FONT_ID, 0},
    {18, NOTOSANS_18_FONT_ID, 0},
    {20, 0, NOTOSANS_18_FONT_ID},
    {22, 0, NOTOSANS_18_FONT_ID},
    {24, NOTOSANS_24_FONT_ID, 0},
    {26, 0, NOTOSANS_18_FONT_ID},
};
constexpr int kLadderCount = sizeof(kLadderBookerly) / sizeof(kLadderBookerly[0]);
// The point size each scaled row is derived from. One constant rather than per-row, because every
// scaled row here comes off the same master.
constexpr float kLadderMasterPt = 18.0f;

}  // namespace

// 24 pt is the pair we can compare directly, because both a real 24 pt face and an 18 pt master
// ship today. 12-from-24 is the reduction case, included because it is the one Jens' reading
// experience says deteriorates — small text needs sharp edges — and it is the claim the
// coverage metric is least able to see.
const FontScalingTestActivity::Page FontScalingTestActivity::kPages[] = {
    // First, because it frames every page after it: if a synthesised size reads as a step out of
    // place here, the per-style pairs explain why.
    {"Bookerly 10-26pt (R real / S scaled)", 0, 0, 0.0f, Mode::Ladder},
    {"Noto Sans 10-26pt (R real / S scaled)", 1, 0, 0.0f, Mode::Ladder},
    {"Bookerly 24pt: real vs scaled from 18pt", BOOKERLY_24_FONT_ID, BOOKERLY_18_FONT_ID, 24.0f / 18.0f,
     Mode::Styles},
    {"Noto Sans 24pt: real vs scaled from 18pt", NOTOSANS_24_FONT_ID, NOTOSANS_18_FONT_ID, 24.0f / 18.0f,
     Mode::Styles},
    {"Bookerly 24pt in running text", BOOKERLY_24_FONT_ID, BOOKERLY_18_FONT_ID, 24.0f / 18.0f, Mode::RunningText},
    {"Bookerly 12pt: real vs REDUCED from 24pt", BOOKERLY_12_FONT_ID, BOOKERLY_24_FONT_ID, 12.0f / 24.0f,
     Mode::Styles},
    {"Bookerly 12pt reduced, running text", BOOKERLY_12_FONT_ID, BOOKERLY_24_FONT_ID, 12.0f / 24.0f,
     Mode::RunningText},
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

  if (p.mode == Mode::Ladder) {
    renderLadder(contentRect.width, leftX, y, bottom, p.realFontId != 0);
    return;
  }

  if (p.mode == Mode::RunningText) {
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

void FontScalingTestActivity::renderLadder(const int contentWidth, const int leftX, int y, const int bottom,
                                           const bool notoSans) const {
  const LadderRow* const ladder = notoSans ? kLadderNotoSans : kLadderBookerly;
  // A fixed label column so every specimen starts at the same x: the eye compares the left edges
  // of the text, and a ragged start would read as a size difference that is not there.
  const int textX = leftX + contentWidth * 15 / 100;
  const int labelH = renderer.getLineHeight(UI_10_FONT_ID);

  for (int i = 0; i < kLadderCount; ++i) {
    const LadderRow& r = ladder[i];
    const bool real = r.realId != 0;
    const int fontId = real ? r.realId : r.masterId;
    const float scale = real ? 1.0f : r.pt / kLadderMasterPt;
    const int lineH = real ? renderer.getLineHeight(fontId) : renderer.getLineHeightScaled(fontId, scale);
    if (y + lineH > bottom) break;

    char label[12];
    snprintf(label, sizeof(label), "%upt %c", static_cast<unsigned>(r.pt), real ? 'R' : 'S');
    // Baseline-aligned with the specimen rather than the row top, so the marker does not read as
    // part of the specimen's own line.
    renderer.drawText(UI_10_FONT_ID, leftX, y + lineH, label, true);
    if (real) {
      renderer.drawText(fontId, textX, y + lineH, kPangram, true);
    } else {
      renderer.drawTextScaled(fontId, textX, y + lineH, kPangram, true, EpdFontFamily::REGULAR, scale);
    }
    y += lineH;
  }
}

void FontScalingTestActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderContent();
  // FULL refresh on every page, not FAST. This screen exists to judge glyph edges, and a FAST
  // refresh leaves the previous page's ink as a differential baseline -- so a ghost of the page
  // before can sit inside a stem and read exactly like a resampling artifact. Jens hit precisely
  // that ambiguity and reasonably wondered whether an artifact he had seen was a one-time
  // rendering effect. Paying ~2 s per page removes the doubt instead of leaving it to be argued
  // about, which is the only reason this screen is worth having.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  // The whole point is to judge anti-aliased output, so the grayscale passes run unconditionally
  // here rather than behind SETTINGS.textAntiAliasing: a comparison made with AA off would say
  // nothing about how the reader will look for anyone who leaves it on, which is the default.
  renderer.setFastGrayscaleLut(SETTINGS.fastAntiAliasing);
  renderer.renderGrayscalePlanesSequential([this](GfxRenderer::RenderMode) { renderContent(); },
                                           [] { return false; });
}
