#include <gtest/gtest.h>

#include "activities/reader/EpubProgressRecord.h"

namespace {

EpubProgressRecord sample() {
  EpubProgressRecord r;
  r.spineIndex = 1733;  // above 255: both bytes of every field matter
  r.page = 300;
  r.pageCount = 412;
  r.percent = 87;
  return r;
}

TEST(EpubProgressRecord, WithoutAParagraphItIsTheSevenByteRecordEveryReaderKnows) {
  uint8_t data[EpubProgressRecord::kMaxSize];
  ASSERT_EQ(7u, sample().encode(data));
  // The prefix the sleep screen and the home progress badge read: spine, page, pageCount, percent.
  EXPECT_EQ(1733, data[0] | (data[1] << 8));
  EXPECT_EQ(300, data[2] | (data[3] << 8));
  EXPECT_EQ(412, data[4] | (data[5] << 8));
  EXPECT_EQ(87, data[6]);
}

TEST(EpubProgressRecord, AParagraphRoundTrips) {
  EpubProgressRecord r = sample();
  r.paragraph = 1025;
  uint8_t data[EpubProgressRecord::kMaxSize];
  const size_t size = r.encode(data);
  ASSERT_EQ(9u, size);

  const auto back = EpubProgressRecord::decode(data, size);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(1733, back->spineIndex);
  EXPECT_EQ(300, back->page);
  EXPECT_EQ(412, back->pageCount);
  EXPECT_EQ(87, back->percent);
  ASSERT_TRUE(back->paragraph.has_value());
  EXPECT_EQ(1025, *back->paragraph);
}

TEST(EpubProgressRecord, ParagraphZeroIsNotAnAnchor) {
  EpubProgressRecord r = sample();
  r.paragraph = 0;  // the LUT's "no <p> opened yet"
  uint8_t data[EpubProgressRecord::kMaxSize];
  EXPECT_EQ(7u, r.encode(data));

  const uint8_t zeroTail[9] = {1, 0, 2, 0, 3, 0, 50, 0, 0};
  const auto decoded = EpubProgressRecord::decode(zeroTail, sizeof(zeroTail));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FALSE(decoded->paragraph.has_value());
}

// Files written by earlier firmware must keep loading exactly as before.
TEST(EpubProgressRecord, ReadsEveryOlderSize) {
  const uint8_t data[9] = {5, 0, 12, 0, 40, 0, 33, 7, 0};

  const auto four = EpubProgressRecord::decode(data, 4);
  ASSERT_TRUE(four.has_value());
  EXPECT_EQ(5, four->spineIndex);
  EXPECT_EQ(12, four->page);
  EXPECT_EQ(0, four->pageCount) << "unknown, so nothing is rescaled against it";

  const auto six = EpubProgressRecord::decode(data, 6);
  ASSERT_TRUE(six.has_value());
  EXPECT_EQ(40, six->pageCount);

  const auto seven = EpubProgressRecord::decode(data, 7);
  ASSERT_TRUE(seven.has_value());
  EXPECT_EQ(33, seven->percent);
  EXPECT_FALSE(seven->paragraph.has_value());

  const auto eight = EpubProgressRecord::decode(data, 8);
  ASSERT_TRUE(eight.has_value()) << "a torn paragraph write still restores the page";
  EXPECT_FALSE(eight->paragraph.has_value());
}

TEST(EpubProgressRecord, RejectsWhatWasAlwaysRejected) {
  const uint8_t data[9] = {};
  EXPECT_FALSE(EpubProgressRecord::decode(data, 0).has_value());
  EXPECT_FALSE(EpubProgressRecord::decode(data, 3).has_value());
  EXPECT_FALSE(EpubProgressRecord::decode(data, 5).has_value()) << "a torn write of the 6-byte record";
}

}  // namespace
