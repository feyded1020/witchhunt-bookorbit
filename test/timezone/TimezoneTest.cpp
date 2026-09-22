// What each timezone SETTING actually does to the clock.
//
// This exists because nothing did. The setting value is an index that used to line up with a
// table in HalClock.cpp, that table was later extended from 19 entries to 51 and reordered
// geographically, and nothing caught it: every timezone silently became a different one. UTC
// selected UTC-12, CET selected UTC-11, US Eastern selected UTC-02. The firmware kept showing
// the right timezone NAME while applying the wrong rules.
//
// A POSIX TZ string is exactly as testable on a laptop as on the device -- setenv, tzset,
// localtime_r -- so the mapping is pinned here rather than trusted to review.

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

#include "TimezoneBySetting.h"

namespace {

// Two instants well clear of every transition in the table: mid-January and mid-July.
constexpr time_t JANUARY = 1736899200;  // 2025-01-15 00:00:00 UTC
constexpr time_t JULY = 1752537600;     // 2025-07-15 00:00:00 UTC

// Seconds east of UTC that `tzString` resolves to at `when`.
int offsetAt(const char* tzString, time_t when) {
  setenv("TZ", tzString, 1);
  tzset();
  struct tm local{};
  localtime_r(&when, &local);
  return static_cast<int>(local.tm_gmtoff);
}

struct Expectation {
  int setting;
  const char* name;
  int winterOffsetHours100;  // hundredths of an hour, so 5:30 is 550
  int summerOffsetHours100;
};

// Hundredths of an hour keeps half-hour zones exact without floating point.
constexpr Expectation EXPECTED[] = {
    {0, "TZ_UTC", 0, 100},
    {1, "TZ_CET", 100, 200},
    {2, "TZ_EET", 200, 300},
    {3, "TZ_MSK", 300, 300},
    {4, "TZ_UTC_PLUS4", 400, 400},
    {5, "TZ_IST", 550, 550},
    {6, "TZ_UTC_PLUS7", 700, 700},
    {7, "TZ_UTC_PLUS8", 800, 800},
    {8, "TZ_UTC_PLUS9", 900, 900},
    {9, "TZ_AEST", 1100, 1000},
    {10, "TZ_NZST", 1300, 1200},
    {11, "TZ_UTC_MINUS3", -300, -300},
    {12, "TZ_EST", -500, -400},
    {13, "TZ_CST", -600, -500},
    {14, "TZ_MST", -700, -600},
    {15, "TZ_PST", -800, -700},
    {16, "TZ_AST_ADT", -400, -300},
    {17, "TZ_ACST_ACDT", 1050, 950},
    {18, "TZ_AKST_AKDT", -900, -800},
};

int toHundredths(int seconds) { return seconds * 100 / 3600; }

}  // namespace

TEST(Timezone, EverySettingHasAString) {
  EXPECT_EQ(TimezoneBySetting::COUNT, sizeof(EXPECTED) / sizeof(EXPECTED[0]));
  for (size_t i = 0; i < TimezoneBySetting::COUNT; ++i) {
    EXPECT_NE(TimezoneBySetting::TZ[i], nullptr) << "setting " << i;
    EXPECT_NE(TimezoneBySetting::TZ[i][0], '\0') << "setting " << i;
  }
}

// The regression this file exists for: a setting resolving to the wrong part of the world.
TEST(Timezone, SettingResolvesToItsOwnOffset) {
  for (const auto& e : EXPECTED) {
    ASSERT_LT(static_cast<size_t>(e.setting), TimezoneBySetting::COUNT) << e.name;
    const char* tz = TimezoneBySetting::TZ[e.setting];
    EXPECT_EQ(toHundredths(offsetAt(tz, JANUARY)), e.winterOffsetHours100) << e.name << " in January (" << tz << ")";
    EXPECT_EQ(toHundredths(offsetAt(tz, JULY)), e.summerOffsetHours100) << e.name << " in July (" << tz << ")";
  }
}

// US Eastern is the one we can check against a real device, so it gets named explicitly.
TEST(Timezone, UsEasternIsMinusFiveAndMinusFourInSummer) {
  const char* tz = TimezoneBySetting::TZ[12];
  EXPECT_EQ(offsetAt(tz, JANUARY), -5 * 3600);
  EXPECT_EQ(offsetAt(tz, JULY), -4 * 3600);
}
