#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LanguageSelect", renderer, mappedInput) {}

void LanguageSelectActivity::onEnter() {
  UiListActivity::onEnter();

  // Set current selection based on current language
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  nav.selected = (it != end) ? static_cast<int>(std::distance(begin, it)) : 0;

  for (int index = 0; index < totalItems; ++index) {
    fui::ListItem item;
    item.label = I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[index]));
    if (SORTED_LANGUAGE_INDICES[index] == currentLang) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(index);
    rowItems[index] = item;
  }
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

void LanguageSelectActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(SORTED_LANGUAGE_INDICES[index]));
  }
  finish();
}

void LanguageSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = totalItems;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
