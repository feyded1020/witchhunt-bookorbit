// Which rows a list that decodes them from storage keeps in memory (src/util/ListRowCache.h, used by
// the reading-stats book list).
//
// The cache must hold every row the next render draws, whatever moved the window: a step the follow
// scrolls after, a page turn that keeps the selection on its line, or nothing at all. And it must
// not refill on every press: each refill reads the rows from the SD card on the loop task.

#include <gtest/gtest.h>

#include <algorithm>

#include "util/ListRowCache.h"

namespace {

using ListRowCache::Range;

void expectRange(const Range& range, const int first, const int last) {
  EXPECT_EQ(range.first, first);
  EXPECT_EQ(range.last, last);
}

}  // namespace

TEST(ListRowCache, EmptyListHoldsNothing) {
  expectRange(ListRowCache::needed(0, 0, 0, 8), 0, 0);
  expectRange(ListRowCache::toDecode(0, 0, 0, 8), 0, 0);
}

TEST(ListRowCache, SelectionOnScreenNeedsTheWindow) {
  // 100 books, eight drawn from row 40, the selection on row 43.
  expectRange(ListRowCache::needed(100, 43, 40, 8), 40, 49);
}

TEST(ListRowCache, DecodesTheWindowAndOneWindowEitherSide) {
  expectRange(ListRowCache::toDecode(100, 43, 40, 8), 32, 57);
}

TEST(ListRowCache, StepPastTheBottomNeedsTheWindowTheFollowScrollsTo) {
  // Down from the last line of [40, 48): the follow puts row 48 on the last line.
  expectRange(ListRowCache::needed(100, 48, 40, 8), 41, 50);
  expectRange(ListRowCache::toDecode(100, 48, 40, 8), 33, 58);
}

TEST(ListRowCache, StepPastTheTopNeedsTheWindowTheFollowScrollsTo) {
  // Up from the first line of [40, 48): the follow puts row 39 on the first line.
  expectRange(ListRowCache::needed(100, 39, 40, 8), 39, 48);
  expectRange(ListRowCache::toDecode(100, 39, 40, 8), 31, 56);
}

TEST(ListRowCache, ClampsToTheList) {
  expectRange(ListRowCache::toDecode(100, 2, 0, 8), 0, 17);
  expectRange(ListRowCache::toDecode(100, 97, 92, 8), 84, 100);
  // A list shorter than a screen is decoded whole.
  expectRange(ListRowCache::needed(5, 2, 0, 8), 0, 5);
  expectRange(ListRowCache::toDecode(5, 2, 0, 8), 0, 5);
}

TEST(ListRowCache, UnmeasuredWindowCountsAsOneRow) {
  expectRange(ListRowCache::needed(100, 10, 10, 0), 10, 12);
  expectRange(ListRowCache::toDecode(100, 10, 10, 0), 9, 13);
  expectRange(ListRowCache::toDecode(100, 10, 10, -3), 9, 13);
}

TEST(ListRowCache, StaleIndexesAreClampedIntoTheList) {
  // The list shrank under a selection and a window from before (a book was removed).
  expectRange(ListRowCache::needed(10, 25, 20, 8), 9, 10);
  expectRange(ListRowCache::toDecode(10, 25, 20, 8), 1, 10);
  expectRange(ListRowCache::needed(10, -4, -2, 8), 0, 9);
}

// Whatever the inputs: the rows decoded hold the rows needed, lie inside the list and come to at
// most three windows and a row; the rows needed hold the selection.
TEST(ListRowCache, DecodeHoldsWhatIsNeededAndStaysSmall) {
  for (int count = 1; count <= 30; ++count) {
    for (int drawn = 1; drawn <= 10; ++drawn) {
      for (int top = 0; top < count; ++top) {
        for (int sel = 0; sel < count; ++sel) {
          const Range needed = ListRowCache::needed(count, sel, top, drawn);
          const Range decode = ListRowCache::toDecode(count, sel, top, drawn);
          ASSERT_TRUE(decode.holds(needed)) << count << " " << sel << " " << top << " " << drawn;
          ASSERT_GE(decode.first, 0);
          ASSERT_LE(decode.last, count);
          ASSERT_LE(decode.last - decode.first, 3 * drawn + 1);
          ASSERT_LE(needed.first, sel);
          ASSERT_GT(needed.last, sel);
          // The row past the window is held too: the list reads it to see whether it fits.
          ASSERT_EQ(needed.last, std::min(count, needed.first + drawn + 1));
        }
      }
    }
  }
}

// Stepping down a 100-row list one row at a time: every screen drawn is held, and the cache is
// refilled about once a screen, never on every step.
TEST(ListRowCache, SteppingDownRefillsAboutOncePerScreen) {
  constexpr int kCount = 100;
  constexpr int kDrawn = 8;
  Range held = ListRowCache::toDecode(kCount, 0, 0, kDrawn);
  int top = 0;
  int refills = 0;
  for (int sel = 1; sel < kCount; ++sel) {
    if (!held.holds(ListRowCache::needed(kCount, sel, top, kDrawn))) {
      held = ListRowCache::toDecode(kCount, sel, top, kDrawn);
      ++refills;
    }
    // The render: the follow scrolls the least it must to show the selection.
    if (sel >= top + kDrawn) top = sel - kDrawn + 1;
    ASSERT_TRUE(held.holds(Range{top, std::min(kCount, top + kDrawn)})) << "row " << sel;
  }
  EXPECT_GE(refills, 1);
  EXPECT_LE(refills, kCount / kDrawn);
}

TEST(ListRowCache, SteppingUpRefillsAboutOncePerScreen) {
  constexpr int kCount = 100;
  constexpr int kDrawn = 8;
  int top = kCount - kDrawn;
  Range held = ListRowCache::toDecode(kCount, kCount - 1, top, kDrawn);
  int refills = 0;
  for (int sel = kCount - 2; sel >= 0; --sel) {
    if (!held.holds(ListRowCache::needed(kCount, sel, top, kDrawn))) {
      held = ListRowCache::toDecode(kCount, sel, top, kDrawn);
      ++refills;
    }
    if (sel < top) top = sel;
    ASSERT_TRUE(held.holds(Range{top, std::min(kCount, top + kDrawn)})) << "row " << sel;
  }
  EXPECT_GE(refills, 1);
  EXPECT_LE(refills, kCount / kDrawn);
}

// Paging forward with the selection kept on its line, the activity passing the page's own top:
// each page is held before its render, and the cache is refilled about every other page.
TEST(ListRowCache, PagingRefillsAboutEveryOtherPage) {
  constexpr int kCount = 100;
  constexpr int kDrawn = 8;
  constexpr int kLine = 3;
  Range held = ListRowCache::toDecode(kCount, kLine, 0, kDrawn);
  int refills = 0;
  int pages = 0;
  for (int top = kDrawn; top < kCount; top += kDrawn) {
    ++pages;
    const int sel = std::min(top + kLine, kCount - 1);
    if (!held.holds(ListRowCache::needed(kCount, sel, top, kDrawn))) {
      held = ListRowCache::toDecode(kCount, sel, top, kDrawn);
      ++refills;
    }
    ASSERT_TRUE(held.holds(Range{top, std::min(kCount, top + kDrawn)})) << "page at " << top;
  }
  EXPECT_LE(refills, (pages + 1) / 2);
}
