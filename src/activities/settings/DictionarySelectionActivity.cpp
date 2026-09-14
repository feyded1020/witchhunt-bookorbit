#include "DictionarySelectionActivity.h"

#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void DictionarySelectionActivity::onEnter() {
  DictionaryRegistry::discover(dictionaries);

  // Point at what is currently selected. A configured dictionary that is no
  // longer on the card falls back to "None" in the list without clearing the
  // setting -- the card may simply not be readable this boot.
  activeIndex = 0;
  for (size_t i = 0; i < dictionaries.size(); i++) {
    if (dictionaries[i].name == SETTINGS.dictionaryName) {
      activeIndex = static_cast<int>(i) + 1;
      break;
    }
  }
  rowItems.clear();
  rowItems.reserve(optionCount());
  for (size_t index = 0; index < optionCount(); ++index) {
    fui::ListItem item;
    item.label = index == 0 ? tr(STR_NONE_OPT) : dictionaries[index - 1].name.c_str();
    if (static_cast<int>(index) == activeIndex) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(index);
    rowItems.push_back(item);
  }

  UiListActivity::onEnter();
  nav.selected = activeIndex;
}

const char* DictionarySelectionActivity::headerTitle() const { return tr(STR_DICTIONARY); }

void DictionarySelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  if (index <= 0) {
    SETTINGS.dictionaryName[0] = '\0';
  } else {
    const std::string& name = dictionaries[static_cast<size_t>(index) - 1].name;
    strncpy(SETTINGS.dictionaryName, name.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  }
  // Persist here rather than leaning on the settings screen: the reader menu
  // can send the user straight into this picker when no dictionary is set, and
  // that path never passes through SettingsActivity.
  SETTINGS.saveToFile();
  finish();
}

void DictionarySelectionActivity::buildScreen(UiScreen& screen) {
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
