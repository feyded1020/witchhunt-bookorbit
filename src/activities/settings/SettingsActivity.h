#pragma once
#include <I18n.h>

#include <array>
#include <string>
#include <vector>

#include "SettingInfo.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class SettingsActivity final : public Activity, protected UiAppHost {
  ButtonNavigator buttonNavigator;
  freeink::ui::ListNav listNav;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  static constexpr int categoryCount = 4;
  // constexpr, not `static const` with an out-of-line definition: a compile-time table has no
  // reason to occupy .data and be initialized at startup when it can sit in flash.
  static constexpr StrId categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                         StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

  std::vector<SettingInfo::SubmenuData> submenuData;
  bool needsHalfRefresh = false;

  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  [[nodiscard]] bool isListItemSelectable(int settingIdx) const;
  void syncListSelection();

  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  static constexpr freeink::ui::ActionId ACTION_TAB = 2;
  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowValues;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  static void settingsScreen(UiScreen& screen, void* user);
  static void onTabEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildSettingsScreen(UiScreen& screen);
  void materializeListWindow();
  void handleRowTouch(int index);

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  // Index 0 of selectedSettingIndex is the category tab, so list row i maps to i + 1.
  ListRowTap::Result selectListRow(int index) override;
  bool pageList(ListPageDirection direction) override;
  void loop() override;
  void render(RenderLock&&) override;
};
