// ReadingStatsStore's arithmetic on plain data: the eviction order, a session and a finish applied
// to one book and the global figures, and a book taken back out of them. The streamed file layer
// (ReadingStatsJsonTest) and the store on real files (ReadingStatsStoreTest) build on these.
#include <gtest/gtest.h>

#include <vector>

#include "HalClock.h"
#include "ReadingStats.h"

// Link stubs. ReadingStats.cpp reaches the clock only for "today"; these tests never do.
namespace HalClock {
time_t now() { return 0; }
bool isSynced() { return false; }
}  // namespace HalClock

namespace {

constexpr time_t kDay = 86400;
// 2026-01-10 12:00 UTC. Noon, so consecutive multiples of kDay land on consecutive local days in
// whatever timezone the host runs in.
constexpr time_t kNoon = 1768046400;

}  // namespace

TEST(ReadingStatsEviction, LeastRecentlyReadGoesFirstThenTheLeastRead) {
  // (lastRead, seconds) pairs: an older last read goes first whatever the time spent...
  EXPECT_TRUE(ReadingStatsStore::evictsBefore(50, 999, 100, 1));
  EXPECT_FALSE(ReadingStatsStore::evictsBefore(100, 1, 50, 999));
  // ...and on the same last read, the book with less time goes first.
  EXPECT_TRUE(ReadingStatsStore::evictsBefore(100, 50, 100, 300));
  EXPECT_FALSE(ReadingStatsStore::evictsBefore(100, 300, 100, 50));
  // A full tie is not "before": the first candidate found stays the victim.
  EXPECT_FALSE(ReadingStatsStore::evictsBefore(100, 50, 100, 50));
}

TEST(ReadingStatsApply, SessionOnANewBookStartsItsHistory) {
  BookReadingStats book;
  book.docId = "a";
  ReadingTotals totals;

  ReadingStatsStore::applySession(book, totals, "Book A", "X", 600, 10, 20, kNoon);

  EXPECT_EQ(book.title, "Book A");
  EXPECT_EQ(book.author, "X");
  EXPECT_EQ(book.totalSeconds, 600u);
  EXPECT_EQ(book.pagesTurned, 10u);
  EXPECT_EQ(book.sessions, 1u);
  EXPECT_EQ(book.progress, 20);
  EXPECT_EQ(book.firstReadEpoch, kNoon);
  EXPECT_EQ(book.lastReadEpoch, kNoon);
  ASSERT_EQ(book.days.size(), 1u);
  EXPECT_EQ(book.days[0].seconds, 600u);
  EXPECT_EQ(totals.totalSeconds, 600u);
  EXPECT_EQ(totals.totalSessions, 1u);
  EXPECT_EQ(totals.totalPagesTurned, 10u);
  ASSERT_EQ(totals.globalDays.size(), 1u);
  EXPECT_EQ(totals.longestStreak, 1u);
}

TEST(ReadingStatsApply, ZeroSecondSessionOnlyMovesProgress) {
  BookReadingStats book;
  book.docId = "a";
  book.totalSeconds = 600;
  book.sessions = 1;
  book.progress = 20;
  ReadingTotals totals;
  totals.totalSeconds = 600;
  totals.totalSessions = 1;

  ReadingStatsStore::applySession(book, totals, "Book A", "", 0, 0, 25, kNoon + kDay);

  EXPECT_EQ(book.progress, 25);
  EXPECT_EQ(book.sessions, 1u);
  EXPECT_EQ(book.totalSeconds, 600u);
  EXPECT_EQ(totals.totalSessions, 1u);
  EXPECT_TRUE(totals.globalDays.empty());
}

TEST(ReadingStatsApply, SessionWithoutAClockLeavesDaysAndDatesAlone) {
  BookReadingStats book;
  book.docId = "a";
  ReadingTotals totals;

  ReadingStatsStore::applySession(book, totals, "Book A", "", 300, 4, 10, /*walltimeEpoch=*/0);

  EXPECT_EQ(book.totalSeconds, 300u);
  EXPECT_EQ(book.sessions, 1u);
  EXPECT_EQ(book.firstReadEpoch, 0);
  EXPECT_EQ(book.lastReadEpoch, 0);
  EXPECT_TRUE(book.days.empty());
  EXPECT_EQ(totals.totalSeconds, 300u);
  EXPECT_TRUE(totals.globalDays.empty());
  EXPECT_EQ(totals.longestStreak, 0u);
}

TEST(ReadingStatsApply, EmptyTitleOrAuthorKeepsTheOldOne) {
  BookReadingStats book;
  book.docId = "a";
  book.title = "Old title";
  book.author = "Old author";
  ReadingTotals totals;

  ReadingStatsStore::applySession(book, totals, "", "", 60, 1, 1, kNoon);

  EXPECT_EQ(book.title, "Old title");
  EXPECT_EQ(book.author, "Old author");
}

TEST(ReadingStatsApply, FinishCountsAndMovesTheDates) {
  BookReadingStats book;
  book.docId = "a";
  book.progress = 97;
  book.lastReadEpoch = kNoon;

  ReadingStatsStore::applyFinish(book, "Book A", "X", kNoon + kDay);

  EXPECT_EQ(book.finishedCount, 1);
  EXPECT_EQ(book.progress, 100);
  EXPECT_EQ(book.lastFinishedEpoch, kNoon + kDay);
  EXPECT_EQ(book.lastReadEpoch, kNoon + kDay);
  EXPECT_EQ(book.title, "Book A");
}

TEST(ReadingStatsTakeOut, NeverWrapsBelowZero) {
  BookReadingStats book;
  book.totalSeconds = 500;
  book.sessions = 3;
  book.pagesTurned = 40;
  book.days = {{localDayIndexFromEpoch(kNoon), 500}};
  ReadingTotals totals;
  totals.totalSeconds = 200;
  totals.totalSessions = 1;
  totals.totalPagesTurned = 10;
  totals.globalDays = {{localDayIndexFromEpoch(kNoon), 200}};

  ReadingStatsStore::takeOut(book, totals);

  EXPECT_EQ(totals.totalSeconds, 0u);
  EXPECT_EQ(totals.totalSessions, 0u);
  EXPECT_EQ(totals.totalPagesTurned, 0u);
  EXPECT_TRUE(totals.globalDays.empty());
}
