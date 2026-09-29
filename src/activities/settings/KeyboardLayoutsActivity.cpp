#include "KeyboardLayoutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void KeyboardLayoutsActivity::onEnter() {
  UiListActivity::onEnter();
  workingMask = keyboard_layouts::enabled();
  edited = false;

  // Each layout goes by the language it is for, named in that language.
  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    rowItems[i].label = I18N.getLanguageName(keyboard_layouts::ALL[i].language);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void KeyboardLayoutsActivity::onExit() {
  if (edited && workingMask != SETTINGS.keyboardLayouts) {
    SETTINGS.keyboardLayouts = workingMask;
    SETTINGS.saveToFile();
  }
  UiListActivity::onExit();
}

const char* KeyboardLayoutsActivity::headerTitle() const { return tr(STR_KEYBOARD_LAYOUTS); }

bool KeyboardLayoutsActivity::isLocked(const uint8_t i) const {
  const uint16_t bit = keyboard_layouts::bitAt(i);
  if (!(workingMask & bit)) return false;
  const uint16_t without = static_cast<uint16_t>(workingMask & ~bit);
  return (without & keyboard_layouts::LATIN_BITS) == 0;
}

void KeyboardLayoutsActivity::activateIndex(const int index) {
  nav.selected = index;
  // The row stays on screen with a new ON/OFF value; a lingering flash would
  // gray an unrelated row on the repaint below.
  app.clearTapFlash();

  if (isLocked(static_cast<uint8_t>(index))) {
    requestUpdate();
    return;
  }

  workingMask = static_cast<uint16_t>(workingMask ^ keyboard_layouts::bitAt(static_cast<uint8_t>(index)));
  edited = true;
  requestUpdate();
}

void KeyboardLayoutsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    const auto row = static_cast<uint8_t>(i);
    if (isLocked(row)) {
      rowItems[i].value = tr(STR_DEFAULT_VALUE);
    } else {
      rowItems[i].value = (workingMask & keyboard_layouts::bitAt(row)) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = keyboard_layouts::COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons are handled by UiListActivity::loop()
  syncListViewport(screen, props);
  screen.list(props);
}
