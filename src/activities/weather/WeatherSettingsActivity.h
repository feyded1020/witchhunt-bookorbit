#pragma once

#include <WeatherData.h>

#include <string>
#include <vector>

#include "../MenuListActivity.h"

/**
 * Settings submenu for weather configuration.
 * Supports city search via geocoding, manual lat/lon entry, and unit selection.
 */
class WeatherSettingsActivity final : public MenuListActivity {
 public:
  explicit WeatherSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : MenuListActivity("WeatherSettings", renderer, mappedInput) {
    buildMenuItems();
  }

  void onEnter() override;
  bool usesWifi() const override { return true; }
  // Tap on a list row -> move the selection there; ActivityManager synthesizes Confirm. Serves
  // BOTH of this screen's lists: the settings menu, and the city-search results, which are not
  // menuItems and so cannot be validated by the MenuListActivity version.
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<GeocodingResult> searchResults;
  bool showingSearchResults = false;
  // The results list's own selection and navigator. The inherited ones belong to the settings
  // menu: its navigator carries the menu's selectable predicate and item count, so stepping the
  // results with it walked the MENU's rows -- past the last city into invisible entries (#342).
  int resultIndex = 0;
  ButtonNavigator resultsNavigator;
  std::string searchQuery;

  void buildMenuItems();
  void onActionSelected(int index) override;
  std::string getItemValueString(int index) const override;
  void onBackPressed() override;
  void onSettingToggled(int index) override;

  void launchCitySearch();
  void launchLatitudeEntry();
  void launchLongitudeEntry();
};
