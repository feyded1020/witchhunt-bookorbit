#include "EnumSelectionActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

uint8_t EnumSelectionActivity::optionCount() const {
  return overrideCount > 0 ? overrideCount : setting.getEnumOptionCount();
}

std::string EnumSelectionActivity::optionLabel(uint8_t index) const {
  if (labelOverride) {
    std::string label = labelOverride(index);
    if (!label.empty()) return label;
  }
  return setting.getEnumOptionLabel(index);
}

void EnumSelectionActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = static_cast<int>(setting.getEnumSelectedIndex());
  const int count = static_cast<int>(optionCount());
  if (nav.selected >= count) nav.selected = 0;

  rowLabels.clear();
  rowItems.clear();
  rowLabels.reserve(count);
  rowItems.reserve(count);
  for (int index = 0; index < count; ++index) {
    rowLabels.push_back(optionLabel(static_cast<uint8_t>(index)));
  }

  const uint8_t activeIndex = setting.getEnumSelectedIndex();
  for (int index = 0; index < count; ++index) {
    fui::ListItem item;
    item.label = rowLabels[index].c_str();
    if (index == static_cast<int>(activeIndex)) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(index);
    rowItems.push_back(item);
  }
}

const char* EnumSelectionActivity::headerTitle() const { return I18N.get(setting.nameId); }

void EnumSelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  setting.setEnumSelectedIndex(static_cast<uint8_t>(index));
  finish();
}

void EnumSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
