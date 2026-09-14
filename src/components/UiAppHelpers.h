#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

#include "MappedInputManager.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

inline std::atomic<const freeink::ui::ThemeTokens*>& sharedUiThemeCell() {
  static std::atomic<const freeink::ui::ThemeTokens*> cell{nullptr};
  return cell;
}

inline void refreshSharedUiThemeTokens(const freeink::ui::GfxRendererTarget& target) {
  static freeink::ui::ThemeTokens pool[2];
  auto& cell = sharedUiThemeCell();
  const auto* current = cell.load(std::memory_order_relaxed);
  freeink::ui::ThemeTokens* next = current == &pool[0] ? &pool[1] : &pool[0];
  *next = uiThemeTokens(target);
  cell.store(next, std::memory_order_release);
}

template <typename App>
inline void applySharedUiTheme(App& app, const freeink::ui::GfxRendererTarget& target) {
  refreshSharedUiThemeTokens(target);
  app.setThemeRef(&sharedUiThemeCell());
}

inline freeink::ui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer) {
  freeink::ui::GfxRendererTarget target(renderer);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_TITLE, UI_12_FONT_ID);
  return target;
}

inline freeink::ui::InputSnapshot touchSnapshotFrom(const MappedInputManager& mappedInput) {
  freeink::ui::InputSnapshot snapshot{};
  int touchX = 0;
  int touchY = 0;

  if (mappedInput.isScreenTouchHeld(touchX, touchY)) {
    snapshot.touchHeld = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  }
  if (mappedInput.wasScreenTouchPressed(touchX, touchY)) {
    snapshot.touchPressed = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  }
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    snapshot.touchReleased = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  } else if (mappedInput.wasScreenTouchReleased()) {
    snapshot.touchReleased = true;
    snapshot.touchX = -1;
    snapshot.touchY = -1;
  }
  // Some touch backends publish the swipe before the raw release edge. Cancel the pressed row
  // now so it cannot remain armed after the list scrolls and interfere with the next tap.
  if (!snapshot.touchReleased && mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) {
    snapshot.touchReleased = true;
    snapshot.touchX = -1;
    snapshot.touchY = -1;
  }
  return snapshot;
}