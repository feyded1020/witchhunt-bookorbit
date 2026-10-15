#pragma once

#include <WeatherData.h>

#include <cstdint>
#include <vector>

#include "activities/UiListActivity.h"

// The places a weather city search matched (at most five: the geocoding query asks for that many).
// Confirm stores the picked place as the weather location and returns; Back returns without
// changing it. An empty search opens it too, so it can say that nothing matched.
class WeatherCityResultsActivity final : public UiListActivity {
 public:
  WeatherCityResultsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::vector<GeocodingResult> results);

  // Opened right after the search with the radio still up for the weather flow, as the weather
  // menu that opens it is: the KOReader sync worker must not take the radio down under it.
  bool usesWifi() const override { return true; }

 private:
  int listCount() const override { return static_cast<int>(cities.size()); }
  const char* headerTitle() const override;
  [[nodiscard]] const char* footerConfirmLabel() const override;
  void drawChrome() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Back cancels: nothing is stored.
  void onBackButton() override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  std::vector<GeocodingResult> cities;
  // The row the list is laying out, as "name, admin1, country". Written by provideRow() on the
  // render task; the list reads it before it asks for the next row.
  char rowLabel[160] = {};
};
