// ReadingStatsSlotFile: the layout of reading-stats.bin -- encodings, checksums, choosing the meta
// copy and the free slot -- and its reads and writes against real files in a temporary directory.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ReadingStatsSlotFile.h"

namespace {

using namespace ReadingStatsSlotFile;

const std::string kIdA = "73c3432c0dae4a36209a8c1525659ebc";
const std::string kIdB = "b6dc5b69b2d69fa1dbea8fdbf4f7cca0";
const std::string kE = "\xC3\xA9";  // U+00E9, two bytes of UTF-8

DocKey keyOf(const std::string& id) {
  DocKey key{};
  EXPECT_TRUE(parseDocId(id, key));
  return key;
}

DocKey keyNumber(const size_t n) {
  DocKey key{};
  key[0] = static_cast<uint8_t>(n);
  key[1] = static_cast<uint8_t>(n >> 8);
  key[15] = 0x5A;
  return key;
}

BookReadingStats sampleBook() {
  BookReadingStats b;
  b.docId = kIdA;
  b.title = "Men at Arms";
  b.author = "Terry Pratchett";
  b.totalSeconds = 5400;
  b.pagesTurned = 90;
  b.sessions = 4;
  b.firstReadEpoch = 1767225600;
  b.lastReadEpoch = 1768046400;
  b.lastFinishedEpoch = 1768046400;
  b.finishedCount = 1;
  b.progress = 100;
  b.days = {{20463, 3000}, {20464, 2400}};
  return b;
}

void expectSameBook(const BookReadingStats& a, const BookReadingStats& b) {
  EXPECT_EQ(a.docId, b.docId);
  EXPECT_EQ(a.title, b.title);
  EXPECT_EQ(a.author, b.author);
  EXPECT_EQ(a.totalSeconds, b.totalSeconds);
  EXPECT_EQ(a.pagesTurned, b.pagesTurned);
  EXPECT_EQ(a.sessions, b.sessions);
  EXPECT_EQ(a.firstReadEpoch, b.firstReadEpoch);
  EXPECT_EQ(a.lastReadEpoch, b.lastReadEpoch);
  EXPECT_EQ(a.lastFinishedEpoch, b.lastFinishedEpoch);
  EXPECT_EQ(a.finishedCount, b.finishedCount);
  EXPECT_EQ(a.progress, b.progress);
  ASSERT_EQ(a.days.size(), b.days.size());
  for (size_t i = 0; i < a.days.size(); ++i) {
    EXPECT_EQ(a.days[i].dayIndex, b.days[i].dayIndex);
    EXPECT_EQ(a.days[i].seconds, b.days[i].seconds);
  }
}

BookReadingStats roundTrip(const BookReadingStats& book) {
  std::vector<uint8_t> image(kSlotSize);
  encodeSlot(keyOf(book.docId), book, image.data());
  DocKey key{};
  BookReadingStats back;
  EXPECT_TRUE(decodeSlot(image.data(), key, back));
  EXPECT_EQ(key, keyOf(book.docId));
  return back;
}

class SlotFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ =
        std::filesystem::temp_directory_path() / (std::string("rsb-") + info->test_suite_name() + "-" + info->name());
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    path_ = (dir_ / "reading-stats.bin").generic_string();
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  // A sealed meta holding only `seconds` of reading, written into `copy`.
  void writeCopy(const uint8_t copy, const uint32_t seq, const uint32_t seconds) const {
    auto meta = Meta::create();
    ASSERT_NE(meta, nullptr);
    ReadingTotals totals;
    totals.totalSeconds = seconds;
    ASSERT_TRUE(meta->writeTotals(totals));
    meta->setSeq(seq);
    meta->seal();
    FsFile file;
    ASSERT_TRUE(Storage.openFileForUpdate("T", path_.c_str(), file));
    ASSERT_TRUE(writeMeta(file, *meta, copy));
  }

  Load load(uint8_t& live, uint32_t& seconds) const {
    auto meta = Meta::create();
    FsFile file;
    EXPECT_TRUE(Storage.openFileForRead("T", path_.c_str(), file));
    const Load result = loadMeta(file, *meta, live);
    seconds = result == Load::Ok ? meta->totalSeconds() : 0;
    return result;
  }

