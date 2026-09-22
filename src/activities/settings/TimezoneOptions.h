#pragma once

#include <cstdint>

#include "CrossPointSettings.h"
#include "SettingInfo.h"
#include "util/Timezones.h"

// The ONE place the timezone settings row is built.
//
// There used to be two: SettingsList.h (web UI and, crucially, persistence) and
// ClockSettingsActivity (the device screen). They listed different numbers of options -- 16
// against 19 -- and because JsonSettingsIO bounds a loaded ENUM on the SETTINGS LIST row's
// option count, the three zones only the device offered saved correctly and then reset to the
// default on the next boot.
//
// The row's options are not a list at all now: they are the table in util/Timezones.cpp, read
// through SettingInfo's flash-resident literal labels. There is nothing left to fall out of
// step with, and the bound JsonSettingsIO applies is the table's own length.
namespace TimezoneOptions {

// Builds the timezone row for `category`. Used by the device Clock screen and by the shared
// settings list the web UI and persistence read.
inline SettingInfo make(const StrId category) {
  // No enumValues: every option's label comes from the table, and city and region names are not
  // translated -- "New York / Toronto" reads the same in every language, and is easier to find
  // than the "US Eastern (EST/EDT)" it replaces.
  return SettingInfo::Enum(StrId::STR_TIMEZONE, &CrossPointSettings::clockTimezone, {}, "clockTimezone", category)
      // Labels come straight out of the table, one call per row rendered, with nothing cached
      // in between -- the names are string literals in flash, so there is nothing to cache.
      .withLiteralOptions([](const uint8_t index) { return timezones::table()[index].name; },
                          static_cast<uint8_t>(timezones::count()))
      // 86 options is far past what cycling on Confirm can serve, which is what the full-screen
      // picker exists for.
      .withSelectorActivity();
}

}  // namespace TimezoneOptions
