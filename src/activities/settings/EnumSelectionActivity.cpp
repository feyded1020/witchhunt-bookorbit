#include "EnumSelectionActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"

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
  layoutListArea(screen);
  auto props = listProps(screen);
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  addList(screen, props);
}
