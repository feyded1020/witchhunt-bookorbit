// BookmarkStore's BookOrbit sync fields: layout-independent position, provisional pages from the
// server, and timestamp identities. In-memory only -- the store's file I/O is not exercised here.
#include <gtest/gtest.h>

#include "BookmarkStore.h"

namespace {
const Bookmark* findSpine(const BookmarkStore& store, uint16_t spine) {
  for (const auto& bm : store.getAll()) {
    if (bm.spineIndex == spine) return &bm;
  }
  return nullptr;
}
}  // namespace

TEST(BookmarkSync, ToggleRecordsChapterPositionWhenPageCountKnown) {
  BookmarkStore store;
  ASSERT_TRUE(store.toggle(3, 0, 11));
  ASSERT_TRUE(store.toggle(4, 10, 11));
  ASSERT_TRUE(store.toggle(5, 5, 11));
  EXPECT_EQ(findSpine(store, 3)->progressQ, 0);
  EXPECT_EQ(findSpine(store, 4)->progressQ, 10000);  // last page is the chapter end
  EXPECT_EQ(findSpine(store, 5)->progressQ, 5000);
}

TEST(BookmarkSync, ToggleWithoutPageCountLeavesPositionUnknown) {
  BookmarkStore store;
  ASSERT_TRUE(store.toggle(1, 4));
  EXPECT_EQ(store.getAll()[0].progressQ, Bookmark::PROGRESS_UNKNOWN);
}

TEST(BookmarkSync, ResolveFillsUnknownPositionFromPage) {
  BookmarkStore store;
  store.toggle(2, 3);  // made mid-build: no page count yet
  EXPECT_TRUE(store.resolvePagesForChapter(2, 7));
  EXPECT_EQ(store.getAll()[0].progressQ, 5000);  // page 3 of 0..6
  EXPECT_FALSE(store.resolvePagesForChapter(2, 7)) << "second pass must be a no-op";
}

TEST(BookmarkSync, ServerBookmarkPlacedOnRealPageOnceChapterIsLaidOut) {
  BookmarkStore store;
  // addSynced needs a plausible clock to mint an identity; the host clock is one.
  const uint32_t ts = store.addSynced(6, 7500, "From the web");
  ASSERT_NE(ts, 0u);
  const Bookmark& bm = store.getAll()[0];
  EXPECT_EQ(bm.pageNumber, 0) << "provisional page is the chapter start";
  EXPECT_TRUE(bm.flags & Bookmark::FLAG_PAGE_APPROX);

  EXPECT_FALSE(store.resolvePagesForChapter(5, 9)) << "other chapters are untouched";
  EXPECT_TRUE(store.resolvePagesForChapter(6, 9));
  EXPECT_EQ(store.getAll()[0].pageNumber, 6);  // 0.75 of pages 0..8
  EXPECT_FALSE(store.getAll()[0].flags & Bookmark::FLAG_PAGE_APPROX);
  EXPECT_EQ(store.getAll()[0].progressQ, 7500) << "the synced position is never rewritten";
}

TEST(BookmarkSync, TimestampsAreUniqueIdentities) {
  BookmarkStore store;
  const uint32_t a = store.addSynced(1, 0, "");
  const uint32_t b = store.addSynced(1, 100, "");
  store.toggle(1, 9, 20);
  ASSERT_NE(a, 0u);
  ASSERT_NE(b, 0u);
  EXPECT_NE(a, b);
  EXPECT_NE(store.getAll()[2].timestamp, a);
  EXPECT_NE(store.getAll()[2].timestamp, b);
}

TEST(BookmarkSync, RemoveByTimestamp) {
  BookmarkStore store;
  const uint32_t keep = store.addSynced(1, 0, "keep");
  const uint32_t drop = store.addSynced(2, 0, "drop");
  EXPECT_TRUE(store.removeByTimestamp(drop));
  EXPECT_FALSE(store.removeByTimestamp(drop));
  EXPECT_FALSE(store.removeByTimestamp(0));
  ASSERT_EQ(store.getAll().size(), 1u);
  EXPECT_EQ(store.getAll()[0].timestamp, keep);
}
