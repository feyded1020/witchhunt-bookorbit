#include "Timezones.h"

#include <HalClock.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"

namespace timezones {
namespace {

// Ordered west to east by standard offset. APPEND ONLY (see header).
//
// The named block is lifted verbatim from crosspoint-reader PR #3562 (Justin Mitchell /
// @itsthisjustin), indices and all, so later changes to it merge cleanly. See Timezones.h for
// what else is theirs. The fixed-offset block that follows drops 18 of theirs: offsets
// no country keeps as standard time, either because none ever did or because the one that did
// retired it (Venezuela's -04:30, North Korea's +08:30, Norfolk's +11:30), plus two that only
// ever existed as a DST offset the named entry above already provides (-02:30 Newfoundland,
// +13:30 Chatham).
//
// Two entries depend on a POSIX transition hour above 24 -- Santiago's "/24" (the first Saturday
// of April at 24:00) and Jerusalem's "/26" (the Thursday of week 4 of March plus 26 hours, which
// is how POSIX spells "the Friday before the last Sunday"). Both libcs this code runs on add the
// rule's seconds to the named day's midnight without clamping: glibc on the test host, and
// newlib 4.3.0 on the device, whose tzcalc_limits.c computes
// `change = days * SECSPERDAY + rule->s + rule->offset` into an int. TimezoneTest pins the
// resulting transition instants, so a libc that ever starts clamping is caught rather than
// silently landing a day early.
constexpr TimezoneInfo TABLE[] = {
    {"Midway", "SST11", -44},
    {"Honolulu", "HST10", -40},
    {"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0", -36},
    {"Los Angeles / Vancouver", "PST8PDT,M3.2.0,M11.1.0", -32},
    {"Denver / Edmonton", "MST7MDT,M3.2.0,M11.1.0", -28},
    {"Phoenix", "MST7", -28},
    {"Chicago / Winnipeg", "CST6CDT,M3.2.0,M11.1.0", -24},
    {"Mexico City", "CST6", -24},
    {"New York / Toronto", "EST5EDT,M3.2.0,M11.1.0", -20},
    {"Bogota / Lima", "COT5", -20},
    {"Halifax", "AST4ADT,M3.2.0,M11.1.0", -16},
    {"Caracas / La Paz", "VET4", -16},
    {"Santiago", "CLT4CLST,M9.1.6/24,M4.1.6/24", -16},
    {"St. John's", "NST3:30NDT,M3.2.0,M11.1.0", -14},
    {"Buenos Aires / Montevideo", "ART3", -12},
    {"Sao Paulo", "BRT3", -12},
    {"Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1", -4},
    {"UTC", "UTC0", 0},
    {"London / Dublin / Lisbon", "GMT0BST,M3.5.0/1,M10.5.0", 0},
    {"Berlin / Paris / Madrid / Rome", "CET-1CEST,M3.5.0,M10.5.0/3", 4},
    {"Lagos / Algiers", "WAT-1", 4},
    {"Athens / Helsinki / Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4", 8},
    {"Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24", 8},
    {"Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0", 8},
    {"Johannesburg", "SAST-2", 8},
    {"Moscow / Istanbul / Riyadh", "MSK-3", 12},
    {"Nairobi", "EAT-3", 12},
    {"Tehran", "IRST-3:30", 14},
    {"Dubai / Tbilisi", "GST-4", 16},
    {"Kabul", "AFT-4:30", 18},
    {"Karachi / Tashkent", "PKT-5", 20},
    {"India / Colombo", "IST-5:30", 22},
    {"Kathmandu", "NPT-5:45", 23},
    {"Dhaka / Almaty", "BST-6", 24},
    {"Yangon", "MMT-6:30", 26},
    {"Bangkok / Jakarta / Hanoi", "ICT-7", 28},
    {"China / Hong Kong / Taipei", "CST-8", 32},
    {"Singapore / Manila / Kuala Lumpur", "SGT-8", 32},
    {"Perth", "AWST-8", 32},
    {"Tokyo / Seoul", "JST-9", 36},
    {"Darwin", "ACST-9:30", 38},
    {"Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3", 38},
    {"Brisbane / Guam", "AEST-10", 40},
    {"Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3", 40},
    {"Honiara / Noumea", "SBT-11", 44},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3", 48},
    {"Fiji", "FJT-12", 48},
    {"Nuku'alofa", "TOT-13", 52},
    {"Kiritimati", "LINT-14", 56},
    // Fixed offsets, no DST — the manual fallback for zones the named list
    // misses. Note the POSIX offset sign is inverted relative to the label.
    {"UTC-12:00", "UTC12", -48},
    {"UTC-11:00", "UTC11", -44},
    {"UTC-10:00", "UTC10", -40},
    {"UTC-09:30", "UTC9:30", -38},
    {"UTC-09:00", "UTC9", -36},
    {"UTC-08:00", "UTC8", -32},
    {"UTC-07:00", "UTC7", -28},
    {"UTC-06:00", "UTC6", -24},
    {"UTC-05:00", "UTC5", -20},
    {"UTC-04:00", "UTC4", -16},
    {"UTC-03:30", "UTC3:30", -14},
    {"UTC-03:00", "UTC3", -12},
    {"UTC-02:00", "UTC2", -8},
    {"UTC-01:00", "UTC1", -4},
    {"UTC+01:00", "UTC-1", 4},
    {"UTC+02:00", "UTC-2", 8},
    {"UTC+03:00", "UTC-3", 12},
    {"UTC+03:30", "UTC-3:30", 14},
    {"UTC+04:00", "UTC-4", 16},
    {"UTC+04:30", "UTC-4:30", 18},
    {"UTC+05:00", "UTC-5", 20},
    {"UTC+05:30", "UTC-5:30", 22},
    {"UTC+05:45", "UTC-5:45", 23},
    {"UTC+06:00", "UTC-6", 24},
    {"UTC+06:30", "UTC-6:30", 26},
    {"UTC+07:00", "UTC-7", 28},
    {"UTC+08:00", "UTC-8", 32},
    {"UTC+08:45", "UTC-8:45", 35},
    {"UTC+09:00", "UTC-9", 36},
    {"UTC+09:30", "UTC-9:30", 38},
    {"UTC+10:00", "UTC-10", 40},
    {"UTC+10:30", "UTC-10:30", 42},
    {"UTC+11:00", "UTC-11", 44},
    {"UTC+12:00", "UTC-12", 48},
    {"UTC+12:45", "UTC-12:45", 51},
    {"UTC+13:00", "UTC-13", 52},
    {"UTC+14:00", "UTC-14", 56},
};
constexpr size_t TABLE_COUNT = sizeof(TABLE) / sizeof(TABLE[0]);
constexpr uint8_t UTC_INDEX = 17;
static_assert(TABLE[UTC_INDEX].stdOffsetQ == 0, "UTC_INDEX must point at the UTC entry");
static_assert(TABLE_COUNT < 255, "255 is the 'never chosen' sentinel in clockTimezone");

// Where each value of the retired TIMEZONE enum lands in the table above.
//
// This is the whole migration. Those 19 values have been persisted in the settings file as raw
// bytes since they were written, so they cannot simply be reinterpreted as indices into a
// different, longer list -- that is precisely the bug that once made UTC select UTC-12 and US
// Eastern select UTC-02.
//
// Every mapping below targets an entry whose POSIX rule is the SAME rule the old value applied,
// not merely a nearby one. Where the strings differ textually they differ only in transition
// hours POSIX defaults to anyway (an omitted "/2"), so nobody's clock moves by a second.
// TimezoneTest proves that by resolving both strings and comparing offsets in January and July.
constexpr uint8_t LEGACY_TO_TABLE[] = {
    18,  // 0  TZ_UTC         GMT0BST        -> London / Dublin / Lisbon (carries BST, as it did)
    19,  // 1  TZ_CET         CET-1CEST      -> Berlin / Paris / Madrid / Rome
    21,  // 2  TZ_EET         EET-2EEST      -> Athens / Helsinki / Kyiv
    25,  // 3  TZ_MSK         MSK-3          -> Moscow / Istanbul / Riyadh
    28,  // 4  TZ_UTC_PLUS4   UTC-4          -> Dubai / Tbilisi
    31,  // 5  TZ_IST         UTC-5:30       -> India / Colombo
    35,  // 6  TZ_UTC_PLUS7   UTC-7          -> Bangkok / Jakarta / Hanoi
    36,  // 7  TZ_UTC_PLUS8   UTC-8          -> China / Hong Kong / Taipei
    39,  // 8  TZ_UTC_PLUS9   UTC-9          -> Tokyo / Seoul
    43,  // 9  TZ_AEST        AEST-10AEDT    -> Sydney / Melbourne
    45,  // 10 TZ_NZST        NZST-12NZDT    -> Auckland
    14,  // 11 TZ_UTC_MINUS3  UTC+3          -> Buenos Aires / Montevideo
    8,   // 12 TZ_EST         EST5EDT        -> New York / Toronto
    6,   // 13 TZ_CST         CST6CDT        -> Chicago / Winnipeg
    4,   // 14 TZ_MST         MST7MDT        -> Denver / Edmonton
    3,   // 15 TZ_PST         PST8PDT        -> Los Angeles / Vancouver
    10,  // 16 TZ_AST_ADT     AST4ADT        -> Halifax
    41,  // 17 TZ_ACST_ACDT   ACST-9:30ACDT  -> Adelaide
    2,   // 18 TZ_AKST_AKDT   AKST9AKDT      -> Anchorage
};
constexpr size_t LEGACY_COUNT = sizeof(LEGACY_TO_TABLE) / sizeof(LEGACY_TO_TABLE[0]);
static_assert(LEGACY_COUNT == static_cast<size_t>(CrossPointSettings::TZ_LEGACY_COUNT),
              "every retired TIMEZONE value needs a destination, or it migrates to the wrong zone");

}  // namespace

const TimezoneInfo* table() { return TABLE; }
size_t count() { return TABLE_COUNT; }
uint8_t utcIndex() { return UTC_INDEX; }

uint8_t indexForLegacy(const uint8_t legacyTimezone) {
  if (legacyTimezone < LEGACY_COUNT) return LEGACY_TO_TABLE[legacyTimezone];
  return UTC_INDEX;
}

uint8_t activeIndex() {
  if (SETTINGS.clockTimezone < TABLE_COUNT) return SETTINGS.clockTimezone;
  // Never chosen on this firmware: fall back to whatever the retired enum held. A device that
  // never touched the setting has timeZone == TZ_UTC == 0, which maps to London -- the rule it
  // has been applying all along, BST included. Anyone who actually wanted UTC can now pick it,
  // which was never possible before: the old TZ_UTC was the UK, despite the name.
  return indexForLegacy(SETTINGS.timeZone);
}

uint8_t indexByName(const char* name) {
  if (name == nullptr) return 255;
  for (size_t i = 0; i < TABLE_COUNT; i++) {
    if (strcmp(TABLE[i].name, name) == 0) return static_cast<uint8_t>(i);
  }
  return 255;
}

void applyToClock() { HalClock::applyTimezone(TABLE[activeIndex()].posixTz); }

}  // namespace timezones
