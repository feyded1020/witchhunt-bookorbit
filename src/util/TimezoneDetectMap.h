#pragma once

#include <cstdint>
#include <string>

#include "util/Timezones.h"

// IANA zone name -> an entry of the table in util/Timezones.cpp.
//
// A header of its own, rather than a static block inside DetectTimezoneActivity.cpp, so the
// mapping can be checked without a network stack: TimezoneTest asserts that every destination
// named here still exists in the table, which is the one way this file can rot.
namespace timezone_detect {

// What the detection service's IANA zone name means in terms of the table in util/Timezones.cpp.
//
// Entries name their destination rather than indexing it: a renamed or removed table entry then
// fails to resolve and detection reports "unsupported", instead of silently selecting whichever
// zone happens to occupy that slot now. TimezoneTest checks that every name here still exists.
//
// EXACT matches only, tried in order; the Europe/ prefix rule below is the single fallback.
struct IanaMapping {
  const char* iana;
  const char* zone;
};

constexpr IanaMapping IANA_MAP[] = {
    // Plain UTC. This used to resolve to the old TZ_UTC, which was "GMT0BST" -- the UK, with
    // British Summer Time. A device reporting UTC was put an hour ahead every summer.
    {"UTC", "UTC"},
    {"Etc/UTC", "UTC"},
    {"Etc/GMT", "UTC"},
    {"Etc/Greenwich", "UTC"},

    // UTC+0 WITH summer time. Lisbon and Dublin belong here and used to fall through the
    // Europe/ prefix rule to CET, an hour off all year.
    {"Europe/London", "London / Dublin / Lisbon"},
    {"Europe/Dublin", "London / Dublin / Lisbon"},
    {"Europe/Lisbon", "London / Dublin / Lisbon"},
    {"Europe/Guernsey", "London / Dublin / Lisbon"},
    {"Europe/Isle_of_Man", "London / Dublin / Lisbon"},
    {"Europe/Jersey", "London / Dublin / Lisbon"},
    {"Atlantic/Canary", "London / Dublin / Lisbon"},
    {"Atlantic/Faroe", "London / Dublin / Lisbon"},
    {"Atlantic/Reykjavik", "UTC"},
    {"Atlantic/Azores", "Azores"},

    {"Europe/Athens", "Athens / Helsinki / Kyiv"},
    {"Europe/Bucharest", "Athens / Helsinki / Kyiv"},
    {"Europe/Helsinki", "Athens / Helsinki / Kyiv"},
    {"Europe/Kiev", "Athens / Helsinki / Kyiv"},
    {"Europe/Kyiv", "Athens / Helsinki / Kyiv"},
    {"Europe/Vilnius", "Athens / Helsinki / Kyiv"},
    {"Europe/Riga", "Athens / Helsinki / Kyiv"},
    {"Europe/Tallinn", "Athens / Helsinki / Kyiv"},
    {"Europe/Sofia", "Athens / Helsinki / Kyiv"},
    {"Europe/Chisinau", "Athens / Helsinki / Kyiv"},
    {"Asia/Beirut", "Athens / Helsinki / Kyiv"},
    {"Asia/Nicosia", "Athens / Helsinki / Kyiv"},

    {"Africa/Cairo", "Cairo"},
    {"Asia/Jerusalem", "Jerusalem"},
    {"Asia/Tel_Aviv", "Jerusalem"},
    {"Africa/Johannesburg", "Johannesburg"},
    {"Africa/Maputo", "Johannesburg"},
    {"Africa/Harare", "Johannesburg"},
    {"Africa/Lusaka", "Johannesburg"},
    {"Africa/Gaborone", "Johannesburg"},
    {"Africa/Windhoek", "Johannesburg"},

    {"Europe/Moscow", "Moscow / Istanbul / Riyadh"},
    {"Europe/Istanbul", "Moscow / Istanbul / Riyadh"},
    {"Asia/Istanbul", "Moscow / Istanbul / Riyadh"},
    {"Europe/Minsk", "Moscow / Istanbul / Riyadh"},
    {"Europe/Kirov", "Moscow / Istanbul / Riyadh"},
    {"Europe/Simferopol", "Moscow / Istanbul / Riyadh"},
    {"Asia/Riyadh", "Moscow / Istanbul / Riyadh"},
    {"Asia/Qatar", "Moscow / Istanbul / Riyadh"},
    {"Asia/Kuwait", "Moscow / Istanbul / Riyadh"},
    {"Asia/Bahrain", "Moscow / Istanbul / Riyadh"},
    {"Asia/Baghdad", "Moscow / Istanbul / Riyadh"},
    {"Asia/Aden", "Moscow / Istanbul / Riyadh"},
    {"Africa/Nairobi", "Nairobi"},
    {"Africa/Addis_Ababa", "Nairobi"},
    {"Africa/Dar_es_Salaam", "Nairobi"},
    {"Africa/Kampala", "Nairobi"},
    {"Africa/Mogadishu", "Nairobi"},
    {"Africa/Khartoum", "Johannesburg"},

    {"Asia/Tehran", "Tehran"},
    {"Asia/Dubai", "Dubai / Tbilisi"},
    {"Asia/Muscat", "Dubai / Tbilisi"},
    {"Asia/Tbilisi", "Dubai / Tbilisi"},
    {"Asia/Baku", "Dubai / Tbilisi"},
    {"Asia/Yerevan", "Dubai / Tbilisi"},
    {"Europe/Samara", "Dubai / Tbilisi"},
    {"Asia/Kabul", "Kabul"},
    {"Asia/Karachi", "Karachi / Tashkent"},
    {"Asia/Tashkent", "Karachi / Tashkent"},
    {"Asia/Ashgabat", "Karachi / Tashkent"},
    {"Asia/Dushanbe", "Karachi / Tashkent"},
    {"Asia/Yekaterinburg", "Karachi / Tashkent"},
    {"Asia/Kolkata", "India / Colombo"},
    {"Asia/Calcutta", "India / Colombo"},
    {"Asia/Colombo", "India / Colombo"},
    {"Asia/Kathmandu", "Kathmandu"},
    {"Asia/Katmandu", "Kathmandu"},
    {"Asia/Dhaka", "Dhaka / Almaty"},
    {"Asia/Almaty", "Dhaka / Almaty"},
    {"Asia/Bishkek", "Dhaka / Almaty"},
    {"Asia/Thimphu", "Dhaka / Almaty"},
    {"Asia/Omsk", "Dhaka / Almaty"},
    {"Asia/Yangon", "Yangon"},
    {"Asia/Rangoon", "Yangon"},

    {"Asia/Bangkok", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Jakarta", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Ho_Chi_Minh", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Saigon", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Phnom_Penh", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Vientiane", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Krasnoyarsk", "Bangkok / Jakarta / Hanoi"},
    {"Asia/Shanghai", "China / Hong Kong / Taipei"},
    {"Asia/Hong_Kong", "China / Hong Kong / Taipei"},
    {"Asia/Taipei", "China / Hong Kong / Taipei"},
    {"Asia/Macau", "China / Hong Kong / Taipei"},
    {"Asia/Chongqing", "China / Hong Kong / Taipei"},
    {"Asia/Irkutsk", "China / Hong Kong / Taipei"},
    {"Asia/Singapore", "Singapore / Manila / Kuala Lumpur"},
    {"Asia/Manila", "Singapore / Manila / Kuala Lumpur"},
    {"Asia/Kuala_Lumpur", "Singapore / Manila / Kuala Lumpur"},
    {"Asia/Brunei", "Singapore / Manila / Kuala Lumpur"},
    {"Asia/Makassar", "Singapore / Manila / Kuala Lumpur"},
    {"Australia/Perth", "Perth"},
    {"Asia/Tokyo", "Tokyo / Seoul"},
    {"Asia/Seoul", "Tokyo / Seoul"},
    {"Asia/Pyongyang", "Tokyo / Seoul"},
    {"Asia/Yakutsk", "Tokyo / Seoul"},
    {"Asia/Jayapura", "Tokyo / Seoul"},

    {"Australia/Darwin", "Darwin"},
    {"Australia/Adelaide", "Adelaide"},
    {"Australia/Broken_Hill", "Adelaide"},
    {"Australia/Brisbane", "Brisbane / Guam"},
    {"Australia/Lindeman", "Brisbane / Guam"},
    {"Pacific/Guam", "Brisbane / Guam"},
    {"Pacific/Port_Moresby", "Brisbane / Guam"},
    {"Asia/Vladivostok", "Brisbane / Guam"},
    {"Australia/Sydney", "Sydney / Melbourne"},
    {"Australia/Melbourne", "Sydney / Melbourne"},
    {"Australia/Hobart", "Sydney / Melbourne"},
    {"Australia/Canberra", "Sydney / Melbourne"},
    {"Pacific/Guadalcanal", "Honiara / Noumea"},
    {"Pacific/Noumea", "Honiara / Noumea"},
    {"Asia/Magadan", "Honiara / Noumea"},
    {"Pacific/Auckland", "Auckland"},
    {"Pacific/Fiji", "Fiji"},
    {"Asia/Kamchatka", "Fiji"},
    {"Pacific/Tongatapu", "Nuku'alofa"},
    {"Pacific/Apia", "Nuku'alofa"},
    {"Pacific/Kiritimati", "Kiritimati"},
    {"Pacific/Midway", "Midway"},
    {"Pacific/Pago_Pago", "Midway"},
    {"Pacific/Niue", "Midway"},
    {"Pacific/Honolulu", "Honolulu"},
    {"Pacific/Tahiti", "Honolulu"},
    {"Pacific/Rarotonga", "Honolulu"},

    {"America/Anchorage", "Anchorage"},
    {"America/Juneau", "Anchorage"},
    {"America/Nome", "Anchorage"},
    {"America/Sitka", "Anchorage"},
    {"America/Yakutat", "Anchorage"},
    {"America/Metlakatla", "Anchorage"},
    {"America/Los_Angeles", "Los Angeles / Vancouver"},
    {"America/Vancouver", "Los Angeles / Vancouver"},
    {"America/Tijuana", "Los Angeles / Vancouver"},
    {"America/Denver", "Denver / Edmonton"},
    {"America/Edmonton", "Denver / Edmonton"},
    {"America/Boise", "Denver / Edmonton"},
    {"America/Phoenix", "Phoenix"},
    {"America/Hermosillo", "Phoenix"},
    {"America/Chicago", "Chicago / Winnipeg"},
    {"America/Winnipeg", "Chicago / Winnipeg"},
    // Mexico dropped daylight saving in October 2022; Saskatchewan never had it.
    {"America/Mexico_City", "Mexico City"},
    {"America/Monterrey", "Mexico City"},
    {"America/Merida", "Mexico City"},
    {"America/Regina", "Mexico City"},
    {"America/Guatemala", "Mexico City"},
    {"America/Costa_Rica", "Mexico City"},
    {"America/El_Salvador", "Mexico City"},
    {"America/Tegucigalpa", "Mexico City"},
    {"America/Managua", "Mexico City"},
    {"America/New_York", "New York / Toronto"},
    {"America/Toronto", "New York / Toronto"},
    {"America/Detroit", "New York / Toronto"},
    {"America/Montreal", "New York / Toronto"},
    {"America/Bogota", "Bogota / Lima"},
    {"America/Lima", "Bogota / Lima"},
    {"America/Guayaquil", "Bogota / Lima"},
    {"America/Panama", "Bogota / Lima"},
    {"America/Jamaica", "Bogota / Lima"},
    {"America/Halifax", "Halifax"},
    {"America/Glace_Bay", "Halifax"},
    {"America/Moncton", "Halifax"},
    {"America/Thule", "Halifax"},
    {"Atlantic/Bermuda", "Halifax"},
    {"America/Caracas", "Caracas / La Paz"},
    {"America/La_Paz", "Caracas / La Paz"},
    {"America/Santo_Domingo", "Caracas / La Paz"},
    {"America/Puerto_Rico", "Caracas / La Paz"},
    {"America/Guyana", "Caracas / La Paz"},
    {"America/Santiago", "Santiago"},
    {"America/St_Johns", "St. John's"},
    {"America/Argentina/Buenos_Aires", "Buenos Aires / Montevideo"},
    {"America/Montevideo", "Buenos Aires / Montevideo"},
    {"America/Asuncion", "Buenos Aires / Montevideo"},
    {"America/Sao_Paulo", "Sao Paulo"},
    {"America/Bahia", "Sao Paulo"},
    {"America/Fortaleza", "Sao Paulo"},
    {"America/Recife", "Sao Paulo"},

    {"Africa/Lagos", "Lagos / Algiers"},
    {"Africa/Algiers", "Lagos / Algiers"},
    {"Africa/Tunis", "Lagos / Algiers"},
    {"Africa/Luanda", "Lagos / Algiers"},
    {"Africa/Kinshasa", "Lagos / Algiers"},
    {"Africa/Douala", "Lagos / Algiers"},
    {"Africa/Casablanca", "Lagos / Algiers"},
    {"Africa/Accra", "UTC"},
    {"Africa/Abidjan", "UTC"},
    {"Africa/Dakar", "UTC"},
    {"Africa/Bamako", "UTC"},
};

// Everything else under Europe/ is Central European Time. Applied only after the exact matches
// above, which is what keeps London, Lisbon, Athens and Moscow out of it.
constexpr char EUROPE_PREFIX[] = "Europe/";
constexpr char EUROPE_FALLBACK[] = "Berlin / Paris / Madrid / Rome";

bool mapIanaTimezone(const std::string& tz, uint8_t& outIndex) {
  for (const auto& mapping : IANA_MAP) {
    if (tz == mapping.iana) {
      const uint8_t index = timezones::indexByName(mapping.zone);
      if (index == 255) return false;  // the table no longer has it; better unset than wrong
      outIndex = index;
      return true;
    }
  }
  if (tz.rfind(EUROPE_PREFIX, 0) == 0) {
    const uint8_t index = timezones::indexByName(EUROPE_FALLBACK);
    if (index == 255) return false;
    outIndex = index;
    return true;
  }
  return false;
}

}  // namespace timezone_detect
