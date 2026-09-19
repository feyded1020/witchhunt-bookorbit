#include <gtest/gtest.h>

#include <set>
#include <string>

#include "CrossPointSettings.h"

namespace {

using S = CrossPointSettings;

// The invariant the settings UI depends on: a font-size row's option INDEX is its stored value,
// so the enum has to run in ascending pixel order or the picker lists the sizes out of order.
// That was tolerable while the labels read "Small / Medium / Tiny"; it is not once they read
// "12pt / 14pt / 10pt". FONT_SIZE was renumbered to make it true -- see FONT_SIZE_ORDER_VERSION.
TEST(FontSizeLadder, EnumValueIsLadderPosition) {
  for (int i = 0; i < S::FONT_SIZE_RUNG_COUNT; ++i) {
    EXPECT_EQ(i, S::FONT_SIZE_RUNGS[i].size) << "rung " << i << " does not sit at its own enum value";
  }
}

TEST(FontSizeLadder, PointSizesAscend) {
  for (int i = 1; i < S::FONT_SIZE_RUNG_COUNT; ++i) {
    EXPECT_LT(S::FONT_SIZE_RUNGS[i - 1].points, S::FONT_SIZE_RUNGS[i].points);
  }
}

TEST(FontSizeLadder, StepsUpThroughEveryVisualSize) {
  EXPECT_EQ(S::SMALL, S::stepFontSize(S::TINY, 1));
  EXPECT_EQ(S::MEDIUM, S::stepFontSize(S::SMALL, 1));
  EXPECT_EQ(S::LARGE, S::stepFontSize(S::MEDIUM, 1));
  EXPECT_EQ(S::EXTRA_LARGE, S::stepFontSize(S::LARGE, 1));
  EXPECT_EQ(S::XX_LARGE, S::stepFontSize(S::EXTRA_LARGE, 1));
}

TEST(FontSizeLadder, StepsDownThroughEveryVisualSize) {
  EXPECT_EQ(S::EXTRA_LARGE, S::stepFontSize(S::XX_LARGE, -1));
  EXPECT_EQ(S::LARGE, S::stepFontSize(S::EXTRA_LARGE, -1));
  EXPECT_EQ(S::MEDIUM, S::stepFontSize(S::LARGE, -1));
  EXPECT_EQ(S::SMALL, S::stepFontSize(S::MEDIUM, -1));
  EXPECT_EQ(S::TINY, S::stepFontSize(S::SMALL, -1));
}

TEST(FontSizeLadder, ClampsRatherThanWrapping) {
  // A pinch that has reached the end should stay there. Wrapping would turn a
  // continued pinch-out into the smallest text on screen.
  EXPECT_EQ(S::XX_LARGE, S::stepFontSize(S::XX_LARGE, 1));
  EXPECT_EQ(S::XX_LARGE, S::stepFontSize(S::XX_LARGE, 99));
  EXPECT_EQ(S::TINY, S::stepFontSize(S::TINY, -1));
  EXPECT_EQ(S::TINY, S::stepFontSize(S::TINY, -99));
}

TEST(FontSizeLadder, ZeroDeltaIsIdentity) {
  for (const auto& rung : S::FONT_SIZE_RUNGS) {
    EXPECT_EQ(rung.size, S::stepFontSize(rung.size, 0));
  }
}

TEST(FontSizeLadder, AnUnknownSizeIsLeftAlone) {
  // A hand-edited settings file, or a value from a future build. It has no place
  // on the ladder, so guessing an end would silently resize the reader's text.
  EXPECT_EQ(200, S::stepFontSize(200, 1));
  EXPECT_EQ(200, S::stepFontSize(200, -1));
}

TEST(FontSizeLadder, LadderCoversEverySize) {
  EXPECT_EQ(static_cast<int>(S::FONT_SIZE_COUNT), S::FONT_SIZE_RUNG_COUNT);
  for (int v = 0; v < S::FONT_SIZE_COUNT; ++v) {
    EXPECT_NE(0, S::fontSizePoints(static_cast<uint8_t>(v))) << "FONT_SIZE " << v << " names no rung";
  }
}

// --- the v0 -> v1 renumbering -----------------------------------------------------------------

// What migration has to preserve is not a number but a SIZE: whatever point size a reader had
// chosen before the renumbering, they must still have afterwards. Stated that way the test does
// not restate the mapping it is checking.
TEST(FontSizeLadder, MigrationPreservesThePointSizeTheReaderChose) {
  // The v0 numbering, which is now only recorded here and in remapLegacyFontSize().
  const struct {
    uint8_t stored;
    uint8_t points;
  } legacy[] = {{0, 12}, {1, 14}, {2, 16}, {3, 18}, {4, 10}};

  for (const auto& e : legacy) {
    const uint8_t migrated = S::remapLegacyFontSize(e.stored, 0);
    EXPECT_EQ(e.points, S::fontSizePoints(migrated))
        << "v0 value " << static_cast<int>(e.stored) << " used to mean " << static_cast<int>(e.points) << "pt";
  }
}

TEST(FontSizeLadder, MigrationIsOnlyAppliedToOlderFiles) {
  // A file already stamped at the current version holds current values; touching them would
  // shift the reader's size by one every time the settings were rewritten.
  for (int v = 0; v < S::FONT_SIZE_COUNT; ++v) {
    const auto stored = static_cast<uint8_t>(v);
    EXPECT_EQ(stored, S::remapLegacyFontSize(stored, S::FONT_SIZE_ORDER_VERSION));
  }
}

TEST(FontSizeLadder, MigrationIsAPermutationSoNoTwoSizesCollapse) {
  std::set<uint8_t> seen;
  for (uint8_t stored = 0; stored < 5; ++stored) {
    EXPECT_TRUE(seen.insert(S::remapLegacyFontSize(stored, 0)).second)
        << "two v0 values migrate to the same size";
  }
}

TEST(FontSizeLadder, MigrationLeavesAValueItDoesNotRecognise) {
  // v0 had no XX_LARGE, and a hand-edited file can hold anything. Either way the caller's own
  // clamp is the right place to deal with it, not a guess here.
  EXPECT_EQ(200, S::remapLegacyFontSize(200, 0));
}

// --- the labels -------------------------------------------------------------------------------

TEST(FontSizeLadder, LabelsReadAsPointSizes) {
  EXPECT_EQ("10pt", S::fontSizeLabel(S::TINY));
  EXPECT_EQ("14pt", S::fontSizeLabel(S::MEDIUM));
  EXPECT_EQ("24pt", S::fontSizeLabel(S::XX_LARGE));
  EXPECT_EQ("", S::fontSizeLabel(200)) << "a value off the ladder has no label to show";
}

// enumLabels is indexed by stored value, so a gap would render as a blank but selectable row.
TEST(FontSizeLadder, LabelListHasNoBlanksAndIsInAscendingOrder) {
  const auto labels = S::fontSizeLabels();
  ASSERT_EQ(static_cast<size_t>(S::FONT_SIZE_COUNT), labels.size());
  for (int v = 0; v < S::FONT_SIZE_COUNT; ++v) {
    EXPECT_EQ(S::fontSizeLabel(static_cast<uint8_t>(v)), labels[v]);
    EXPECT_FALSE(labels[v].empty());
  }
  EXPECT_EQ("10pt", labels.front());
  EXPECT_EQ("24pt", labels.back());
}

TEST(FontSizeLadder, DefaultEntryShiftsEveryValueByOne) {
  const auto labels = S::fontSizeLabels("Default");
  ASSERT_EQ(static_cast<size_t>(S::FONT_SIZE_COUNT) + 1, labels.size());
  EXPECT_EQ("Default", labels[0]);
  for (int v = 0; v < S::FONT_SIZE_COUNT; ++v) {
    EXPECT_EQ(S::fontSizeLabel(static_cast<uint8_t>(v)), labels[v + 1]);
  }
}

}  // namespace
