#include <gtest/gtest.h>

#include "activities/reader/ChapterPageSpan.h"

namespace {

TEST(ChapterPageSpan, AOneFileChapterPassesThePerSpineCounterThrough) {
  const ChapterPageSpan span;
  const auto d = span.apply(3, 10);
  EXPECT_EQ(3, d.page);
  EXPECT_EQ(10, d.total);
  EXPECT_FALSE(d.approximate);
}

// Issue #325: text, illustration, text. Reading on across either boundary must keep counting,
// not restart at 1.
TEST(ChapterPageSpan, CountingContinuesAcrossTheFilesOfOneChapter) {
  // Last page of the first text file (8 pages); the illustration (1) and the rest (6) are cached.
  ChapterPageSpan first;
  first.pagesAfter = 1 + 6;
  const auto end = first.apply(8, 8);
  EXPECT_EQ(8, end.page);
  EXPECT_EQ(15, end.total);

  // The illustration page.
  ChapterPageSpan image;
  image.pagesBefore = 8;
  image.pagesAfter = 6;
  const auto pic = image.apply(1, 1);
  EXPECT_EQ(9, pic.page);
  EXPECT_EQ(15, pic.total);

  // First page of the second text file.
  ChapterPageSpan second;
  second.pagesBefore = 8 + 1;
  const auto start = second.apply(1, 6);
  EXPECT_EQ(10, start.page);
  EXPECT_EQ(15, start.total);
  EXPECT_FALSE(start.approximate);
}

TEST(ChapterPageSpan, AnUnbuiltFileIsEstimatedAtThePagesPerByteOfTheCountedOnes) {
  // 5 exact pages in 10 KB before, the current file 10 pages in 20 KB: 1 page per 2 KB, so an
  // unbuilt 40 KB file after it counts as 20.
  ChapterPageSpan span;
  span.pagesBefore = 5;
  span.knownBytes = 10000;
  span.currentBytes = 20000;
  span.unknownAfter = 1;
  span.unknownBytesAfter = 40000;
  const auto d = span.apply(1, 10);
  EXPECT_EQ(6, d.page);
  EXPECT_EQ(35, d.total);
  EXPECT_TRUE(d.approximate);
}

TEST(ChapterPageSpan, AnUnbuiltIllustrationFileStillCountsAsAPage) {
  // A 728-byte image-only XHTML is far below one page's worth of text bytes.
  ChapterPageSpan span;
  span.currentBytes = 20000;
  span.unknownBefore = 1;
  span.unknownBytesBefore = 728;
  const auto d = span.apply(1, 10);
  EXPECT_EQ(2, d.page);
  EXPECT_EQ(11, d.total);
  EXPECT_TRUE(d.approximate);
}

TEST(ChapterPageSpan, NothingIsProjectedBeforeTheCurrentFileHasAPageCount) {
  // An early mid-build estimate of 0 pages: keep the status bar's existing "no total yet" state.
  ChapterPageSpan span;
  span.pagesBefore = 12;
  span.unknownAfter = 2;
  span.unknownBytesAfter = 30000;
  const auto d = span.apply(1, 0);
  EXPECT_EQ(1, d.page);
  EXPECT_EQ(0, d.total);
  EXPECT_FALSE(d.approximate);
}

}  // namespace
