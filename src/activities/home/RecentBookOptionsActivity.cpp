#include "RecentBookOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RecentBookOptionsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void RecentBookOptionsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    MenuResult result;
    result.action = static_cast<int>(actions[selectorIndex]);
    setResult(std::move(result));
    finish();
    return;
  }
  buttonNavigator.onNextList(ButtonNavigator::getStepNextButtons(), selectorIndex, rowCount(),
                             [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(ButtonNavigator::getStepPreviousButtons(), selectorIndex, rowCount(),
                                 [this] { requestUpdate(); });
}

void RecentBookOptionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_OPTIONS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // Which book this is about: the actions below are meaningless without it.
  const int titleX = contentRect.x + metrics.contentSidePadding;
  renderer.drawText(
      UI_10_FONT_ID, titleX, contentTop,
      renderer.truncatedText(UI_10_FONT_ID, title.c_str(), contentRect.width - metrics.contentSidePadding * 2).c_str(),
      true, EpdFontFamily::BOLD);

  const int listTop = contentTop + lineHeight + metrics.verticalSpacing;
  const int listHeight = contentRect.height - listTop - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{contentRect.x, listTop, contentRect.width, listHeight}, rowCount(), selectorIndex,
               [this](int index) { return labelFor(actions[index]); });

  const auto hints = mappedInput.mapHints(tr(STR_CANCEL), tr(STR_SELECT), "", "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
  renderer.displayBuffer();
}

std::string RecentBookOptionsActivity::labelFor(const Action action) {
  switch (action) {
    case Action::Open:
      return std::string(tr(STR_OPEN));
    case Action::OpenAndSync:
      return std::string(tr(STR_OPEN_AND_SYNC));
    case Action::Info:
      return std::string(tr(STR_INFO));
    case Action::Remove:
      return std::string(tr(STR_REMOVE));
  }
  return std::string();
}

ListRowTap::Result RecentBookOptionsActivity::selectListRow(const int index) {
  return ListRowTap::apply(index, rowCount(), selectorIndex);
}
