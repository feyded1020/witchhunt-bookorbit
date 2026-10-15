#pragma once

#include <string>

#include "../MenuListActivity.h"

/**
 * Settings submenu for weather configuration.
 * Supports city search via geocoding, manual lat/lon entry, and unit selection.
 * The search's matches open as their own list, WeatherCityResultsActivity.
 */
class WeatherSettingsActivity final : public MenuListActivity {
 public:
  explicit WeatherSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : MenuListActivity("WeatherSettings", renderer, mappedInput) {
    buildMenuItems();
  }

  bool usesWifi() const override { return true; }
  // MenuListActivity draws its rows through drawMenuList(rect) and has no render() of its own:
  // the rect, under this screen's header, is only known here.
  void render(RenderLock&&) override;

 private:
  void buildMenuItems();
  void onActionSelected(int index) override;
  std::string getItemValueString(int index) const override;
  void onSettingToggled(int index) override;

  void launchCitySearch();
  // Searches for `query` and opens the matches as their own list.
  void showCityResults(const std::string& query);
  void launchLatitudeEntry();
  void launchLongitudeEntry();
};
