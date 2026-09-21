#include "ControlsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ControlsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ControlsActivity::loop() {
  // Nothing here is selectable, so Confirm leaves as Back does rather than doing nothing. On a
  // board with no Back key the strip's Back box is a tap away, and so is the Home key.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
}

void ControlsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  GUI.drawHeader(renderer, UITheme::getHeaderRect(renderer), tr(STR_CONTROLS));

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = contentRect.height - listTop - metrics.verticalSpacing;
  // Action on the left, gesture on the right: the same shape as a settings row, which is the
  // arrangement everything else on this device reads in.
  // -1: no row highlighted. drawList skips the highlight fill, starts at row 0 and draws every
  // row in normal ink, which is what a page you only read should look like.
  GUI.drawList(
      renderer, Rect{contentRect.x, listTop, contentRect.width, listHeight}, rowCount(), /*selectedIndex=*/-1,
      [this](int index) { return entries[index].action; }, nullptr, nullptr,
      [this](int index) { return entries[index].gesture; });

  const auto hints = mappedInput.mapHints(tr(STR_BACK), "", "", "", "", "");
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
  renderer.displayBuffer();
}