  void flipByte(const size_t at) const {
    std::fstream f(path_, std::ios::in | std::ios::out | std::ios::binary);
    f.seekg(static_cast<std::streamoff>(at));
    char c = 0;
    f.get(c);
    f.seekp(static_cast<std::streamoff>(at));
    f.put(static_cast<char>(c ^ 0x01));
  }

  std::filesystem::path dir_;
  std::string path_;
};

}  // namespace

TEST(SlotFileCrc, MatchesTheStandardCheckValue) {
  const std::string text = "123456789";
  EXPECT_EQ(crc32(reinterpret_cast<const uint8_t*>(text.data()), text.size()), 0xCBF43926u);
}

TEST(SlotFileDocId, RoundTripsTheMd5Hex) {
  DocKey key{};
  ASSERT_TRUE(parseDocId(kIdA, key));
  EXPECT_EQ(key[0], 0x73);
  EXPECT_EQ(key[15], 0xbc);
  EXPECT_EQ(formatDocId(key), kIdA);
}

TEST(SlotFileDocId, ReadsUpperCaseAndWritesLowerCase) {
  DocKey key{};
  ASSERT_TRUE(parseDocId("73C3432C0DAE4A36209A8C1525659EBC", key));
  EXPECT_EQ(formatDocId(key), kIdA);
}

TEST(SlotFileDocId, RejectsAnythingElse) {
  DocKey key{};
  EXPECT_FALSE(parseDocId("", key));
  EXPECT_FALSE(parseDocId("a", key));
  EXPECT_FALSE(parseDocId(kIdA.substr(1), key));
  EXPECT_FALSE(parseDocId(kIdA + "0", key));
  EXPECT_FALSE(parseDocId("zz" + kIdA.substr(2), key));
}

TEST(SlotFileCut, KeepsWholeCharacters) {
  EXPECT_EQ(cutLength(kE + kE + kE, 3), 2u);
  EXPECT_EQ(cutLength(kE + kE + kE, 4), 4u);
  EXPECT_EQ(cutLength("abcdef", 4), 4u);
  EXPECT_EQ(cutLength("abc", 10), 3u);
}

TEST(SlotFileSlot, RoundTripsEveryField) { expectSameBook(roundTrip(sampleBook()), sampleBook()); }

TEST(SlotFileSlot, TakesTheLargestValuesEveryFieldCanHold) {
  BookReadingStats book = sampleBook();
  book.totalSeconds = 4294967295u;
  book.pagesTurned = 4294967295u;
  book.sessions = 4294967295u;
  book.firstReadEpoch = 4102444800;  // 2100-01-01
  book.lastReadEpoch = 4102444800;
  book.lastFinishedEpoch = 4102444800;
  book.finishedCount = 65535;
  book.title = std::string(kTitleMax, 't');
  book.author = std::string(kAuthorMax, 'a');
  book.days.clear();
  for (size_t d = 0; d < kBookDayCapacity; ++d) book.days.push_back({static_cast<uint16_t>(65000 + d), 4294967295u});

  expectSameBook(roundTrip(book), book);
}

TEST(SlotFileSlot, CutsLongTextsAndKeepsTheNewestDays) {
  BookReadingStats book = sampleBook();
  book.title = std::string(kTitleMax - 1, 't') + kE;  // the two-byte character straddles the limit
  book.author = std::string(kAuthorMax + 40, 'a');
  book.days.clear();
  for (size_t d = 0; d < kBookDayCapacity + 10; ++d) book.days.push_back({static_cast<uint16_t>(20000 + d), 60});

  const BookReadingStats back = roundTrip(book);

  EXPECT_EQ(back.title, std::string(kTitleMax - 1, 't'));
  EXPECT_EQ(back.author, std::string(kAuthorMax, 'a'));
  ASSERT_EQ(back.days.size(), kBookDayCapacity);
  EXPECT_EQ(back.days.front().dayIndex, 20010);
  EXPECT_EQ(back.days.back().dayIndex, 20069);
}

