#pragma once

#include <cstddef>

// POSIX TZ string for each CrossPointSettings::TZ_* value.
//
// Indexed by the SETTING, which is persisted to the settings file as a raw byte and is therefore
// a storage contract: TZ_EST has meant 12 since it was written and must go on meaning 12, or an
// update silently moves people to another timezone.
//
// Deliberately NOT an index into HalClock's TIMEZONES[]. That list is data, its order is not a
// contract, and it has already been reordered once: extending it from 19 entries to 51 in
// geographic order left applyTimezone() looking settings up in a table that no longer agreed
// with them, so every zone was wrong -- UTC selected UTC-12, CET selected UTC-11, Eastern
// selected UTC-02. Keeping the strings here means a future reordering of that list cannot reach
// the clock at all.
//
// Strings are the ones this project shipped for these settings before the reorder.
namespace TimezoneBySetting {

inline constexpr const char* TZ[] = {
    "GMT0BST,M3.5.0/1,M10.5.0/2",        // 0  TZ_UTC          UTC+0, BST in summer
    "CET-1CEST,M3.5.0/2,M10.5.0/3",      // 1  TZ_CET          UTC+1, CEST in summer
    "EET-2EEST,M3.5.0/3,M10.5.0/4",      // 2  TZ_EET          UTC+2, EEST in summer
    "MSK-3",                             // 3  TZ_MSK          UTC+3, no DST
    "UTC-4",                             // 4  TZ_UTC_PLUS4    UTC+4, no DST
    "UTC-5:30",                          // 5  TZ_IST          UTC+5:30, no DST
    "UTC-7",                             // 6  TZ_UTC_PLUS7    UTC+7, no DST
    "UTC-8",                             // 7  TZ_UTC_PLUS8    UTC+8, no DST
    "UTC-9",                             // 8  TZ_UTC_PLUS9    UTC+9, no DST
    "AEST-10AEDT,M10.1.0/2,M4.1.0/3",    // 9  TZ_AEST         UTC+10, AEDT in summer
    "NZST-12NZDT,M9.5.0/2,M4.1.0/3",     // 10 TZ_NZST         UTC+12, NZDT in summer
    "UTC+3",                             // 11 TZ_UTC_MINUS3   UTC-3, no DST
    "EST5EDT,M3.2.0/2,M11.1.0/2",        // 12 TZ_EST          UTC-5, EDT in summer
    "CST6CDT,M3.2.0/2,M11.1.0/2",        // 13 TZ_CST          UTC-6, CDT in summer
    "MST7MDT,M3.2.0/2,M11.1.0/2",        // 14 TZ_MST          UTC-7, MDT in summer
    "PST8PDT,M3.2.0/2,M11.1.0/2",        // 15 TZ_PST          UTC-8, PDT in summer
    "AST4ADT,M3.2.0/2,M11.1.0/2",        // 16 TZ_AST_ADT      UTC-4, ADT in summer
    "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3",  // 17 TZ_ACST_ACDT    UTC+9:30, ACDT in summer
    "AKST9AKDT,M3.2.0/2,M11.1.0/2",      // 18 TZ_AKST_AKDT    UTC-9, AKDT in summer
};

inline constexpr size_t COUNT = sizeof(TZ) / sizeof(TZ[0]);

}  // namespace TimezoneBySetting
