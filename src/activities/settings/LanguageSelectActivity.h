#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return totalItems; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  constexpr static uint8_t totalItems = getLanguageCount();
  freeink::ui::ListItem rowItems[totalItems]{};
};