TEST(SlotFileSlot, OneChangedByteFailsTheCheck) {
  std::vector<uint8_t> image(kSlotSize);
  encodeSlot(keyOf(kIdA), sampleBook(), image.data());
  image[100] ^= 0x01;
  DocKey key{};
  BookReadingStats back;

  EXPECT_FALSE(decodeSlot(image.data(), key, back));
}

TEST(SlotFileSlot, AZeroedSlotHoldsNoBook) {
  const std::vector<uint8_t> image(kSlotSize, 0);
  DocKey key{};
  BookReadingStats back;

  EXPECT_FALSE(decodeSlot(image.data(), key, back));
}

TEST(SlotFileMeta, AFreshMetaHasNoBooks) {
  auto meta = Meta::create();
  ASSERT_NE(meta, nullptr);

  EXPECT_EQ(meta->bookCount(), 0u);
  EXPECT_EQ(meta->seq(), 0u);
  EXPECT_EQ(meta->freeEntry(), 0u);
  EXPECT_EQ(meta->freeSlot(), 0);
  EXPECT_EQ(meta->find(keyOf(kIdA)), kEntryCount);
  EXPECT_FALSE(meta->valid());  // not sealed
}

TEST(SlotFileMeta, RoundTripsTotalsAndEntries) {
  auto meta = Meta::create();
  ReadingTotals totals;
  totals.totalSeconds = 1350;
  totals.totalSessions = 4;
  totals.totalPagesTurned = 23;
  totals.longestStreak = 2;
  totals.globalDays = {{20463, 950}, {20464, 400}};
  ASSERT_TRUE(meta->writeTotals(totals));
  meta->setEntry(0, entryFor(keyOf(kIdA), sampleBook(), 7));
  meta->setSeq(9);
  meta->seal();

  ASSERT_TRUE(meta->valid());
  ReadingTotals back;
  meta->readTotals(back);
  EXPECT_EQ(back.totalSeconds, 1350u);
  EXPECT_EQ(meta->totalSeconds(), 1350u);
  EXPECT_EQ(back.totalSessions, 4u);
  EXPECT_EQ(back.totalPagesTurned, 23u);
  EXPECT_EQ(back.longestStreak, 2);
  ASSERT_EQ(back.globalDays.size(), 2u);
  EXPECT_EQ(back.globalDays[1].dayIndex, 20464);
  EXPECT_EQ(back.globalDays[1].seconds, 400u);
  const size_t at = meta->find(keyOf(kIdA));
  ASSERT_EQ(at, 0u);
  const Entry entry = meta->entry(at);
  EXPECT_EQ(entry.slot, 7);
  EXPECT_EQ(entry.totalSeconds, 5400u);
  EXPECT_EQ(entry.progress, 100);
  EXPECT_EQ(entry.finishedCount, 1);
  EXPECT_EQ(entry.dayCount, 2);
  EXPECT_EQ(entry.lastReadEpoch, 1768046400);
  EXPECT_EQ(meta->bookCount(), 1u);
  EXPECT_EQ(meta->seq(), 9u);
}

TEST(SlotFileMeta, HoldsNoMoreDaysThanItsCapacity) {
  auto meta = Meta::create();
  ReadingTotals totals;
  totals.globalDays.resize(kGlobalDayCapacity + 1, DayBucket{1, 1});

  EXPECT_FALSE(meta->writeTotals(totals));
}

TEST(SlotFileMeta, FreeSlotSkipsTheSlotsInUse) {
  auto meta = Meta::create();
  meta->setEntry(0, entryFor(keyOf(kIdA), sampleBook(), 0));
  meta->setEntry(1, entryFor(keyOf(kIdB), sampleBook(), 1));

  EXPECT_EQ(meta->freeSlot(), 2);
  EXPECT_EQ(meta->freeEntry(), 2u);
}

TEST(SlotFileMeta, AFullDirectoryLeavesExactlyTheSpareSlot) {
  auto meta = Meta::create();
  for (size_t i = 0; i < kEntryCount; ++i) {
    meta->setEntry(i, entryFor(keyNumber(i), sampleBook(), static_cast<uint8_t>(kEntryCount - i)));  // slots 100..1
  }

  EXPECT_EQ(meta->freeEntry(), kEntryCount);
  EXPECT_EQ(meta->freeSlot(), 0);
  EXPECT_EQ(meta->bookCount(), kEntryCount);
}

