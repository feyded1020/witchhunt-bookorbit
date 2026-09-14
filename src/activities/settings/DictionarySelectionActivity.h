#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/DictionaryRegistry.h"

class MappedInputManager;

/// Full-screen list of the StarDict dictionaries found on the SD card, plus a
/// "None" entry that clears the selection.
///
/// An activity rather than an enum picker because the options are discovered by
/// scanning /dictionaries and /.dictionaries when the list opens, so they cannot
/// be a fixed list in SettingsList.h.
class DictionarySelectionActivity final : public UiListActivity {
 public:
  explicit DictionarySelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("DictionarySelect", renderer, mappedInput) {}

  void onEnter() override;

 private:
  // Index 0 is always "None"; entry i>0 is dictionaries[i-1].
  size_t optionCount() const { return dictionaries.size() + 1; }
  int listCount() const override { return static_cast<int>(optionCount()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  std::vector<DictionaryEntry> dictionaries;
  std::vector<freeink::ui::ListItem> rowItems;
  int activeIndex = 0;
};
