#include "WeatherCityResultsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WeatherSettingsStore.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

WeatherCityResultsActivity::WeatherCityResultsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::vector<GeocodingResult> results)
    : UiListActivity("WeatherCityResults", renderer, mappedInput), cities(std::move(results)) {}

const char* WeatherCityResultsActivity::headerTitle() const { return tr(STR_WEATHER_SEARCH_RESULTS); }

// Nothing to pick on an empty search, so Confirm shows no hint; Back still returns.
const char* WeatherCityResultsActivity::footerConfirmLabel() const {
  return cities.empty() ? "" : UiListActivity::footerConfirmLabel();
}

void WeatherCityResultsActivity::drawChrome() {
  UiListActivity::drawChrome();
  if (!cities.empty()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = listContentRect();
  const Rect header = listHeaderRect();
  const int top = header.y + header.height + metrics.verticalSpacing;
  const int bottom = content.y + content.height;
  renderer.drawCenteredText(UI_10_FONT_ID, top + (bottom - top) / 2, tr(STR_NO_ENTRIES));
}

void WeatherCityResultsActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<WeatherCityResultsActivity*>(ctx);
  const GeocodingResult& city = self->cities[index];
  // "name, admin1, country", leaving out the parts the geocoder did not return.
  snprintf(self->rowLabel, sizeof(self->rowLabel), "%s%s%s%s%s", city.name.c_str(), city.admin1.empty() ? "" : ", ",
           city.admin1.c_str(), city.country.empty() ? "" : ", ", city.country.c_str());
  item.label = self->rowLabel;
  item.actionValue = static_cast<int16_t>(index);
}

void WeatherCityResultsActivity::buildScreen(UiScreen& screen) {
  layoutListArea(screen);
  // No rows to lay out: drawChrome() says that nothing matched.
  if (cities.empty()) return;

  fui::ListProps props = listProps(screen);
  props.count = static_cast<uint16_t>(cities.size());
  props.rowProvider = &WeatherCityResultsActivity::provideRow;
  props.rowProviderCtx = this;
  addList(screen, props);
}

void WeatherCityResultsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const GeocodingResult& city = cities[static_cast<size_t>(index)];
  WEATHER_SETTINGS.setLocation(city.latitude, city.longitude, city.name + ", " + city.country);
  WEATHER_SETTINGS.saveToFile();
  ActivityResult picked;  // not cancelled: the weather menu repaints its Location row
  setResult(std::move(picked));
  finish();
}

void WeatherCityResultsActivity::onBackButton() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}