TEST(SlotFileMeta, ClearingAnEntryFreesItAndItsSlot) {
  auto meta = Meta::create();
  meta->setEntry(0, entryFor(keyOf(kIdA), sampleBook(), 0));
  meta->setEntry(1, entryFor(keyOf(kIdB), sampleBook(), 1));

  meta->setEntry(0, Entry{});

  EXPECT_EQ(meta->find(keyOf(kIdA)), kEntryCount);
  EXPECT_EQ(meta->freeEntry(), 0u);
  EXPECT_EQ(meta->freeSlot(), 0);
  EXPECT_EQ(meta->bookCount(), 1u);
  meta->clear();
  EXPECT_EQ(meta->bookCount(), 0u);
}

TEST(SlotFileMeta, OneChangedByteFailsTheCheck) {
  auto meta = Meta::create();
  meta->seal();
  ASSERT_TRUE(meta->valid());

  meta->data()[100] ^= 0x01;

  EXPECT_FALSE(meta->valid());
}

TEST(SlotFileMeta, TwoBooksOnOneSlotAreInvalid) {
  auto meta = Meta::create();
  meta->setEntry(0, entryFor(keyOf(kIdA), sampleBook(), 5));
  meta->setEntry(1, entryFor(keyOf(kIdB), sampleBook(), 5));
  meta->seal();

  EXPECT_FALSE(meta->valid());
}

TEST_F(SlotFileTest, ANewFileIsFullSizeWithNoValidMeta) {
  ASSERT_TRUE(createZeroed(path_.c_str()));

  EXPECT_EQ(std::filesystem::file_size(path_), kFileSize);
  uint8_t live = 0;
  uint32_t seconds = 0;
  EXPECT_EQ(load(live, seconds), Load::Corrupt);
  EXPECT_EQ(live, kNoCopy);
}

TEST_F(SlotFileTest, LoadsTheNewerCopy) {
  ASSERT_TRUE(createZeroed(path_.c_str()));
  ASSERT_NO_FATAL_FAILURE(writeCopy(0, 1, 100));
  ASSERT_NO_FATAL_FAILURE(writeCopy(1, 2, 200));
  uint8_t live = kNoCopy;
  uint32_t seconds = 0;

  ASSERT_EQ(load(live, seconds), Load::Ok);

  EXPECT_EQ(live, 1);
  EXPECT_EQ(seconds, 200u);
}

TEST_F(SlotFileTest, ATornNewerCopyGivesWayToTheOlder) {
  ASSERT_TRUE(createZeroed(path_.c_str()));
  ASSERT_NO_FATAL_FAILURE(writeCopy(0, 1, 100));
  ASSERT_NO_FATAL_FAILURE(writeCopy(1, 2, 200));
  flipByte(kMetaSize + 40);  // inside copy B's directory: its CRC no longer holds
  uint8_t live = kNoCopy;
  uint32_t seconds = 0;

  ASSERT_EQ(load(live, seconds), Load::Ok);

  EXPECT_EQ(live, 0);
  EXPECT_EQ(seconds, 100u);
}

TEST_F(SlotFileTest, AFileOfTheWrongSizeIsCorrupt) {
  std::ofstream(path_, std::ios::binary) << "short";
  uint8_t live = 0;
  uint32_t seconds = 0;

  EXPECT_EQ(load(live, seconds), Load::Corrupt);
}

TEST_F(SlotFileTest, SlotsRoundTripThroughTheFile) {
  ASSERT_TRUE(createZeroed(path_.c_str()));
  std::vector<uint8_t> image(kSlotSize);
  encodeSlot(keyOf(kIdA), sampleBook(), image.data());
  FsFile file;
  ASSERT_TRUE(Storage.openFileForUpdate("T", path_.c_str(), file));

  ASSERT_TRUE(writeSlot(file, kSlotCount - 1, image.data()));
  std::vector<uint8_t> back(kSlotSize);
  ASSERT_TRUE(readSlot(file, kSlotCount - 1, back.data()));

  EXPECT_EQ(back, image);
  EXPECT_FALSE(writeSlot(file, kSlotCount, image.data()));
}
