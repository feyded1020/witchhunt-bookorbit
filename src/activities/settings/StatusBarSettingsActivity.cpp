#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId progressBarNames[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
const StrId progressBarThicknessNames[] = {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                                           StrId::STR_PROGRESS_BAR_THICK};
const StrId titleNames[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
const StrId statusItemsPositionNames[] = {StrId::STR_TOP, StrId::STR_BOTTOM};
const StrId clockPositionNames[] = {StrId::STR_ALIGN_LEFT, StrId::STR_ALIGN_RIGHT};

// One menu row. Editing a status-bar option means: cycle `field` through `valueCount` values and
// display its current value. Rows with an enum-style set of choices provide `valueNames` (indexed by
// the field value); rows with no `valueNames` are on/off toggles, drawn as a switch.
//
// The whole menu is this single table. Adding, removing, or reordering a row is a one-line edit here —
// there is no parallel index bookkeeping to keep in sync. Rows with `requiresClock` are skipped when
// the clock feature is off, so the visible list compacts without any index remapping.
struct StatusBarItem {
  StrId label;
  uint8_t CrossPointSettings::* field;
  uint8_t valueCount;
  uint8_t defaultValue;     // value to reset to if the stored one is out of range
  const StrId* valueNames;  // nullptr → on/off switch
  bool requiresClock;
};

template <size_t N>
constexpr StatusBarItem enumItem(StrId label, uint8_t CrossPointSettings::* field, const StrId (&names)[N],
                                 uint8_t defaultValue, bool requiresClock = false) {
  return {label, field, static_cast<uint8_t>(N), defaultValue, names, requiresClock};
}
constexpr StatusBarItem toggleItem(StrId label, uint8_t CrossPointSettings::* field, bool requiresClock = false) {
  return {label, field, 2, 1, nullptr, requiresClock};
}

const StatusBarItem statusBarItems[] = {
    enumItem(StrId::STR_STATUS_ITEMS_POSITION, &CrossPointSettings::statusBarItemsPosition, statusItemsPositionNames,
             CrossPointSettings::STATUS_BAR_ITEMS_POSITION::STATUS_BAR_ITEMS_BOTTOM),
    toggleItem(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount),
    toggleItem(StrId::STR_PRINTED_PAGE_NUMBER, &CrossPointSettings::statusBarPrintedPage),
    toggleItem(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage),
    enumItem(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle, titleNames,
             CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE),
    toggleItem(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery),
    toggleItem(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, /*requiresClock=*/true),
    enumItem(StrId::STR_CLOCK_POSITION, &CrossPointSettings::statusBarClockPosition, clockPositionNames,
             CrossPointSettings::STATUS_BAR_CLOCK_POSITION::STATUS_BAR_CLOCK_LEFT, /*requiresClock=*/true),
    enumItem(StrId::STR_UPPER_PROGRESS_BAR, &CrossPointSettings::statusBarUpperProgressBar, progressBarNames,
             CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS),
    enumItem(StrId::STR_UPPER_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarUpperProgressBarThickness,
             progressBarThicknessNames, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL),
    enumItem(StrId::STR_LOWER_PROGRESS_BAR, &CrossPointSettings::statusBarLowerProgressBar, progressBarNames,
             CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS),
    enumItem(StrId::STR_LOWER_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarLowerProgressBarThickness,
             progressBarThicknessNames, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL),
};
static_assert(sizeof(statusBarItems) / sizeof(statusBarItems[0]) ==
                  static_cast<size_t>(StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS),
              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with statusBarItems[]");

// Map a visible row index (clock rows omitted when the clock is off) to its entry in statusBarItems.
const StatusBarItem& visibleItem(int visibleIndex) {
  int seen = 0;
  for (const auto& item : statusBarItems) {
    if (item.requiresClock && !SETTINGS.useClock) {
      continue;
    }
    if (seen == visibleIndex) {
      return item;
    }
    ++seen;
  }
  return statusBarItems[0];  // out-of-range guard; callers clamp the index first
}

int visibleItemCount() {
  return static_cast<int>(
      std::count_if(std::begin(statusBarItems), std::end(statusBarItems),
                    [](const StatusBarItem& item) { return !item.requiresClock || SETTINGS.useClock; }));
}

constexpr int previewHorizontalInset = 10;
constexpr int previewHeight = 78;
constexpr int previewInnerMargin = 4;
constexpr int previewBatteryInset = 2;  // matches the battery's inset from the margin in the real bar
constexpr int statusItemGap = 8;        // gap between adjacent status items, as in BaseTheme::drawStatusBar

// The band under the list that the preview owns: its label, the box, and a spacing above and below.
int previewBandHeight(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  return renderer.getLineHeight(UI_10_FONT_ID) + previewHeight + metrics.verticalSpacing * 2;
}

void drawPreviewProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t progressBar,
                            const uint8_t thickness, const bool topEdge) {
  if (progressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS) {
    return;
  }

  const int percent = progressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS ? 75 : 25;
  const int barHeight = UITheme::getProgressBarHeight(progressBar, thickness);
  const int y = topEdge ? rect.y + previewInnerMargin : rect.y + rect.height - previewInnerMargin - barHeight;
  const int barWidth = (rect.width - previewInnerMargin * 2) * percent / 100;
  renderer.fillRect(rect.x + previewInnerMargin, y, barWidth, barHeight);
}

void drawPreviewStatusItems(const GfxRenderer& renderer, const Rect& rect, const ThemeMetrics& metrics) {
  const bool hasProgressText = SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage;
  const bool hasTitle = SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE;
  const bool hasStatusItems = hasProgressText || hasTitle || SETTINGS.statusBarBattery ||
                              SETTINGS.statusBarPrintedPage || (SETTINGS.useClock && SETTINGS.statusBarClock);
  if (!hasStatusItems) {
    return;
  }

  const bool statusItemsAtTop =
      SETTINGS.statusBarItemsPosition == CrossPointSettings::STATUS_BAR_ITEMS_POSITION::STATUS_BAR_ITEMS_TOP;
  const int adjacentProgressHeight = statusItemsAtTop
                                         ? UITheme::getProgressBarHeight(SETTINGS.statusBarUpperProgressBar,
                                                                         SETTINGS.statusBarUpperProgressBarThickness)
                                         : UITheme::getProgressBarHeight(SETTINGS.statusBarLowerProgressBar,
                                                                         SETTINGS.statusBarLowerProgressBarThickness);
  const int statusItemsHeight = UITheme::getStatusBarItemsHeight();
  const int textY = statusItemsAtTop
                        ? rect.y + previewInnerMargin + adjacentProgressHeight + 4
                        : rect.y + rect.height - previewInnerMargin - adjacentProgressHeight - statusItemsHeight + 4;

  const bool showBatteryPercentage =
      SETTINGS.statusBarBattery &&
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  const bool showClock = SETTINGS.useClock && SETTINGS.statusBarClock;
  const int previewClockWidth = showClock ? renderer.getTextWidth(SMALL_FONT_ID, "00:00") : 0;
  const bool clockOnRight =
      SETTINGS.statusBarClockPosition == CrossPointSettings::STATUS_BAR_CLOCK_POSITION::STATUS_BAR_CLOCK_RIGHT;

  // Left cluster: battery, then the clock when it is left-positioned. Reserving the battery's
  // *measured* width (icon + percentage) is what keeps the clock off the percentage text —
  // estimating it is what made the preview overlap (issue #214).
  const int leftClusterX = rect.x + previewInnerMargin + previewBatteryInset;
  int leftClusterWidth = 0;
  if (SETTINGS.statusBarBattery) {
    GUI.drawBatteryLeft(renderer, Rect{leftClusterX, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
    leftClusterWidth = BaseTheme::statusBarBatteryWidth(renderer, metrics, showBatteryPercentage);
  }

  // Right-aligned zone: the printed ("physical") page label sits to the LEFT of the device page
  // counter as a parenthesised hint, matching BaseTheme::drawStatusBar. Example label "(vii)".
  const char* printedLabel = SETTINGS.statusBarPrintedPage ? "(vii)" : "";
  const int printedLabelWidth = *printedLabel ? renderer.getTextWidth(SMALL_FONT_ID, printedLabel) : 0;
  const int printedLabelGap = printedLabelWidth > 0 && hasProgressText ? 8 : 0;

  int progressTextWidth = 0;
  const int rightEdge = rect.x + rect.width - previewInnerMargin - 2;
  if (hasProgressText) {
    char progressStr[32] = "";
    if (SETTINGS.statusBarChapterPageCount && SETTINGS.statusBarBookProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d  %d%%", 8, 32, 75);
    } else if (SETTINGS.statusBarBookProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%d%%", 75);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", 8, 32);
    }

    const int progressStrWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    progressTextWidth = progressStrWidth + printedLabelGap + printedLabelWidth;
    renderer.drawText(SMALL_FONT_ID, rightEdge - progressStrWidth, textY, progressStr);
    if (printedLabelWidth > 0) {
      renderer.drawText(SMALL_FONT_ID, rightEdge - progressStrWidth - printedLabelGap - printedLabelWidth, textY,
                        printedLabel);
    }
  } else if (printedLabelWidth > 0) {
    progressTextWidth = printedLabelWidth;
    renderer.drawText(SMALL_FONT_ID, rightEdge - printedLabelWidth, textY, printedLabel);
  }

  // Clock goes at whichever end it is configured for, mirroring BaseTheme::drawStatusBar: just
  // past the battery on the left, or just past the progress text on the right.
  int rightClusterWidth = progressTextWidth;
  if (showClock) {
    int clockX;
    if (clockOnRight) {
      rightClusterWidth += (rightClusterWidth > 0 ? statusItemGap : 0) + previewClockWidth;
      clockX = rightEdge - rightClusterWidth;
    } else {
      clockX = leftClusterX + leftClusterWidth + statusItemGap;
      leftClusterWidth += statusItemGap + previewClockWidth;
    }
    renderer.drawText(SMALL_FONT_ID, clockX, textY, "00:00");
  }

  if (!hasTitle) {
    return;
  }

  const char* title = SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE
                          ? tr(STR_EXAMPLE_BOOK)
                          : tr(STR_EXAMPLE_CHAPTER);
  const int leftReserve = leftClusterWidth > 0 ? previewBatteryInset + leftClusterWidth + statusItemGap : 6;
  const int rightReserve = rightClusterWidth > 0 ? rightClusterWidth + 18 : 6;
  const int titleAreaWidth = rect.width - previewInnerMargin * 2 - leftReserve - rightReserve;
  if (titleAreaWidth <= 0) {
    return;
  }

  std::string previewTitle = renderer.truncatedText(SMALL_FONT_ID, title, titleAreaWidth);
  const int titleWidth = renderer.getTextWidth(SMALL_FONT_ID, previewTitle.c_str());
  renderer.drawText(SMALL_FONT_ID, rect.x + previewInnerMargin + leftReserve + (titleAreaWidth - titleWidth) / 2, textY,
                    previewTitle.c_str());
}
}  // namespace

void StatusBarSettingsActivity::onEnter() {
  // Clamp status bar settings in case of corrupt/migrated data: every field must hold a valid value
  // index (0..valueCount-1). A stray value would index past its valueNames array when rendered.
  for (const auto& item : statusBarItems) {
    if (SETTINGS.*item.field >= item.valueCount) {
      SETTINGS.*item.field = item.defaultValue;
    }
  }

  // The rows this visit shows. Labels and action values never change while the screen is open;
  // buildScreen() fills in the values.
  rowCount = visibleItemCount();
  for (int i = 0; i < rowCount; ++i) {
    rowItems[i] = {};
    rowItems[i].label = I18N.get(visibleItem(i).label);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }

  // Last: this arms the screen, so a render can build from here on.
  UiListActivity::onEnter();
}

const char* StatusBarSettingsActivity::headerTitle() const { return tr(STR_CUSTOMISE_STATUS_BAR); }

// Every row changes in place on Confirm: a switch flips, a value moves to the next one.
const char* StatusBarSettingsActivity::footerConfirmLabel() const { return tr(STR_TOGGLE); }

void StatusBarSettingsActivity::activateIndex(const int index) {
  // The row repaints with its new value; a lingering tap flash would gray it.
  app.clearTapFlash();
  nav.selected = index;
  const StatusBarItem& item = visibleItem(index);
  SETTINGS.*item.field = static_cast<uint8_t>((SETTINGS.*item.field + 1) % item.valueCount);
  SETTINGS.saveToFile();
  // Nothing else repaints: the controller's Confirm path calls activateIndex() and returns.
  requestUpdate();
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  // Above the preview band afterUiRender() draws into.
  layoutListArea(screen, 0, static_cast<int16_t>(previewBandHeight(renderer, UITheme::getInstance().getMetrics())));

  // Labels were set in onEnter(); the values track SETTINGS, so they are refreshed on every pass.
  // A value is an I18N pointer and a switch is two flags: nothing is allocated.
  for (int i = 0; i < rowCount; ++i) {
    const StatusBarItem& item = visibleItem(i);
    const uint8_t value = SETTINGS.*item.field;
    auto& row = rowItems[i];
    row.toggle = item.valueNames == nullptr;
    row.toggleChecked = row.toggle && value != 0;
    row.value = row.toggle ? nullptr : I18N.get(item.valueNames[value]);
  }

  auto props = listProps(screen);
  props.items = rowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.valueInset = 8;  // air between the value and the row edge
  addList(screen, props);
}

void StatusBarSettingsActivity::afterUiRender() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = listContentRect();
  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int bandTop = content.y + content.height - previewBandHeight(renderer, metrics);

  const int labelY = bandTop + metrics.verticalSpacing / 2;
  renderer.drawText(UI_10_FONT_ID, content.x + metrics.contentSidePadding, labelY, tr(STR_PREVIEW));
  const Rect previewRect{content.x + previewHorizontalInset, labelY + labelHeight + metrics.verticalSpacing / 2,
                         content.width - previewHorizontalInset * 2, previewHeight};
  renderer.drawRect(previewRect.x, previewRect.y, previewRect.width, previewRect.height);
  drawPreviewProgressBar(renderer, previewRect, SETTINGS.statusBarUpperProgressBar,
                         SETTINGS.statusBarUpperProgressBarThickness, true);
  drawPreviewProgressBar(renderer, previewRect, SETTINGS.statusBarLowerProgressBar,
                         SETTINGS.statusBarLowerProgressBarThickness, false);
  drawPreviewStatusItems(renderer, previewRect, metrics);
}
