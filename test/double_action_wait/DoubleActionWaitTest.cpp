// When a Short press has to wait out the double-click window: only for a double action that can do
// something where the press lands. A reader-only action falls through to the screen anywhere else,
// so waiting for it there only delays every press and merges two quick presses into one Double.

#include <gtest/gtest.h>

#include "util/DoubleActionWait.h"

TEST(DoubleActionWait, NoConfiguredActionNeverWaits) {
  EXPECT_FALSE(doubleActionNeedsWait(false, false, false));
  EXPECT_FALSE(doubleActionNeedsWait(false, false, true));
  EXPECT_FALSE(doubleActionNeedsWait(false, true, true));
}

TEST(DoubleActionWait, GlobalActionWaitsEverywhere) {
  EXPECT_TRUE(doubleActionNeedsWait(true, false, false));
  EXPECT_TRUE(doubleActionNeedsWait(true, false, true));
}

TEST(DoubleActionWait, ReaderActionWaitsOnlyInTheReader) {
  EXPECT_TRUE(doubleActionNeedsWait(true, true, true));
  EXPECT_FALSE(doubleActionNeedsWait(true, true, false));
}
