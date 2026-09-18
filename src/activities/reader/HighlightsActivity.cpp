#include "HighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t SNIPPET_BYTES = 60;
}

std::string HighlightsActivity::getItemLabel(const int index) const {
  const Highlight& h = store.getAll()[index];
  // A one-line snippet: collapse whitespace and cut on a UTF-8 boundary.
  std::string snippet;
  snippet.reserve(SNIPPET_BYTES + 4);
  bool space = false;
  for (const char c : h.text) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      space = !snippet.empty();
      continue;
    }
    if (snippet.size() >= SNIPPET_BYTES && (static_cast<uint8_t>(c) & 0xC0) != 0x80) {
      snippet += "...";
      break;
    }
    if (space) snippet.push_back(' ');
    space = false;
    snippet.push_back(c);
  }
  std::string label = "\"" + snippet + "\"";
  if (!h.chapter.empty()) label += " - " + h.chapter;
  return label;
}

void HighlightsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void HighlightsActivity::onExit() {
  store.save();
  Activity::onExit();
}

void HighlightsActivity::close() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void HighlightsActivity::deleteSelected() {
  const int total = static_cast<int>(store.getAll().size());
  if (total == 0 || selectorIndex >= total) return;
  store.removeAt(static_cast<size_t>(selectorIndex));
  const int remaining = total - 1;
  if (remaining == 0) {
    close();
    return;
  }
  if (selectorIndex >= remaining) selectorIndex = remaining - 1;
  requestUpdate();
}

void HighlightsActivity::loop() {
  const int totalItems = static_cast<int>(store.getAll().size());

  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    if (ev.type != ButtonEventManager::PressType::Short) continue;
    if (ev.button == MappedInputManager::Button::Back) {
      close();
      return;
    }
    if (totalItems > 0 && ev.button == MappedInputManager::Button::Confirm) {
      const Highlight& h = store.getAll()[selectorIndex];
      const uint16_t q = h.progressQ == Highlight::PROGRESS_UNKNOWN ? 0 : h.progressQ;
      setResult(HighlightJumpResult{h.spineIndex, q});
      finish();
      return;
    }
    // Logical Right deletes, as on the starred-pages list; Up/Down scroll.
    if (totalItems > 0 && MappedInputManager::isDirection(ev.button, MappedInputManager::Direction::Right)) {
      deleteSelected();
      return;
    }
  }

  if (totalItems == 0) return;
  buttonNavigator.onNextList(ButtonNavigator::getStepNextButtons(), selectorIndex, totalItems,
                             [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(ButtonNavigator::getStepPreviousButtons(), selectorIndex, totalItems,
                                 [this] { requestUpdate(); });
}

void HighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_HIGHLIGHTS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;
  const int totalItems = static_cast<int>(store.getAll().size());

  if (totalItems == 0) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                      tr(STR_NO_HIGHLIGHTS));
  } else {
    GUI.drawList(renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, totalItems, selectorIndex,
                 [this](int index) { return getItemLabel(index); });
  }

  const bool hasItems = totalItems > 0;
  const auto hints = mappedInput.mapHints(tr(STR_BACK), hasItems ? tr(STR_SELECT) : "", "",
                                          hasItems ? tr(STR_DELETE) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
  renderer.displayBuffer();
}

ListRowTap::Result HighlightsActivity::selectListRow(const int index) {
  return ListRowTap::apply(index, static_cast<int>(store.getAll().size()), selectorIndex);
}
