#pragma once

#include <cstddef>
#include <cstdint>

// Curated IANA-style timezone list for the clock. Each entry carries the POSIX TZ rule newlib
// applies in localtime_r(), so zones with daylight saving get the correct wall time year-round.
//
// Ported from crosspoint-reader PR #3562 ("feat: Replace UTC offset with timezone and DST
// settings", Justin Mitchell / @itsthisjustin).
//
// THEIRS: this struct, the zone table and its ordering, and the idea of a picker over a table
// rather than an enum. The named block is kept verbatim, indices and all, so later changes to it
// merge cleanly.
//
// OURS: the migration. Their PR replaced a quarter-hour UTC offset setting; this one replaces the
// 19-value TIMEZONE enum this firmware has been persisting since it shipped, which needs a
// value-by-value map -- see LEGACY_TO_TABLE in Timezones.cpp. Also ours: dropping 18 fixed-offset
// rows no country keeps as standard time, and the test suite. Their forced-DST setting is NOT
// ported; that is a separate feature.
//
// APPEND ONLY: CrossPointSettings::clockTimezone persists an index into this table, so reordering
// or removing entries retargets users' saved zones. This firmware has made that mistake twice
// already -- once by reordering a table applyTimezone() indexed into (every zone silently became
// a different one), and once by letting a settings-list label array fall three entries short of
// the enum (the three zones past the end reset to the default on every boot). Both are one list
// of timezones disagreeing with another. There is now exactly one list, and the settings row
// takes its options straight from it.
struct TimezoneInfo {
  const char* name;     // shown in the picker; city/region names stay untranslated
  const char* posixTz;  // POSIX TZ rule, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
  // Standard (non-DST) offset in quarter hours. Upstream uses it for display and to migrate
  // their old quarter-hour setting; here it is a CHECK on the rule beside it. Daylight saving
  // only ever moves a clock forward, so the standard offset is the smaller of the two seasons
  // for every zone, and TimezoneTest resolves all 86 rules and compares. That is what would
  // catch a mistyped rule -- the sort of thing that reads fine and is an hour out.
  int16_t stdOffsetQ;
};

namespace timezones {

const TimezoneInfo* table();
size_t count();

// Index of plain UTC. Distinct from London: the UK entry carries BST and is +1 in summer.
uint8_t utcIndex();

// SETTINGS.clockTimezone when it has been set, otherwise the legacy TIMEZONE enum value in
// SETTINGS.timeZone mapped through LEGACY_TO_TABLE. 255 in the setting means "never chosen".
uint8_t activeIndex();

// Where a value of the retired TIMEZONE enum lands in the table. Out-of-range input returns the
// UTC index. Pure, so the migration can be checked without a settings object.
uint8_t indexForLegacy(uint8_t legacyTimezone);

// Index of the entry whose name is exactly `name`, or 255 if there is none.
//
// Lets callers that need to NAME a zone -- timezone detection, chiefly -- refer to table entries
// by the string in the table rather than by a magic index. A renamed or removed entry then fails
// to resolve, which is recoverable, instead of silently selecting whatever moved into that slot,
// which is the failure this file exists to prevent.
uint8_t indexByName(const char* name);

// Push the active zone's TZ rule into HalClock. Call once at boot after settings load, and again
// whenever clockTimezone changes.
void applyToClock();

}  // namespace timezones
