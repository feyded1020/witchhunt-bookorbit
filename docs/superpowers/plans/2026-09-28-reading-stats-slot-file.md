# Reading Stats in a Fixed-Slot File — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reading-stats update writes one 1 KB book slot and one 6 KB meta copy in place, copy-on-write, instead of rewriting the whole ~108 KB JSON history (2.7 s at a worst-case session end on the X3).

**Architecture:** A new layout layer, `ReadingStatsSlotFile`, owns `/.crosspoint/reading-stats.bin`:
- two meta copies (header, a 100-entry directory, 400 global day buckets, CRC32);
- 101 book slots (one spare).

`ReadingStatsStore` keeps its public shape (updates, queries, Home snapshots, arithmetic) but reads and writes that file:
- the book goes into a free slot first, then the meta into the copy that is not the valid newest;
- queries read the meta and at most a few slots;
- the legacy `reading-stats.json` is imported once;
- the web handlers generate today's JSON from the new file.

The streamed JSON rewrite is deleted at the end, and `ReadingStatsFile` shrinks to the JSON codec and is renamed `ReadingStatsJson`.

**Tech Stack:** C++20, ESP32-C3 firmware via PlatformIO (pioarduino), host tests with GoogleTest via CMake + Ninja (MSYS2 UCRT64), `lib/JsonParser/StreamingJsonParser` (import only).

**Spec:** `docs/superpowers/specs/2026-09-28-reading-stats-slot-file-design.md` (it replaces the file layer of `2026-09-28-reading-stats-streamed-store-design.md`; the rest of that spec stands and is built).

## Global Constraints

**Layout** (spec §1): 115 712 bytes in all.

| Region | Offset | Size | Contents |
|---|---|---|---|
| Meta copy A | 0 | 6 144 B | header 32 B, directory 100 × 36 B, global days 400 × 6 B, CRC32 at 6 032 |
| Meta copy B | 6 144 | 6 144 B | the same, one generation apart |
| Slots | 12 288 | 101 × 1 024 B | 904 bytes used, CRC32 at byte 1 020 |

- Little-endian, written field by field; epochs 64-bit.
- The docId is its 16 MD5 bytes, parsed from `KOReaderDocumentId::calculateFromFilename()`'s 32 hex characters.
- Title ≤ 320 bytes and author ≤ 160 bytes, cut at a UTF-8 character boundary.

**Copy-on-write order:**
1. slot write, then flush;
2. meta into the copy that is **not** the live one, then flush.

A slot any live entry refers to, and the live meta copy, are never overwritten.

**Caps and keys:**
- Caps unchanged: `kMaxBooks = 100`, `kMaxBookDays = 60`, `kMaxGlobalDays = 400`. `static_assert`s tie them to the layout constants.
- The history stays keyed by `KOReaderDocumentId::calculateFromFilename()`, the MD5 of the basename. A docId that is not 32 hex characters is never stored.
- **Deleting a book from the card never touches its history.** There is no hook in `FileBrowserActivity` or the web file manager.

**Web API unchanged** (spec §5):
- The same endpoints, requests and responses.
- The export is today's JSON format, sent chunked (no Content-Length), as `reading-stats.json`.
- A card with no history answers 404 to the export.

**Legacy `reading-stats.json`:** only read, then renamed to `reading-stats.json.imported` after a verified import. It is never deleted. While `reading-stats.bin` exists, any JSON is ignored.

**Runtime rules:**
- **No SD access from any `render()`.** Store calls come from the loop task (the web handlers run there too).
- **Memory:**
  - The 6 KB `Meta` is always on the heap (`Meta::create()`), never a local; other stack locals stay under 256 B.
  - Heap goes through `makeUniqueNoThrow` (lib/Memory).
  - No exceptions (`-fno-exceptions`), never `try/catch`.
  - Heap figures (`esp_get_free_heap_size`, `heap_caps_get_largest_free_block`) appear only inside `LOG_*` macros, as today: host builds compile those away.
- **HAL:** SD access only through `Storage` / `FsFile` (HalStorage). No `.close()` on a local handle: scope it and let the destructor close it.
- **No new user-facing strings.**

**Tooling (this machine):**
- Host tests: build and run from **Git Bash**.
- Firmware: build from **PowerShell** with `PLATFORMIO_CORE_DIR=C:\pio`.
- clang-format: `/c/Program Files/LLVM/bin/clang-format.exe`.
- The host target `epub_build_inventory` fails on Windows (`dlfcn.h`). This is pre-existing; ignore it.
- The tool layer decodes a backslash followed by `u` and four hex digits in written content. Never write one literally; build it with Python `chr(92)` if ever needed. This plan needs none.

**Commands used throughout:**
- Configure host tests: `cmake -S test -B build/test`
- Build one host test: `cmake --build build/test --target <Target>` (targets: `ReadingStatsSlotFileTest`, `ReadingStatsTest`, `ReadingStatsStoreTest`)
- Run one: `./build/test/reading_stats/<Target>.exe --gtest_filter='<Suite>.*'`
- Full host suite: `cmake --build build/test -j 8 -- -k 0 > build/test/build.log 2>&1; ctest --test-dir build/test -j 8 > build/test/ctest.log 2>&1; tail -5 build/test/ctest.log`
- Firmware (PowerShell): `$env:PLATFORMIO_CORE_DIR='C:\pio'; $env:PYTHONUTF8='1'; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e default`
- Format: `"/c/Program Files/LLVM/bin/clang-format.exe" -i <files>`

**Commits:** conventional style with scope, e.g. `feat(stats): …`, body wrapped at 72.

**Branch:** `feat/stats-slot-file`, created from `feat/stats-streamed-store` (HEAD e97363dad). The untracked `reading-stats.json` in the repo root belongs to the user: never add, commit or delete it.

## Review Focus

1. **Power lost after the slot write, before the meta write.** The history must read exactly as before the update, and the next update must work (Task 3, `CrashAfterTheSlotWriteLeavesTheOldHistory`).
2. **A torn meta write (the newest copy fails its CRC).** The older copy must be used, and the next update must write over the torn one (Task 3, `TornMetaFallsBackToTheOlderCopy`, `TornMetaThenAnotherSessionKeepsGoing`).
3. **A full history taking hundreds of updates with evictions.** There must be no slot leak, exactly one spare, every indexed book readable, and the totals exact (Task 3, `ManyUpdatesOnAFullHistoryLeakNoSlot`).
4. **The first call after the firmware update is a session end, with only the legacy JSON on the card.** It must import first, then record the session, with nothing lost (Task 4, `FirstSessionAfterTheUpdateImportsFirst`). An import cut short must be redone (Task 4, `InterruptedImportIsRedone`).
5. **A title over 320 bytes with a multi-byte character at the cut.** It must be stored cut at a character boundary and the export must stay valid UTF-8 (Task 1, `CutsLongTextsAndKeepsTheNewestDays`; Task 3, `LongTitleIsCutWhole`).

---

### Task 0: Device gate — in-place write timing (throwaway, never committed)

The spec's step 0: nothing past Task 2 is built until this passes. Tasks 1–2 are layout and JSON code that the result does not change, so they may proceed while the user runs the build. **Task 3 does not start until the gate has passed.**

**Files:**
- Modify (temporarily): `src/main.cpp`. Revert it in Step 4.

**Interfaces:**
- Consumes: `Storage.openFileForWrite`, `Storage.openFileForUpdate`, `Storage.remove`, `FsFile::seek/write/read/flush`, `makeUniqueNoThrow`, `LOG_INF/LOG_ERR`.
- Produces: nothing kept.

- [ ] **Step 1: Add the benchmark to `src/main.cpp`**

Insert this function above `void setup() {` (make sure `#include <Memory.h>` is among the includes; add it if not):

```cpp
// THROWAWAY (reading-stats slot-file plan, Task 0): in-place write timing. Never committed.
static void benchmarkSlotWrites() {
  constexpr size_t kMeta = 6144;
  constexpr size_t kSlot = 1024;
  constexpr size_t kSlots = 101;
  constexpr size_t kFile = 2 * kMeta + kSlots * kSlot;
  constexpr char kBenchPath[] = "/.crosspoint/rst-bench.bin";
  constexpr int kRounds = 20;
  auto buf = makeUniqueNoThrow<uint8_t[]>(kMeta);
  if (!buf) {
    LOG_ERR("RSTB", "no 6 KB buffer");
    return;
  }
  memset(buf.get(), 0xA5, kMeta);
  unsigned long t = millis();
  {
    FsFile f;
    if (!Storage.openFileForWrite("RSTB", kBenchPath, f)) {
      LOG_ERR("RSTB", "create failed");
      return;
    }
    for (size_t done = 0; done < kFile; done += kSlot) f.write(buf.get(), kSlot);
    f.flush();
  }
  LOG_INF("RSTB", "create %u B: %lu ms", static_cast<unsigned>(kFile), millis() - t);
  unsigned long slotSum = 0, metaSum = 0, readSum = 0, slotMax = 0, metaMax = 0;
  {
    FsFile f;
    if (!Storage.openFileForUpdate("RSTB", kBenchPath, f)) {
      LOG_ERR("RSTB", "open failed");
      return;
    }
    for (int i = 0; i < kRounds; ++i) {
      buf[0] = static_cast<uint8_t>(i);
      t = millis();
      f.seek(2 * kMeta + (static_cast<size_t>(i) % kSlots) * kSlot);
      f.write(buf.get(), kSlot);
      f.flush();
      const unsigned long slotMs = millis() - t;
      t = millis();
      f.seek(static_cast<size_t>(i % 2) * kMeta);
      f.write(buf.get(), kMeta);
      f.flush();
      const unsigned long metaMs = millis() - t;
      t = millis();
      f.seek(static_cast<size_t>((i + 1) % 2) * kMeta);
      f.read(buf.get(), kMeta);
      const unsigned long readMs = millis() - t;
      LOG_INF("RSTB", "round %d: slot %lu ms, meta %lu ms, update %lu ms, meta read %lu ms", i, slotMs, metaMs,
              slotMs + metaMs, readMs);
      slotSum += slotMs;
      metaSum += metaMs;
      readSum += readMs;
      slotMax = std::max(slotMax, slotMs);
      metaMax = std::max(metaMax, metaMs);
    }
  }
  LOG_INF("RSTB", "avg over %d: slot %lu ms (max %lu), meta %lu ms (max %lu), update %lu ms, meta read %lu ms",
          kRounds, slotSum / kRounds, slotMax, metaSum / kRounds, metaMax, (slotSum + metaSum) / kRounds,
          readSum / kRounds);
  Storage.remove(kBenchPath);
}
```

Call it at the end of `setup()`, directly before `logStartupMemory("setup_complete");`:

```cpp
  benchmarkSlotWrites();
```

- [ ] **Step 2: Build the firmware**

Run (PowerShell): `$env:PLATFORMIO_CORE_DIR='C:\pio'; $env:PYTHONUTF8='1'; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e default`
Expected: `SUCCESS`.

- [ ] **Step 3: Hand it to the user**

Ask the user to flash this build on **both the X3 and the X4**, boot each once with the serial monitor attached, and paste the `RSTB` lines. Record both `avg over 20` lines in the ledger.

**Gate:**
- **Pass:** the average `update` is under 300 ms on both boards. Continue with Task 3.
- **Fail:** stop and bring the numbers to the user. The spec's fallback, writing only the meta's changed sectors with a CRC per sector, changes Task 1's `Meta` and Task 3's write, and needs its own design pass.

- [ ] **Step 4: Revert the benchmark**

Run: `git checkout -- src/main.cpp && git status --short src/main.cpp`
Expected: no output (main.cpp clean). Nothing from this task is committed.

---

### Task 1: The slot layout — `ReadingStatsSlotFile`

The file format and nothing else: encodings, checksums, choosing the valid newest meta copy and the free slot, and fixed-offset reads and writes. No arithmetic, no policy.

**Files:**
- Create: `src/ReadingStatsSlotFile.h`, `src/ReadingStatsSlotFile.cpp`
- Create: `test/reading_stats/ReadingStatsSlotFileTest.cpp`
- Modify: `test/reading_stats/CMakeLists.txt` (new target)

**Interfaces:**
- Consumes: `DayBucket`, `BookReadingStats`, `ReadingTotals` (`src/ReadingStatsTypes.h`); `HalFile`/`FsFile`, `Storage`; `makeUniqueNoThrow`.
- Produces (namespace `ReadingStatsSlotFile`):
  - Path: `kPath = "/.crosspoint/reading-stats.bin"`.
  - Constants:
    - `kVersion = 1`;
    - `kEntryCount = 100`, `kSlotCount = 101`;
    - `kGlobalDayCapacity = 400`, `kBookDayCapacity = 60`;
    - `kTitleMax = 320`, `kAuthorMax = 160`;
    - `kMetaSize = 6144`, `kSlotSize = 1024`, `kSlotsOffset = 12288`, `kFileSize = 115712`;
    - `kNoSlot = 0xFF`, `kNoCopy = 0xFF`.
  - `using DocKey = std::array<uint8_t, 16>;`
  - Free helpers:
    - `bool parseDocId(const std::string&, DocKey&)`;
    - `std::string formatDocId(const DocKey&)`;
    - `uint32_t crc32(const uint8_t*, size_t)`;
    - `size_t cutLength(const std::string&, size_t maxBytes)`.
  - `struct Entry { DocKey key; uint8_t slot = kNoSlot; uint8_t dayCount; uint8_t progress; uint16_t finishedCount; uint32_t totalSeconds; int64_t lastReadEpoch; bool used() const; }`
  - `Entry entryFor(const DocKey&, const BookReadingStats&, uint8_t slot)`
  - `class Meta`, a 6 144-byte image:
    - `static std::unique_ptr<Meta> create()`, `void clear()`;
    - `uint32_t seq() const`, `void setSeq(uint32_t)`, `uint32_t totalSeconds() const`;
    - `void readTotals(ReadingTotals&) const`, `bool writeTotals(const ReadingTotals&)`;
    - `Entry entry(size_t) const`, `void setEntry(size_t, const Entry&)`;
    - `size_t find(const DocKey&) const`, `size_t freeEntry() const`, `uint8_t freeSlot() const`, `size_t bookCount() const`;
    - `void seal()`, `bool valid() const`;
    - `data()`.
  - Slots: `void encodeSlot(const DocKey&, const BookReadingStats&, uint8_t* out)`, `bool decodeSlot(const uint8_t* in, DocKey&, BookReadingStats&)`.
  - Reading and writing the file:
    - `enum class Load : uint8_t { Ok, Corrupt, IoError }`;
    - `Load loadMeta(HalFile&, Meta&, uint8_t& live)`;
    - `bool writeMeta(HalFile&, const Meta&, uint8_t copy)`;
    - `bool readSlot(HalFile&, uint8_t slot, uint8_t* out)`, `bool writeSlot(HalFile&, uint8_t slot, const uint8_t* in)`;
    - `bool createZeroed(const char* path)`.
  - Reads and writes do **not** flush; callers flush where order matters. `createZeroed` flushes.
  - **Plan decision vs the spec:** `crc32` is our own (bitwise, zlib/IEEE). `lib/uzlib` declares `uzlib_crc32` but this copy never defines it. The meta is held as one 6 KB block: one write call, and every measured call site has ≥ 23 KB contiguous.

- [ ] **Step 1: Add the test target**

Append to `test/reading_stats/CMakeLists.txt`:

```cmake
# The layout of reading-stats.bin against real files in a temporary directory.
add_executable(ReadingStatsSlotFileTest
  ReadingStatsSlotFileTest.cpp
  ${REPO_ROOT}/src/ReadingStatsSlotFile.cpp
)

target_include_directories(ReadingStatsSlotFileTest PRIVATE
  ${REPO_ROOT}/src
  # HalStorage.h over stdio and std::filesystem. Must precede test/shims (no-op HalStorage.h) and
  # lib/hal (the real one wants SdFat).
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(ReadingStatsSlotFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingStatsSlotFileTest)
```

- [ ] **Step 2: Write the failing tests**

Create `test/reading_stats/ReadingStatsSlotFileTest.cpp`:

```cpp
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
```

- [ ] **Step 3: Run the tests to watch them fail**

Run: `cmake -S test -B build/test > /dev/null && cmake --build build/test --target ReadingStatsSlotFileTest 2>&1 | tail -5`
Expected: FAIL. The build stops with `ReadingStatsSlotFile.h: No such file or directory` (or CMake reports the missing source `src/ReadingStatsSlotFile.cpp`).

- [ ] **Step 4: Write the header**

Create `src/ReadingStatsSlotFile.h`:

```cpp
#pragma once

#include <HalStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ReadingStatsTypes.h"

// The reading history on the card: one file of fixed size, never resized.
//
//   0       meta copy A    header, directory of kEntryCount books, global days, CRC
//   6144    meta copy B    the same, one generation apart (seq)
//   12288   kSlotCount slots of kSlotSize bytes, one book each; one more than the cap
//
// Updates are copy-on-write: a book goes into a slot no entry refers to, then the meta goes into the
// copy that is not the valid newest one. Power lost at any point leaves the previous history
// readable (docs/superpowers/specs/2026-09-28-reading-stats-slot-file-design.md). This file is the
// layout only; the store (ReadingStats.h) owns the arithmetic and the order of the writes, and does
// the flushing. Every field is little-endian and written field by field.
namespace ReadingStatsSlotFile {

constexpr char kPath[] = "/.crosspoint/reading-stats.bin";

constexpr uint16_t kVersion = 1;
constexpr size_t kEntryCount = 100;             // the book cap
constexpr size_t kSlotCount = kEntryCount + 1;  // the spare an update writes into
constexpr size_t kGlobalDayCapacity = 400;
constexpr size_t kBookDayCapacity = 60;
constexpr size_t kTitleMax = 320;  // bytes of UTF-8
constexpr size_t kAuthorMax = 160;

constexpr size_t kMetaSize = 6144;
constexpr size_t kSlotSize = 1024;
constexpr size_t kSlotsOffset = 2 * kMetaSize;
constexpr size_t kFileSize = kSlotsOffset + kSlotCount * kSlotSize;

constexpr uint8_t kNoSlot = 0xFF;
constexpr uint8_t kNoCopy = 0xFF;

// A document id as its 16 MD5 bytes.
using DocKey = std::array<uint8_t, 16>;

// The 32 hex characters KOReaderDocumentId makes, as 16 bytes; false for anything else.
bool parseDocId(const std::string& docId, DocKey& key);
// Lowercase hex, as KOReaderDocumentId writes it.
std::string formatDocId(const DocKey& key);

// CRC-32 as zlib computes it (IEEE, reflected, 0xEDB88320).
uint32_t crc32(const uint8_t* data, size_t size);

// How many bytes of `text` fit in `maxBytes` without splitting a UTF-8 character.
size_t cutLength(const std::string& text, size_t maxBytes);

// One directory entry, decoded: what everything but the book screens needs.
struct Entry {
  DocKey key{};
  uint8_t slot = kNoSlot;  // kNoSlot: the entry is free
  uint8_t dayCount = 0;
  uint8_t progress = 0;
  uint16_t finishedCount = 0;
  uint32_t totalSeconds = 0;
  int64_t lastReadEpoch = 0;
  bool used() const { return slot != kNoSlot; }
};

// The entry for `book` kept in `slot`.
Entry entryFor(const DocKey& key, const BookReadingStats& book, uint8_t slot);

// A meta copy as its on-card image, edited in place. 6 KB: always on the heap (create()).
class Meta {
 public:
  // An empty history, seq 0, not sealed. nullptr when the heap has no 6 KB block.
  static std::unique_ptr<Meta> create();
  void clear();

  uint32_t seq() const;
  void setSeq(uint32_t seq);
  uint32_t totalSeconds() const;

  void readTotals(ReadingTotals& totals) const;
  // False when there are more global days than the image holds.
  bool writeTotals(const ReadingTotals& totals);

  Entry entry(size_t index) const;  // a free Entry for a free index or one out of range
  void setEntry(size_t index, const Entry& entry);  // a free Entry frees the index

  size_t find(const DocKey& key) const;  // kEntryCount when absent
  size_t freeEntry() const;              // kEntryCount when every entry is used
  uint8_t freeSlot() const;              // the lowest slot no entry refers to; kNoSlot if none
  size_t bookCount() const;

  // Magic, version, slot count, book count and CRC: call before writing the image out.
  void seal();
  // All of those hold, and no two entries share a slot.
  bool valid() const;

  const uint8_t* data() const { return image_; }
  uint8_t* data() { return image_; }

 private:
  uint8_t image_[kMetaSize] = {};
};

// A book into a slot image of kSlotSize bytes: title and author cut to their limits at a character
// boundary, at most kBookDayCapacity days (the newest).
void encodeSlot(const DocKey& key, const BookReadingStats& book, uint8_t* out);
// The book in a slot image, docId formatted from the slot's key; false when the slot fails its
// check (a free, torn or damaged slot).
bool decodeSlot(const uint8_t* in, DocKey& key, BookReadingStats& book);

enum class Load : uint8_t { Ok, Corrupt, IoError };

// The valid newest meta copy into `meta`; `live` is its copy (0 or 1). Corrupt when neither copy
// holds or the file is not kFileSize bytes.
Load loadMeta(HalFile& file, Meta& meta, uint8_t& live);
// A sealed meta into copy 0 or 1. No flush.
bool writeMeta(HalFile& file, const Meta& meta, uint8_t copy);
bool readSlot(HalFile& file, uint8_t slot, uint8_t* out);
// No flush.
bool writeSlot(HalFile& file, uint8_t slot, const uint8_t* in);
// A new file of kFileSize zero bytes at `path` (no valid meta yet), flushed.
bool createZeroed(const char* path);

}  // namespace ReadingStatsSlotFile
```

- [ ] **Step 5: Write the implementation**

Create `src/ReadingStatsSlotFile.cpp`:

```cpp
#include "ReadingStatsSlotFile.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <bitset>
#include <cstring>
#include <vector>

namespace ReadingStatsSlotFile {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'S', 'T', 'B'};
constexpr size_t kDaySize = 6;

// Meta copy: header, directory, global days, CRC.
constexpr size_t kVersionAt = 4;
constexpr size_t kSlotCountAt = 6;
constexpr size_t kSeqAt = 8;
constexpr size_t kTotalSecondsAt = 12;
constexpr size_t kTotalSessionsAt = 16;
constexpr size_t kTotalPagesAt = 20;
constexpr size_t kLongestStreakAt = 24;
constexpr size_t kBookCountAt = 26;
constexpr size_t kGlobalDayCountAt = 28;
constexpr size_t kHeaderSize = 32;
constexpr size_t kEntrySize = 36;
constexpr size_t kEntriesAt = kHeaderSize;
constexpr size_t kGlobalDaysAt = kEntriesAt + kEntryCount * kEntrySize;
constexpr size_t kMetaCrcAt = kGlobalDaysAt + kGlobalDayCapacity * kDaySize;
static_assert(kMetaCrcAt == 6032, "the meta layout moved");
static_assert(kMetaCrcAt + 4 <= kMetaSize, "the meta outgrew its copy");

// A directory entry, from its first byte (the key's).
constexpr size_t kEntrySlotAt = 16;  // slot + 1: 0 marks a free entry, so a zeroed image has none
constexpr size_t kEntryDayCountAt = 17;
constexpr size_t kEntryProgressAt = 18;
constexpr size_t kEntryFinishedAt = 20;
constexpr size_t kEntrySecondsAt = 24;
constexpr size_t kEntryLastReadAt = 28;

// A book slot, from its first byte (the key's).
constexpr size_t kSlotSecondsAt = 16;
constexpr size_t kSlotPagesAt = 20;
constexpr size_t kSlotSessionsAt = 24;
constexpr size_t kSlotFirstReadAt = 28;
constexpr size_t kSlotLastReadAt = 36;
constexpr size_t kSlotLastFinishedAt = 44;
constexpr size_t kSlotFinishedAt = 52;
constexpr size_t kSlotProgressAt = 54;
constexpr size_t kSlotDayCountAt = 55;
constexpr size_t kSlotTitleLenAt = 56;
constexpr size_t kSlotAuthorLenAt = 58;
constexpr size_t kSlotDaysAt = 60;
constexpr size_t kSlotTitleAt = kSlotDaysAt + kBookDayCapacity * kDaySize;
constexpr size_t kSlotAuthorAt = kSlotTitleAt + kTitleMax;
constexpr size_t kSlotCrcAt = kSlotSize - 4;
static_assert(kSlotAuthorAt + kAuthorMax == 900, "the slot layout moved");
static_assert(kSlotAuthorAt + kAuthorMax <= kSlotCrcAt, "the slot outgrew its size");

void put16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void put64(uint8_t* p, const int64_t v) {
  const auto u = static_cast<uint64_t>(v);
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(u >> (8 * i));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

int64_t get64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return static_cast<int64_t>(v);
}

void putDays(uint8_t* p, const std::vector<DayBucket>& days, const size_t first) {
  for (size_t i = first; i < days.size(); ++i, p += kDaySize) {
    put16(p, days[i].dayIndex);
    put32(p + 2, days[i].seconds);
  }
}

void getDays(const uint8_t* p, const size_t count, std::vector<DayBucket>& days) {
  days.clear();
  days.reserve(count);
  for (size_t i = 0; i < count; ++i, p += kDaySize) days.push_back({get16(p), get32(p + 2)});
}

int hexDigit(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

uint8_t rawSlot(const uint8_t* image, const size_t index) { return image[kEntriesAt + index * kEntrySize + kEntrySlotAt]; }

}  // namespace

bool parseDocId(const std::string& docId, DocKey& key) {
  if (docId.size() != 2 * key.size()) return false;
  for (size_t i = 0; i < key.size(); ++i) {
    const int hi = hexDigit(docId[2 * i]);
    const int lo = hexDigit(docId[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    key[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

std::string formatDocId(const DocKey& key) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text(2 * key.size(), '0');
  for (size_t i = 0; i < key.size(); ++i) {
    text[2 * i] = kHex[key[i] >> 4];
    text[2 * i + 1] = kHex[key[i] & 0x0F];
  }
  return text;
}

uint32_t crc32(const uint8_t* data, const size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

size_t cutLength(const std::string& text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text.size();
  size_t n = maxBytes;
  // Back off to the first byte of the character the limit falls inside.
  while (n > 0 && (static_cast<uint8_t>(text[n]) & 0xC0) == 0x80) --n;
  return n;
}

Entry entryFor(const DocKey& key, const BookReadingStats& book, const uint8_t slot) {
  Entry entry;
  entry.key = key;
  entry.slot = slot;
  entry.dayCount = static_cast<uint8_t>(std::min(book.days.size(), kBookDayCapacity));
  entry.progress = book.progress;
  entry.finishedCount = book.finishedCount;
  entry.totalSeconds = book.totalSeconds;
  entry.lastReadEpoch = static_cast<int64_t>(book.lastReadEpoch);
  return entry;
}

// ---- Meta -------------------------------------------------------------------------------------

std::unique_ptr<Meta> Meta::create() { return makeUniqueNoThrow<Meta>(); }

void Meta::clear() { std::memset(image_, 0, sizeof(image_)); }

uint32_t Meta::seq() const { return get32(image_ + kSeqAt); }

void Meta::setSeq(const uint32_t seq) { put32(image_ + kSeqAt, seq); }

uint32_t Meta::totalSeconds() const { return get32(image_ + kTotalSecondsAt); }

void Meta::readTotals(ReadingTotals& totals) const {
  totals.totalSeconds = get32(image_ + kTotalSecondsAt);
  totals.totalSessions = get32(image_ + kTotalSessionsAt);
  totals.totalPagesTurned = get32(image_ + kTotalPagesAt);
  totals.longestStreak = get16(image_ + kLongestStreakAt);
  getDays(image_ + kGlobalDaysAt, std::min<size_t>(get16(image_ + kGlobalDayCountAt), kGlobalDayCapacity),
          totals.globalDays);
}

bool Meta::writeTotals(const ReadingTotals& totals) {
  if (totals.globalDays.size() > kGlobalDayCapacity) return false;
  put32(image_ + kTotalSecondsAt, totals.totalSeconds);
  put32(image_ + kTotalSessionsAt, totals.totalSessions);
  put32(image_ + kTotalPagesAt, totals.totalPagesTurned);
  put16(image_ + kLongestStreakAt, totals.longestStreak);
  put16(image_ + kGlobalDayCountAt, static_cast<uint16_t>(totals.globalDays.size()));
  std::memset(image_ + kGlobalDaysAt, 0, kGlobalDayCapacity * kDaySize);
  putDays(image_ + kGlobalDaysAt, totals.globalDays, 0);
  return true;
}

Entry Meta::entry(const size_t index) const {
  Entry entry;
  if (index >= kEntryCount || rawSlot(image_, index) == 0) return entry;
  const uint8_t* p = image_ + kEntriesAt + index * kEntrySize;
  std::memcpy(entry.key.data(), p, entry.key.size());
  entry.slot = static_cast<uint8_t>(p[kEntrySlotAt] - 1);
  entry.dayCount = p[kEntryDayCountAt];
  entry.progress = p[kEntryProgressAt];
  entry.finishedCount = get16(p + kEntryFinishedAt);
  entry.totalSeconds = get32(p + kEntrySecondsAt);
  entry.lastReadEpoch = get64(p + kEntryLastReadAt);
  return entry;
}

void Meta::setEntry(const size_t index, const Entry& entry) {
  if (index >= kEntryCount) return;
  uint8_t* p = image_ + kEntriesAt + index * kEntrySize;
  std::memset(p, 0, kEntrySize);
  if (!entry.used()) return;
  std::memcpy(p, entry.key.data(), entry.key.size());
  p[kEntrySlotAt] = static_cast<uint8_t>(entry.slot + 1);
  p[kEntryDayCountAt] = entry.dayCount;
  p[kEntryProgressAt] = entry.progress;
  put16(p + kEntryFinishedAt, entry.finishedCount);
  put32(p + kEntrySecondsAt, entry.totalSeconds);
  put64(p + kEntryLastReadAt, entry.lastReadEpoch);
}

size_t Meta::find(const DocKey& key) const {
  for (size_t i = 0; i < kEntryCount; ++i) {
    if (rawSlot(image_, i) != 0 && std::memcmp(image_ + kEntriesAt + i * kEntrySize, key.data(), key.size()) == 0) {
      return i;
    }
  }
  return kEntryCount;
}

size_t Meta::freeEntry() const {
  for (size_t i = 0; i < kEntryCount; ++i) {
    if (rawSlot(image_, i) == 0) return i;
  }
  return kEntryCount;
}

uint8_t Meta::freeSlot() const {
  std::bitset<kSlotCount> taken;
  for (size_t i = 0; i < kEntryCount; ++i) {
    const uint8_t raw = rawSlot(image_, i);
    if (raw != 0 && raw <= kSlotCount) taken.set(raw - 1);
  }
  for (size_t slot = 0; slot < kSlotCount; ++slot) {
    if (!taken.test(slot)) return static_cast<uint8_t>(slot);
  }
  return kNoSlot;
}

size_t Meta::bookCount() const {
  size_t count = 0;
  for (size_t i = 0; i < kEntryCount; ++i) count += rawSlot(image_, i) != 0 ? 1 : 0;
  return count;
}

void Meta::seal() {
  std::memcpy(image_, kMagic, sizeof(kMagic));
  put16(image_ + kVersionAt, kVersion);
  put16(image_ + kSlotCountAt, static_cast<uint16_t>(kSlotCount));
  put16(image_ + kBookCountAt, static_cast<uint16_t>(bookCount()));
  put32(image_ + kMetaCrcAt, crc32(image_, kMetaCrcAt));
}

bool Meta::valid() const {
  if (std::memcmp(image_, kMagic, sizeof(kMagic)) != 0) return false;
  if (get16(image_ + kVersionAt) != kVersion || get16(image_ + kSlotCountAt) != kSlotCount) return false;
  if (get32(image_ + kMetaCrcAt) != crc32(image_, kMetaCrcAt)) return false;
  if (get16(image_ + kGlobalDayCountAt) > kGlobalDayCapacity) return false;
  // Every book in a slot of its own: two entries on one slot would lose a book at the next update.
  std::bitset<kSlotCount> taken;
  size_t books = 0;
  for (size_t i = 0; i < kEntryCount; ++i) {
    const uint8_t raw = rawSlot(image_, i);
    if (raw == 0) continue;
    if (raw > kSlotCount || taken.test(raw - 1)) return false;
    taken.set(raw - 1);
    ++books;
  }
  return books == get16(image_ + kBookCountAt);
}

// ---- Slots ------------------------------------------------------------------------------------

void encodeSlot(const DocKey& key, const BookReadingStats& book, uint8_t* out) {
  std::memset(out, 0, kSlotSize);
  std::memcpy(out, key.data(), key.size());
  put32(out + kSlotSecondsAt, book.totalSeconds);
  put32(out + kSlotPagesAt, book.pagesTurned);
  put32(out + kSlotSessionsAt, book.sessions);
  put64(out + kSlotFirstReadAt, static_cast<int64_t>(book.firstReadEpoch));
  put64(out + kSlotLastReadAt, static_cast<int64_t>(book.lastReadEpoch));
  put64(out + kSlotLastFinishedAt, static_cast<int64_t>(book.lastFinishedEpoch));
  put16(out + kSlotFinishedAt, book.finishedCount);
  out[kSlotProgressAt] = book.progress;
  // The newest days: they are sorted ascending.
  const size_t first = book.days.size() > kBookDayCapacity ? book.days.size() - kBookDayCapacity : 0;
  out[kSlotDayCountAt] = static_cast<uint8_t>(book.days.size() - first);
  putDays(out + kSlotDaysAt, book.days, first);
  const size_t titleLen = cutLength(book.title, kTitleMax);
  const size_t authorLen = cutLength(book.author, kAuthorMax);
  put16(out + kSlotTitleLenAt, static_cast<uint16_t>(titleLen));
  put16(out + kSlotAuthorLenAt, static_cast<uint16_t>(authorLen));
  std::memcpy(out + kSlotTitleAt, book.title.data(), titleLen);
  std::memcpy(out + kSlotAuthorAt, book.author.data(), authorLen);
  put32(out + kSlotCrcAt, crc32(out, kSlotCrcAt));
}

bool decodeSlot(const uint8_t* in, DocKey& key, BookReadingStats& book) {
  if (get32(in + kSlotCrcAt) != crc32(in, kSlotCrcAt)) return false;
  const uint8_t dayCount = in[kSlotDayCountAt];
  const uint16_t titleLen = get16(in + kSlotTitleLenAt);
  const uint16_t authorLen = get16(in + kSlotAuthorLenAt);
  if (dayCount > kBookDayCapacity || titleLen > kTitleMax || authorLen > kAuthorMax) return false;
  std::memcpy(key.data(), in, key.size());
  book = BookReadingStats{};
  book.docId = formatDocId(key);
  book.title.assign(reinterpret_cast<const char*>(in + kSlotTitleAt), titleLen);
  book.author.assign(reinterpret_cast<const char*>(in + kSlotAuthorAt), authorLen);
  book.totalSeconds = get32(in + kSlotSecondsAt);
  book.pagesTurned = get32(in + kSlotPagesAt);
  book.sessions = get32(in + kSlotSessionsAt);
  book.firstReadEpoch = static_cast<time_t>(get64(in + kSlotFirstReadAt));
  book.lastReadEpoch = static_cast<time_t>(get64(in + kSlotLastReadAt));
  book.lastFinishedEpoch = static_cast<time_t>(get64(in + kSlotLastFinishedAt));
  book.finishedCount = get16(in + kSlotFinishedAt);
  book.progress = in[kSlotProgressAt];
  getDays(in + kSlotDaysAt, dayCount, book.days);
  return true;
}

// ---- The file ---------------------------------------------------------------------------------

Load loadMeta(HalFile& file, Meta& meta, uint8_t& live) {
  live = kNoCopy;
  if (file.size() != kFileSize) return Load::Corrupt;
  bool marked[2] = {false, false};
  uint32_t seqs[2] = {0, 0};
  for (uint8_t copy = 0; copy < 2; ++copy) {
    uint8_t head[kSeqAt + 4];
    if (!file.seek(copy * kMetaSize) || file.read(head, sizeof(head)) != static_cast<int>(sizeof(head))) {
      return Load::IoError;
    }
    marked[copy] = std::memcmp(head, kMagic, sizeof(kMagic)) == 0;
    seqs[copy] = get32(head + kSeqAt);
  }
  // The newer copy first; one that fails its checks (a write torn by a power cut) gives way to
  // the other, which the torn write never touched.
  const uint8_t newer = seqs[1] > seqs[0] ? 1 : 0;
  const uint8_t order[2] = {newer, static_cast<uint8_t>(1 - newer)};
  for (const uint8_t copy : order) {
    if (!marked[copy]) continue;
    if (!file.seek(copy * kMetaSize) || file.read(meta.data(), kMetaSize) != static_cast<int>(kMetaSize)) {
      return Load::IoError;
    }
    if (meta.valid()) {
      live = copy;
      return Load::Ok;
    }
    LOG_ERR("RST", "Meta copy %c fails its checks", copy == 0 ? 'A' : 'B');
  }
  return Load::Corrupt;
}

bool writeMeta(HalFile& file, const Meta& meta, const uint8_t copy) {
  return copy < 2 && file.seek(copy * kMetaSize) && file.write(meta.data(), kMetaSize) == kMetaSize;
}

bool readSlot(HalFile& file, const uint8_t slot, uint8_t* out) {
  return slot < kSlotCount && file.seek(kSlotsOffset + slot * kSlotSize) &&
         file.read(out, kSlotSize) == static_cast<int>(kSlotSize);
}

bool writeSlot(HalFile& file, const uint8_t slot, const uint8_t* in) {
  return slot < kSlotCount && file.seek(kSlotsOffset + slot * kSlotSize) && file.write(in, kSlotSize) == kSlotSize;
}

bool createZeroed(const char* path) {
  auto zeros = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!zeros) return false;
  FsFile file;
  if (!Storage.openFileForWrite("RST", path, file)) return false;
  for (size_t written = 0; written < kFileSize; written += kSlotSize) {
    if (file.write(zeros.get(), kSlotSize) != kSlotSize) return false;
  }
  file.flush();
  return true;
}

}  // namespace ReadingStatsSlotFile
```

- [ ] **Step 6: Run the tests to watch them pass**

Run: `cmake -S test -B build/test > /dev/null && cmake --build build/test --target ReadingStatsSlotFileTest 2>&1 | tail -2 && ./build/test/reading_stats/ReadingStatsSlotFileTest.exe 2>&1 | tail -3`
Expected: `[  PASSED  ] 23 tests.`

- [ ] **Step 7: Format and build the firmware**

Run: `"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStatsSlotFile.h src/ReadingStatsSlotFile.cpp test/reading_stats/ReadingStatsSlotFileTest.cpp`
Then (PowerShell) the firmware build command.
Expected: `SUCCESS`. The new file compiles on the device toolchain even though nothing calls it yet.

- [ ] **Step 8: Commit**

```bash
git add src/ReadingStatsSlotFile.h src/ReadingStatsSlotFile.cpp test/reading_stats/ReadingStatsSlotFileTest.cpp test/reading_stats/CMakeLists.txt
git commit -m "feat(stats): the fixed-slot layout of reading-stats.bin

Two meta copies (header, a 100-entry directory, 400 global days,
CRC32) and 101 book slots of 1 KB, little-endian, field by field.
Copy-on-write needs only what is here: the valid newest copy, the
free slot, and fixed-offset reads and writes. Nothing uses it yet."
```

---

### Task 2: JSON writers for a generated history

The web will generate its JSON from the slot file (Task 3). This task gives `ReadingStatsFile` the pieces:
- the file head and the dashboard head;
- `writeBook` with an optional `etaSeconds`;
- the separator and the tail.

The existing rewrite and dashboard code is refactored onto them, so their tests prove the new pieces byte for byte.

**Files:**
- Modify: `src/ReadingStatsFile.h`, `src/ReadingStatsFile.cpp`
- Test: `test/reading_stats/ReadingStatsFileTest.cpp`

**Interfaces:**
- Consumes: `emit`, `emitNumber`, `emitDays`, `emitBounded` (file-local in ReadingStatsFile.cpp); `ReadingStatsStore::currentStreakIn`, `longestStreakIn`.
- Produces (namespace `ReadingStatsFile`):
  - `void writeBook(Print& out, const BookReadingStats& book, long long etaSeconds = -1)`
  - `void writeFileHead(Print& out, const ReadingTotals& totals)`
  - `void writeDashboardHead(Print& out, const ReadingTotals& totals, uint32_t bookCount, uint32_t finishedBookCount, uint16_t today)`
  - `void writeBookSeparator(Print& out)` writes `,`
  - `void writeTail(Print& out)` writes `]}`

- [ ] **Step 1: Write the failing tests**

Append to `test/reading_stats/ReadingStatsFileTest.cpp`:

```cpp
TEST(ReadingStatsFileWriteBook, AddsTheTimeToFinishBeforeTheClosingBrace) {
  StringPrint plain;
  StringPrint withEta;

  ReadingStatsFile::writeBook(plain, bookD());
  ReadingStatsFile::writeBook(withEta, bookD(), 1980);

  EXPECT_EQ(plain.text, kBookD);
  EXPECT_EQ(withEta.text, open(kBookD) + R"(,"etaSeconds":1980})");
}

TEST(ReadingStatsFileHeads, FileHeadAndBooksMakeTheFile) {
  StringPrint out;

  ReadingStatsFile::writeFileHead(out, totalsOf(1350, 4, 23, {{20463, 950}, {20464, 400}}));
  out.text += kBookA;
  ReadingStatsFile::writeBookSeparator(out);
  out.text += kBookB;
  ReadingStatsFile::writeBookSeparator(out);
  out.text += kBookC;
  ReadingStatsFile::writeTail(out);

  EXPECT_EQ(out.text, kFile);
}

TEST(ReadingStatsFileHeads, DashboardHeadCarriesCountsAndStreaks) {
  StringPrint out;

  ReadingStatsFile::writeDashboardHead(out, totalsOf(1350, 4, 23, {{20463, 950}, {20464, 400}}), 3, 1, 20464);

  EXPECT_EQ(out.text,
            R"({"totalSeconds":1350,"totalSessions":4,"totalPagesTurned":23,"bookCount":3,"finishedBookCount":1,)"
            R"("todayDayIndex":20464,"currentStreak":2,"longestStreak":2,)"
            R"("globalDays":[[20463,950],[20464,400]],"books":[)");
}

TEST(ReadingStatsFileHeads, DashboardHeadLeavesStreaksOutWithoutAClock) {
  StringPrint out;

  ReadingStatsFile::writeDashboardHead(out, totalsOf(300, 1, 5, {{20463, 300}}), 1, 1, 0);

  EXPECT_EQ(out.text,
            R"({"totalSeconds":300,"totalSessions":1,"totalPagesTurned":5,"bookCount":1,"finishedBookCount":1,)"
            R"("todayDayIndex":0,"globalDays":[[20463,300]],"books":[)");
}
```

- [ ] **Step 2: Run the tests to watch them fail**

Run: `cmake --build build/test --target ReadingStatsTest 2>&1 | grep -E "error" | head -5`
Expected: FAIL to compile. The errors name `writeFileHead`, `writeDashboardHead`, `writeBookSeparator` and `writeTail`, which are not members of `ReadingStatsFile`, plus a `writeBook` call with too many arguments.

- [ ] **Step 3: Declare the writers**

In `src/ReadingStatsFile.h`, replace the `writeBook` declaration and its comment:

```cpp
// One book as a JSON object, in the field order the device has always written. Strings are
// escaped the way ArduinoJson does: quote, backslash and control characters; UTF-8 passes through.
void writeBook(Print& out, const BookReadingStats& book);
```

with:

```cpp
// One book as a JSON object, in the field order the device has always written. Strings are
// escaped the way ArduinoJson does: quote, backslash and control characters; UTF-8 passes through.
// With `etaSeconds` not negative, the dashboard's `"etaSeconds"` field goes last, before the
// closing brace, where the web page has always found it.
void writeBook(Print& out, const BookReadingStats& book, long long etaSeconds = -1);

// The history file's text up to its books, `{"totalSeconds":…,"globalDays":[…],"books":[`. The
// books follow through writeBook() and writeBookSeparator(), then writeTail().
void writeFileHead(Print& out, const ReadingTotals& totals);

// The /api/stats payload up to its books: the figures, the counts, today's day index, the streaks
// when `today` is known and there are days, and the global days.
void writeDashboardHead(Print& out, const ReadingTotals& totals, uint32_t bookCount, uint32_t finishedBookCount,
                        uint16_t today);

void writeBookSeparator(Print& out);  // between two books
void writeTail(Print& out);           // after the last book: closes the books and the object
```

- [ ] **Step 4: Implement them and move the old writers onto them**

In `src/ReadingStatsFile.cpp`, in `writeBook`, change the signature line to `void writeBook(Print& out, const BookReadingStats& book, const long long etaSeconds) {` and replace its last line, `  emit(out, "}");`, with:

```cpp
  if (etaSeconds >= 0) emitNumber(out, "etaSeconds", etaSeconds);
  emit(out, "}");
```

Replace the whole `writeDashboard` function with:

```cpp
void writeDashboardHead(Print& out, const ReadingTotals& totals, const uint32_t bookCount,
                        const uint32_t finishedBookCount, const uint16_t today) {
  char head[192];
  int n = snprintf(head, sizeof(head),
                   "{\"totalSeconds\":%lu,\"totalSessions\":%lu,\"totalPagesTurned\":%lu,\"bookCount\":%lu,"
                   "\"finishedBookCount\":%lu,\"todayDayIndex\":%u",
                   static_cast<unsigned long>(totals.totalSeconds), static_cast<unsigned long>(totals.totalSessions),
                   static_cast<unsigned long>(totals.totalPagesTurned), static_cast<unsigned long>(bookCount),
                   static_cast<unsigned long>(finishedBookCount), today);
  emitBounded(out, head, n, sizeof(head));
  if (today != 0 && !totals.globalDays.empty()) {
    n = snprintf(head, sizeof(head), ",\"currentStreak\":%u,\"longestStreak\":%u",
                 ReadingStatsStore::currentStreakIn(totals.globalDays, today),
                 ReadingStatsStore::longestStreakIn(totals.globalDays, totals.longestStreak));
    emitBounded(out, head, n, sizeof(head));
  }
  emit(out, ",\"globalDays\":");
  emitDays(out, totals.globalDays);
  emit(out, ",\"books\":[");
}

void writeFileHead(Print& out, const ReadingTotals& totals) {
  char head[160];
  const int n = snprintf(head, sizeof(head),
                         "{\"totalSeconds\":%lu,\"totalSessions\":%lu,\"totalPagesTurned\":%lu,\"longestStreak\":%u,"
                         "\"globalDays\":",
                         static_cast<unsigned long>(totals.totalSeconds),
                         static_cast<unsigned long>(totals.totalSessions),
                         static_cast<unsigned long>(totals.totalPagesTurned), totals.longestStreak);
  emitBounded(out, head, n, sizeof(head));
  emitDays(out, totals.globalDays);
  emit(out, ",\"books\":[");
}

void writeBookSeparator(Print& out) { emit(out, ","); }

void writeTail(Print& out) { emit(out, "]}"); }

void writeDashboard(HalFile& in, const Summary& summary, const uint16_t today, Print& out) {
  writeDashboardHead(out, summary, summary.bookCount, summary.finishedBookCount, today);
  DashboardCopy copy(
      out, ReadingStatsStore::pooledSecondsPerPercent(summary.totalSeconds, summary.paceSeconds, summary.pacePercents));
  if (copy.run(in) != ScanResult::Ok) {
    LOG_ERR("RSF", "Stats file changed or failed between passes; dashboard truncated");
  }
  writeTail(out);
}
```

In `writeRewrite`, replace everything from `  char head[160];` through `  emit(out, ",\"books\":[");` with `  writeFileHead(out, rewrite.totals);`. Replace its final `  emit(out, "]}");` with `  writeTail(out);`.

- [ ] **Step 5: Run the tests to watch them pass**

Run: `cmake --build build/test --target ReadingStatsTest 2>&1 | tail -1 && ./build/test/reading_stats/ReadingStatsTest.exe 2>&1 | tail -3`
Expected: `[  PASSED  ] 39 tests.` That is the previous 35 plus the 4 new ones. The existing `ReadingStatsFileDashboard.*` and `ReadingStatsFileRewrite.*` still pass, which proves the moved heads are byte-identical.

- [ ] **Step 6: Format and commit**

```bash
"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsFileTest.cpp
git add src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsFileTest.cpp
git commit -m "refactor(stats): JSON heads and a time-to-finish field for writeBook

The web will generate its JSON from the slot file rather than copy it
from the card. The file and dashboard heads, the separator and the
tail come out of writeRewrite and writeDashboard, which now use them;
their tests pin the output byte for byte."
```

---

### Task 3: The store on the slot file

**Starts only after Task 0's gate passed.** This task:
- rewrites `ReadingStatsStore`'s internals on `ReadingStatsSlotFile` (copy-on-write updates, reads of the meta and single slots, Home snapshots, web writers);
- moves its query types into the store;
- switches every consumer and the two web handlers.

The legacy JSON is not imported yet (Task 4). Until then, `prepare()` refuses to start a history beside it, so a device flashed mid-plan loses nothing.

**Files:**
- Modify: `src/ReadingStats.h` (replaced whole), `src/ReadingStats.cpp`
- Modify: `src/activities/settings/ReadingStatsActivity.h`, `ReadingStatsActivity.cpp`
- Modify: `src/activities/settings/ReadingStatsBookListActivity.h`, `ReadingStatsBookListActivity.cpp`
- Modify: `src/activities/settings/ReadingStatsBookDetailActivity.cpp`, `src/activities/home/BookInfoActivity.cpp`
- Modify: `src/components/BookProgressPresentation.cpp`, `src/ReadingSessionTracker.cpp` (comment)
- Modify: `src/network/CrossPointWebServer.cpp`
- Test: `test/reading_stats/ReadingStatsStoreTest.cpp` (replaced whole), `test/reading_stats/CMakeLists.txt`

**Interfaces:**
- Consumes: everything Task 1 produces; Task 2's `writeFileHead`, `writeDashboardHead`, `writeBook(out, book, eta)`, `writeBookSeparator`, `writeTail`.
- Produces (`ReadingStatsStore`, all public):
  - Result types:
    - `enum class WriteResult : uint8_t { Done, NotFound, NoMemory, Failed }` (unchanged);
    - `enum class ReadResult : uint8_t { Ok, NoMemory, IoError, Corrupt, Stale }`.
  - Query types:
    - `struct IndexEntry { uint32_t totalSeconds; ReadingStatsSlotFile::DocKey key; }`;
    - `struct Summary : ReadingTotals { uint32_t bookCount, finishedBookCount, paceSeconds, pacePercents, seq; std::vector<IndexEntry> byTime; }`;
    - `struct BookQuery { bool found; BookReadingStats book; float pooledPace; }` (unchanged);
    - `struct RecentSnapshot { std::string docId; bool known; uint32_t totalSeconds; uint16_t knownDays; time_t lastReadEpoch; uint8_t progress; }`.
  - Constructor: `explicit ReadingStatsStore(std::string path = ReadingStatsSlotFile::kPath, std::string legacyPath = ReadingStatsFile::kPath)`.
  - Queries:
    - `ReadResult querySummary(Summary& out, bool withIndex = false)`;
    - `ReadResult queryBook(const std::string& docId, BookQuery& out)`;
    - `ReadResult queryBooksAt(const std::vector<IndexEntry>& index, size_t first, size_t count, uint32_t seq, std::vector<BookReadingStats>& books)` replaces `queryBookAt`.
  - Home: `void prefetchRecent(const std::vector<std::string>& docIds)`, `const RecentSnapshot* recent(const std::string&) const`, `float recentPooledPace() const`.
  - Web: `ReadResult writeDashboard(Print& out, uint16_t today)`, `ReadResult writeExport(Print& out)`.
  - Unchanged: `recordSession`, `markFinished`, `removeBook` and every arithmetic static.
  - Gone: `kRecentCacheSize`, `scanFile`, `rememberRecent`, `forgetRecent`, `paceKnown_`.
  - Private, used by Task 4: `ReadResult prepare()`, `bool prepared_`, `std::string legacyPath_`.
  - **Plan decision vs the spec:** the query types are nested in `ReadingStatsStore`, next to `WriteResult` and `BookQuery`, rather than in `ReadingStatsTypes.h`. Only the store makes them, and the JSON layer keeps its own scan types until Task 5 prunes them.
  - **Correction to the spec (§2 updated to match):**
    - **What the spec had:** the list kept each book's slot number and docId, and read a row by slot checked against that docId.
    - **Why that fails:** under copy-on-write, a book's *old* slot keeps a valid copy of the same book after the book moves, so the docId check cannot see the move.
    - **What the plan does:** the list keeps the meta's `seq` from when it took the index. `queryBooksAt` loads the meta once per page. A different `seq` is `Stale`; otherwise it reads each row through the directory.

- [ ] **Step 1: Point the test targets at the slot file**

In `test/reading_stats/CMakeLists.txt`, add `${REPO_ROOT}/src/ReadingStatsSlotFile.cpp` to the sources of **both** `ReadingStatsTest` and `ReadingStatsStoreTest`: after `${REPO_ROOT}/src/ReadingStatsFile.cpp` in each `add_executable`.

- [ ] **Step 2: Write the failing tests**

Replace `test/reading_stats/ReadingStatsStoreTest.cpp` whole with:

```cpp
// ReadingStatsStore against real files: every update, query and web payload on reading-stats.bin,
// through the file-backed HalStorage shim in a temporary directory. The crash tests rebuild the card
// the way a power cut would leave it and check that the history reads as it did before the update.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "HalClock.h"
#include "ReadingStats.h"

// Link stubs: ReadingStats.cpp reaches the clock only for "today".
namespace HalClock {
time_t now() { return 0; }
bool isSynced() { return false; }
}  // namespace HalClock

namespace {

using ReadResult = ReadingStatsStore::ReadResult;
using WriteResult = ReadingStatsStore::WriteResult;

// 2026-01-10 12:00 UTC: day 20463 in any zone within eleven hours of UTC.
constexpr time_t kNoon = 1768046400;
constexpr time_t kDay = 86400;
constexpr size_t kSeqAt = 8;  // the header's seq (ReadingStatsSlotFile.cpp)

// A document id as KOReaderDocumentId makes them: 32 lowercase hex characters.
std::string id(const unsigned n) {
  char text[33];
  snprintf(text, sizeof(text), "%032x", n);
  return text;
}

class StringPrint : public Print {
 public:
  std::string text;
  size_t write(uint8_t b) override {
    text.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    text.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

std::string readBytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void writeBytes(const std::string& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

uint32_t seqOfCopy(const std::string& bytes, const size_t copy) {
  const auto* p = reinterpret_cast<const uint8_t*>(bytes.data() + copy * ReadingStatsSlotFile::kMetaSize + kSeqAt);
  return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

class StoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ =
        std::filesystem::temp_directory_path() / (std::string("rst-") + info->test_suite_name() + "-" + info->name());
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    path_ = (dir_ / "reading-stats.bin").generic_string();
    legacy_ = (dir_ / "reading-stats.json").generic_string();
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  static ReadingStatsStore::Summary summaryOf(ReadingStatsStore& store, const bool withIndex = false) {
    ReadingStatsStore::Summary summary;
    EXPECT_EQ(store.querySummary(summary, withIndex), ReadResult::Ok);
    return summary;
  }

  static ReadingStatsStore::BookQuery bookOf(ReadingStatsStore& store, const std::string& docId) {
    ReadingStatsStore::BookQuery query;
    EXPECT_EQ(store.queryBook(docId, query), ReadResult::Ok);
    return query;
  }

  // Two books: 1000 s at 25 % and 300 s at 40 %, so a global pace of 1300 s / 65 % = 20 s per %.
  static void twoBooks(ReadingStatsStore& store) {
    ASSERT_EQ(store.recordSession(id(1), "Book A", "X", 1000, 17, 25, kNoon), WriteResult::Done);
    ASSERT_EQ(store.recordSession(id(2), "B", "", 300, 5, 40, kNoon), WriteResult::Done);
  }

  // The cap's worth of books, 60 s each; id(7) read longest ago.
  static void fullHistory(ReadingStatsStore& store) {
    for (unsigned i = 0; i < ReadingStatsStore::kMaxBooks; ++i) {
      const time_t wall = i == 7 ? kNoon - kDay : kNoon + static_cast<time_t>(i);
      ASSERT_EQ(store.recordSession(id(i), "T", "", 60, 1, 5, wall), WriteResult::Done);
    }
  }

  std::filesystem::path dir_;
  std::string path_;
  std::string legacy_;
};

}  // namespace

TEST_F(StoreTest, SummaryOfNoHistoryIsEmpty) {
  ReadingStatsStore store(path_, legacy_);

  const auto summary = summaryOf(store);

  EXPECT_EQ(summary.bookCount, 0u);
  EXPECT_EQ(summary.totalSeconds, 0u);
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(StoreTest, FirstSessionCreatesTheFile) {
  ReadingStatsStore store(path_, legacy_);

  ASSERT_EQ(store.recordSession(id(1), "Book A", "X", 600, 10, 20, kNoon), WriteResult::Done);

  EXPECT_EQ(std::filesystem::file_size(path_), ReadingStatsSlotFile::kFileSize);
  const auto query = bookOf(store, id(1));
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 600u);
  EXPECT_EQ(query.book.title, "Book A");
  EXPECT_EQ(query.book.author, "X");
  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 600u);
}

TEST_F(StoreTest, ZeroSecondSessionForANewBookWritesNothing) {
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(store.recordSession(id(1), "Book A", "", 0, 0, 3, kNoon), WriteResult::Done);

  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(StoreTest, SessionsMergeIntoTheirBook) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "Book A", "X", 600, 10, 20, kNoon), WriteResult::Done);
  ASSERT_EQ(store.recordSession(id(2), "B", "", 100, 2, 10, kNoon), WriteResult::Done);

  ASSERT_EQ(store.recordSession(id(1), "Book A", "X", 150, 3, 50, kNoon + kDay), WriteResult::Done);

  const auto query = bookOf(store, id(1));
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 750u);
  EXPECT_EQ(query.book.sessions, 2u);
  EXPECT_EQ(query.book.progress, 50);
  EXPECT_EQ(query.book.days.size(), 2u);
  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 850u);
  EXPECT_EQ(summary.totalSessions, 3u);
}

TEST_F(StoreTest, QueryBookFindsItAndTheGlobalPace) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));

  const auto query = bookOf(store, id(2));

  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 300u);
  EXPECT_FLOAT_EQ(query.pooledPace, 20.0f);
}

TEST_F(StoreTest, QueryBooksAtReadsTheIndexedEntries) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));
  const auto summary = summaryOf(store, /*withIndex=*/true);
  ASSERT_EQ(summary.byTime.size(), 2u);
  std::vector<BookReadingStats> books;

  ASSERT_EQ(store.queryBooksAt(summary.byTime, 0, 2, summary.seq, books), ReadResult::Ok);

  ASSERT_EQ(books.size(), 2u);
  EXPECT_EQ(books[0].docId, id(1));  // 1000 s: first by time
  EXPECT_EQ(books[0].title, "Book A");
  EXPECT_EQ(books[1].docId, id(2));
}

TEST_F(StoreTest, QueryBooksAtNoticesAChangedHistory) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));
  const auto summary = summaryOf(store, /*withIndex=*/true);
  // The book's old slot still holds a valid copy of it: only the meta's seq tells the index is old.
  ASSERT_EQ(store.recordSession(id(1), "Book A", "X", 60, 1, 26, kNoon), WriteResult::Done);
  std::vector<BookReadingStats> books;

  EXPECT_EQ(store.queryBooksAt(summary.byTime, 0, 2, summary.seq, books), ReadResult::Stale);
}

TEST_F(StoreTest, PrefetchReportsKnownAndUnknownBooks) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));

  store.prefetchRecent({id(2), id(9), "not-a-document-id"});

  const auto* b = store.recent(id(2));
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(b->known);
  EXPECT_EQ(b->totalSeconds, 300u);
  EXPECT_EQ(b->progress, 40);
  EXPECT_EQ(b->knownDays, 1);
  ASSERT_NE(store.recent(id(9)), nullptr);
  EXPECT_FALSE(store.recent(id(9))->known);
  ASSERT_NE(store.recent("not-a-document-id"), nullptr);
  EXPECT_FALSE(store.recent("not-a-document-id")->known);
  EXPECT_EQ(store.recent(id(1)), nullptr);  // not asked for
  EXPECT_FLOAT_EQ(store.recentPooledPace(), 20.0f);
}

TEST_F(StoreTest, PrefetchShowsTheLatestFigures) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));
  store.prefetchRecent({id(2)});

  ASSERT_EQ(store.recordSession(id(2), "B", "", 200, 2, 45, kNoon), WriteResult::Done);
  store.prefetchRecent({id(2)});

  ASSERT_NE(store.recent(id(2)), nullptr);
  EXPECT_EQ(store.recent(id(2))->totalSeconds, 500u);
  EXPECT_EQ(store.recent(id(2))->progress, 45);
}

TEST_F(StoreTest, NewBookAtTheCapEvictsTheLeastRecentlyRead) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(fullHistory(store));

  ASSERT_EQ(store.recordSession(id(100), "New", "", 30, 1, 1, kNoon + kDay), WriteResult::Done);

  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, ReadingStatsStore::kMaxBooks);
  // The evicted book's reading still counts: eviction frees the entry, not the history's totals.
  EXPECT_EQ(summary.totalSeconds, 6030u);
  EXPECT_FALSE(bookOf(store, id(7)).found);
  EXPECT_TRUE(bookOf(store, id(100)).found);
}

TEST_F(StoreTest, ManyUpdatesOnAFullHistoryLeakNoSlot) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(fullHistory(store));
  uint32_t seconds = 100 * 60;

  for (unsigned round = 0; round < 300; ++round) {
    // Every third round a book never seen, which evicts; the other two re-read the newest one.
    const unsigned book = 1000 + round - round % 3;
    ASSERT_EQ(store.recordSession(id(book), "T", "", 30, 1, 5, kNoon + 1000 + round), WriteResult::Done);
    seconds += 30;
  }

  const auto summary = summaryOf(store, /*withIndex=*/true);
  EXPECT_EQ(summary.bookCount, ReadingStatsStore::kMaxBooks);
  EXPECT_EQ(summary.totalSeconds, seconds);
  std::vector<BookReadingStats> books;
  ASSERT_EQ(store.queryBooksAt(summary.byTime, 0, summary.byTime.size(), summary.seq, books), ReadResult::Ok);
  ASSERT_EQ(books.size(), ReadingStatsStore::kMaxBooks);
  for (const auto& book : books) EXPECT_EQ(book.title, "T");
  FsFile file;
  ASSERT_TRUE(Storage.openFileForRead("T", path_.c_str(), file));
  auto meta = ReadingStatsSlotFile::Meta::create();
  uint8_t live = ReadingStatsSlotFile::kNoCopy;
  ASSERT_EQ(ReadingStatsSlotFile::loadMeta(file, *meta, live), ReadingStatsSlotFile::Load::Ok);
  EXPECT_EQ(meta->bookCount(), ReadingStatsStore::kMaxBooks);
  EXPECT_NE(meta->freeSlot(), ReadingStatsSlotFile::kNoSlot);
}

TEST_F(StoreTest, MarkFinishedRespectsTheCap) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(fullHistory(store));

  ASSERT_EQ(store.markFinished(id(100), "New", "", kNoon + kDay), WriteResult::Done);

  EXPECT_EQ(summaryOf(store).bookCount, ReadingStatsStore::kMaxBooks);
  EXPECT_FALSE(bookOf(store, id(7)).found);
}

TEST_F(StoreTest, MarkFinishedCountsAndCreatesTheEntry) {
  ReadingStatsStore store(path_, legacy_);

  ASSERT_EQ(store.markFinished(id(1), "Book A", "", kNoon), WriteResult::Done);

  const auto query = bookOf(store, id(1));
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.finishedCount, 1);
  EXPECT_EQ(query.book.progress, 100);
  EXPECT_EQ(summaryOf(store).finishedBookCount, 1u);
}

TEST_F(StoreTest, MarkFinishedOnABookWithRealReading) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "Real", "A", 5400, 90, 97, kNoon), WriteResult::Done);

  ASSERT_EQ(store.markFinished(id(1), "Real", "A", kNoon + kDay), WriteResult::Done);

  const auto query = bookOf(store, id(1));
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.finishedCount, 1);
  EXPECT_EQ(query.book.lastFinishedEpoch, kNoon + kDay);
  EXPECT_EQ(query.book.totalSeconds, 5400u);
}

TEST_F(StoreTest, SessionOnAFinishedBookIsRecorded) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "Real", "A", 5400, 90, 100, kNoon), WriteResult::Done);
  ASSERT_EQ(store.markFinished(id(1), "Real", "A", kNoon), WriteResult::Done);

  ASSERT_EQ(store.recordSession(id(1), "Real", "A", 600, 10, 100, kNoon + kDay), WriteResult::Done);

  const auto query = bookOf(store, id(1));
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 6000u);
  EXPECT_EQ(query.book.finishedCount, 1);
}

TEST_F(StoreTest, RemoveTakesTheBookOutOfTheTotals) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));

  ASSERT_EQ(store.removeBook(id(1)), WriteResult::Done);

  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 300u);
  EXPECT_EQ(summary.totalSessions, 1u);
  EXPECT_EQ(store.removeBook(id(1)), WriteResult::NotFound);
}

TEST_F(StoreTest, RemoveFromNoHistoryIsNotFound) {
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(store.removeBook(id(1)), WriteResult::NotFound);

  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(StoreTest, WhatIsNotADocumentIdIsNeverStored) {
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(store.recordSession("a", "A", "", 60, 1, 1, kNoon), WriteResult::Failed);
  EXPECT_EQ(store.removeBook("a"), WriteResult::NotFound);
  EXPECT_FALSE(bookOf(store, "a").found);
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(StoreTest, CorruptFileIsSetAsideAndAFreshHistoryStarts) {
  writeBytes(path_, "junk");
  ReadingStatsStore store(path_, legacy_);
  ReadingStatsStore::Summary summary;
  EXPECT_EQ(store.querySummary(summary), ReadResult::Corrupt);

  ASSERT_EQ(store.recordSession(id(1), "Book A", "", 60, 1, 1, kNoon), WriteResult::Done);

  EXPECT_EQ(readBytes((dir_ / "reading-stats.corrupt.bin").generic_string()), "junk");
  summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 60u);
}

TEST_F(StoreTest, RemoveOnACorruptFileFailsAndLeavesIt) {
  writeBytes(path_, "junk");
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(store.removeBook(id(1)), WriteResult::Failed);

  EXPECT_EQ(readBytes(path_), "junk");
}

TEST_F(StoreTest, AwkwardTitlesSurviveExactly) {
  ReadingStatsStore store(path_, legacy_);
  const std::string title = "Say \"hi\" \\ back\nslash";
  const std::string control = std::string("ctl") + '\x01';

  ASSERT_EQ(store.recordSession(id(1), title, "", 60, 1, 1, kNoon), WriteResult::Done);
  ASSERT_EQ(store.recordSession(id(2), control, "", 60, 1, 1, kNoon), WriteResult::Done);

  EXPECT_EQ(bookOf(store, id(1)).book.title, title);
  EXPECT_EQ(bookOf(store, id(2)).book.title, control);
}

TEST_F(StoreTest, LongTitleIsCutWhole) {
  ReadingStatsStore store(path_, legacy_);
  const std::string e = "\xC3\xA9";
  const std::string title = std::string(ReadingStatsSlotFile::kTitleMax - 1, 't') + e + "tail";

  ASSERT_EQ(store.recordSession(id(1), title, "", 60, 1, 1, kNoon), WriteResult::Done);

  EXPECT_EQ(bookOf(store, id(1)).book.title, std::string(ReadingStatsSlotFile::kTitleMax - 1, 't'));
}

TEST_F(StoreTest, CrashAfterTheSlotWriteLeavesTheOldHistory) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "A", "", 600, 5, 10, kNoon), WriteResult::Done);
  const std::string before = readBytes(path_);
  ASSERT_EQ(store.recordSession(id(2), "B", "", 300, 3, 5, kNoon), WriteResult::Done);

  // Power lost after the slot write, before the meta write: the new slot is on the card, the
  // meta copies are as they were.
  std::string cut = readBytes(path_);
  cut.replace(0, ReadingStatsSlotFile::kSlotsOffset, before, 0, ReadingStatsSlotFile::kSlotsOffset);
  writeBytes(path_, cut);

  auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 600u);
  EXPECT_FALSE(bookOf(store, id(2)).found);
  // And the history takes the next session as if nothing had happened.
  ASSERT_EQ(store.recordSession(id(2), "B", "", 300, 3, 5, kNoon), WriteResult::Done);
  summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 900u);
}

TEST_F(StoreTest, TornMetaFallsBackToTheOlderCopy) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "A", "", 600, 5, 10, kNoon), WriteResult::Done);
  ASSERT_EQ(store.recordSession(id(2), "B", "", 300, 3, 5, kNoon), WriteResult::Done);

  // The meta write of the second session torn by a power cut: its copy fails its CRC.
  std::string torn = readBytes(path_);
  const size_t newest = seqOfCopy(torn, 1) > seqOfCopy(torn, 0) ? 1 : 0;
  torn[newest * ReadingStatsSlotFile::kMetaSize + 100] ^= 0x01;
  writeBytes(path_, torn);

  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 600u);
  EXPECT_TRUE(bookOf(store, id(1)).found);
}

TEST_F(StoreTest, TornMetaThenAnotherSessionKeepsGoing) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_EQ(store.recordSession(id(1), "A", "", 600, 5, 10, kNoon), WriteResult::Done);
  ASSERT_EQ(store.recordSession(id(2), "B", "", 300, 3, 5, kNoon), WriteResult::Done);
  std::string torn = readBytes(path_);
  const size_t newest = seqOfCopy(torn, 1) > seqOfCopy(torn, 0) ? 1 : 0;
  torn[newest * ReadingStatsSlotFile::kMetaSize + 100] ^= 0x01;
  writeBytes(path_, torn);

  ASSERT_EQ(store.recordSession(id(3), "C", "", 50, 1, 5, kNoon), WriteResult::Done);

  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 650u);
  EXPECT_TRUE(bookOf(store, id(1)).found);
  EXPECT_TRUE(bookOf(store, id(3)).found);
}

TEST_F(StoreTest, StaleTemporaryFileIsDiscarded) {
  writeBytes(path_ + ".tmp", "half a history");
  ReadingStatsStore store(path_, legacy_);

  ASSERT_EQ(store.recordSession(id(1), "A", "", 60, 1, 1, kNoon), WriteResult::Done);

  EXPECT_FALSE(std::filesystem::exists(path_ + ".tmp"));
  EXPECT_EQ(summaryOf(store).bookCount, 1u);
}

TEST_F(StoreTest, LegacyHistoryIsLeftAloneUntilImported) {
  writeBytes(legacy_, "{}");
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(store.recordSession(id(1), "A", "", 60, 1, 1, kNoon), WriteResult::Failed);

  EXPECT_FALSE(std::filesystem::exists(path_));
  EXPECT_EQ(readBytes(legacy_), "{}");
}

TEST_F(StoreTest, DashboardGivesEveryBookItsTimeToFinish) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));
  // At 1 % no pace of its own: it takes the global one.
  ASSERT_EQ(store.recordSession(id(3), "C", "", 50, 1, 1, kNoon), WriteResult::Done);
  StringPrint out;

  ASSERT_EQ(store.writeDashboard(out, 0), ReadResult::Ok);

  EXPECT_EQ(out.text.rfind(R"({"totalSeconds":1350,"totalSessions":3,"totalPagesTurned":23,"bookCount":3,)"
                           R"("finishedBookCount":0,"todayDayIndex":0,)",
                           0),
            0u);
  // A: 1000 s / 25 % = 40 s/% x 75 %. B: 300 s / 40 % = 7.5 s/% x 60 %. C: 20 s/% x 99 %.
  EXPECT_NE(out.text.find(R"("etaSeconds":3000})"), std::string::npos);
  EXPECT_NE(out.text.find(R"("etaSeconds":450})"), std::string::npos);
  EXPECT_NE(out.text.find(R"("etaSeconds":1980})"), std::string::npos);
  EXPECT_EQ(out.text.substr(out.text.size() - 2), "]}");
}

TEST_F(StoreTest, ExportIsTheJsonFileFormat) {
  ReadingStatsStore store(path_, legacy_);
  ASSERT_NO_FATAL_FAILURE(twoBooks(store));
  const auto summary = summaryOf(store);
  StringPrint expected;
  ReadingStatsFile::writeFileHead(expected, summary);
  ReadingStatsFile::writeBook(expected, bookOf(store, id(1)).book);
  ReadingStatsFile::writeBookSeparator(expected);
  ReadingStatsFile::writeBook(expected, bookOf(store, id(2)).book);
  ReadingStatsFile::writeTail(expected);
  StringPrint out;

  ASSERT_EQ(store.writeExport(out), ReadResult::Ok);

  EXPECT_EQ(out.text, expected.text);
  EXPECT_EQ(out.text.rfind(R"({"totalSeconds":1300,"totalSessions":2,"totalPagesTurned":22,)", 0), 0u);
}

TEST_F(StoreTest, ExportOfNoHistoryIsAnEmptyFile) {
  ReadingStatsStore store(path_, legacy_);
  StringPrint out;

  ASSERT_EQ(store.writeExport(out), ReadResult::Ok);

  EXPECT_EQ(out.text, R"({"totalSeconds":0,"totalSessions":0,"totalPagesTurned":0,"longestStreak":0,)"
                      R"("globalDays":[],"books":[]})");
}
```

- [ ] **Step 3: Run the tests to watch them fail**

Run: `cmake -S test -B build/test > /dev/null && cmake --build build/test --target ReadingStatsStoreTest 2>&1 | grep -E "error" | head -5`
Expected: FAIL to compile. The errors name `ReadResult`, `writeExport`, `writeDashboard` and the two-argument constructor, none of which is a member of `ReadingStatsStore` yet.

- [ ] **Step 4: Replace the store header**

Replace `src/ReadingStats.h` whole with:

```cpp
#pragma once
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "ReadingStatsFile.h"
#include "ReadingStatsSlotFile.h"
#include "ReadingStatsTypes.h"

// Helpers — both return 0 when HalClock is unsynced (caller should skip).
uint16_t localDayIndexFromEpoch(time_t epoch);
uint16_t currentLocalDayIndex();

// The reading history: /.crosspoint/reading-stats.bin (ReadingStatsSlotFile), one file with a fixed
// place for every book. An update writes that book and the global figures, copy-on-write, never
// the whole history: rewriting the old single JSON file took 2.7 s at a worst-case session end on
// the X3. A reading-stats.json left by older firmware is imported into it once (prepare()).
//
// The store holds no history: each call reads what it needs (the ~6 KB meta, a slot or two) and
// lets it go. The only resident state is Home's snapshots (prefetchRecent). Call from the loop task,
// never from render().
//
// Deleting a book from the card never touches its history: only removeBook() takes a book out.
class ReadingStatsStore {
  static ReadingStatsStore instance;

 public:
  static ReadingStatsStore& getInstance() { return instance; }

  explicit ReadingStatsStore(std::string path = ReadingStatsSlotFile::kPath,
                             std::string legacyPath = ReadingStatsFile::kPath)
      : path_(std::move(path)), legacyPath_(std::move(legacyPath)) {}

  // ---- Updates ----------------------------------------------------------------------------------
  enum class WriteResult : uint8_t { Done, NotFound, NoMemory, Failed };

  // A finished session: creates the book's entry when the session has seconds (a book opened and
  // closed without reading is not history), evicting the least recently read book past kMaxBooks;
  // a zero-second session still moves an existing book's progress.
  WriteResult recordSession(const std::string& docId, const std::string& title, const std::string& author,
                            uint32_t sessionSeconds, uint32_t sessionPagesTurned, uint8_t progress,
                            time_t walltimeEpoch);

  // One more finish; creates the entry when needed, under the same cap.
  WriteResult markFinished(const std::string& docId, const std::string& title, const std::string& author,
                           time_t walltimeEpoch);

  // Forget one book: its entry goes and its time, sessions, pages and day buckets come back out of
  // the totals, so the stats read as if it had never been read. Exact for everything the screens
  // show — a book keeps its newest kMaxBookDays buckets, which always cover the 30-day sparkline
  // and the current streak. The persisted longest-streak record stays.
  WriteResult removeBook(const std::string& docId);

  // ---- Queries ----------------------------------------------------------------------------------
  //
  // Each reads the history and returns only what was asked for; nothing stays resident. No history
  // file is an empty history (Ok). Corrupt: the file is unreadable; the next update sets it aside
  // and starts a fresh history.
  enum class ReadResult : uint8_t { Ok, NoMemory, IoError, Corrupt, Stale };

  // A book in the time order.
  struct IndexEntry {
    uint32_t totalSeconds = 0;
    ReadingStatsSlotFile::DocKey key{};
  };

  struct Summary : ReadingTotals {
    uint32_t bookCount = 0;
    uint32_t finishedBookCount = 0;
    // Over the books far enough in to count toward the global pace.
    uint32_t paceSeconds = 0;
    uint32_t pacePercents = 0;
    // The history's generation when read (0: no history). Every update raises it; queryBooksAt()
    // compares it to tell that the index went stale.
    uint32_t seq = 0;
    // withIndex only: descending by time; equal times in directory order.
    std::vector<IndexEntry> byTime;
  };

  struct BookQuery {
    bool found = false;
    BookReadingStats book;
    float pooledPace = 0.0f;  // the global pace, for a book without one of its own
  };

  // What Home draws for one recent book.
  struct RecentSnapshot {
    std::string docId;
    bool known = false;  // false: the history holds no entry for this book
    uint32_t totalSeconds = 0;
    uint16_t knownDays = 0;  // dated reading days the book keeps (at most kMaxBookDays)
    time_t lastReadEpoch = 0;
    uint8_t progress = 0;
  };

  ReadResult querySummary(Summary& out, bool withIndex = false);
  ReadResult queryBook(const std::string& docId, BookQuery& out);
  // The books index[first, first + count) names, read with one load of the meta. Stale when the
  // history changed since the index was taken (`seq`, from its Summary): take the index again.
  // A book gone in between comes back empty.
  ReadResult queryBooksAt(const std::vector<IndexEntry>& index, size_t first, size_t count, uint32_t seq,
                          std::vector<BookReadingStats>& books);

  // ---- Home -------------------------------------------------------------------------------------
  //
  // Home calls prefetchRecent() on entry: one read of the meta gives a snapshot for each book asked
  // for (known or not) and the global pace, so the themes read no file from render(). They stay
  // until the next prefetch.
  void prefetchRecent(const std::vector<std::string>& docIds);
  const RecentSnapshot* recent(const std::string& docId) const;
  float recentPooledPace() const { return pooledPace_; }

  // ---- The web ----------------------------------------------------------------------------------
  //
  // Generated one book at a time: the meta and one slot, whatever the history holds.
  // The /api/stats payload: the figures, streaks when `today` is known, every book with its
  // time-to-finish estimate (etaSeconds).
  ReadResult writeDashboard(Print& out, uint16_t today);
  // The history in the reading-stats.json format, which older firmware reads.
  ReadResult writeExport(Print& out);

  // ---- Reading speed / time-to-finish -----------------------------------------------------------
  //
  // Average seconds spent reading per 1% of book progress: a book's own once it has covered enough
  // ground to be meaningful, else the global average over the books that have. The minimum-progress
  // gate keeps 30 seconds at 2% from extrapolating to a 25-minute book.
  static constexpr uint8_t MIN_BOOK_PROGRESS_FOR_PERSONAL_RATE = 3;  // %
  static constexpr uint32_t MIN_GLOBAL_SECONDS_FOR_RATE = 60;        // s

  // ---- The arithmetic, on plain data ------------------------------------------------------------
  //
  // Shared by the writes, the screens and the web, so the device screens and the web dashboard
  // agree on every figure.
  static uint32_t secondsOn(const std::vector<DayBucket>& days, uint16_t dayIndex);
  static uint16_t currentStreakIn(const std::vector<DayBucket>& days, uint16_t today);
  // The longest run in `days`, or the persisted `record` when that is longer.
  static uint16_t longestStreakIn(const std::vector<DayBucket>& days, uint16_t record);
  static bool countsTowardPace(const uint8_t progress) { return progress >= MIN_BOOK_PROGRESS_FOR_PERSONAL_RATE; }
  // The cap's eviction order: the least recently read book goes first (an entry never read with
  // the clock set, lastReadEpoch 0, is the oldest); on the same date, the one with less time.
  static bool evictsBefore(time_t aLastRead, uint32_t aSeconds, time_t bLastRead, uint32_t bSeconds);
  // The global pace from the sums over the books that count toward it; 0 below
  // MIN_GLOBAL_SECONDS_FOR_RATE of reading overall.
  static float pooledSecondsPerPercent(uint32_t globalTotalSeconds, uint32_t countedSeconds, uint32_t countedPercents);
  // A book's own pace, or 0 when it has not covered enough ground to have one.
  static float ownSecondsPerPercent(uint32_t totalSeconds, uint8_t progress);
  static uint32_t etaSeconds(float secondsPerPercent, float remainingPercent);
  // Past the caps, the oldest buckets go; the global trim first folds the streak they held into
  // `record`, since the record may live in them.
  static void trimGlobalDays(std::vector<DayBucket>& days, uint16_t& record);
  static void trimBookDays(std::vector<DayBucket>& days);
  // Takes one book's contribution back out of the global figures (removal's arithmetic).
  static void takeOut(const BookReadingStats& book, ReadingTotals& totals);
  // A finished session applied to one book and the global figures (the session end's arithmetic):
  // counters, the book's and the global day buckets, the streak record. `title` / `author` replace
  // the book's only when non-empty.
  static void applySession(BookReadingStats& book, ReadingTotals& totals, const std::string& title,
                           const std::string& author, uint32_t sessionSeconds, uint32_t sessionPagesTurned,
                           uint8_t progress, time_t walltimeEpoch);
  // One more finish of the book.
  static void applyFinish(BookReadingStats& book, const std::string& title, const std::string& author,
                          time_t walltimeEpoch);

  // Bounds (memory audit 2026-09, R8). Past these caps the least recently read book goes, and the
  // oldest day buckets go; the sparkline needs 30 days and the streak walk needs the current run,
  // both well inside the global window, and the longest streak is kept as a number. The file's
  // layout holds exactly these (static_asserts below).
  static constexpr size_t kMaxBooks = 100;
  static constexpr size_t kMaxBookDays = 60;
  static constexpr size_t kMaxGlobalDays = 400;

 private:
  enum class Edit : uint8_t { Session, Finish, Remove };
  // The update shared by the three writes. `apply` changes the book and the totals and returns
  // false when there is nothing to write.
  WriteResult write(Edit edit, const std::string& docId,
                    const std::function<bool(BookReadingStats& book, ReadingTotals& totals, bool existed)>& apply);
  // Before anything reads the history: a create or an import cut short is discarded, and the
  // legacy JSON imported once. Ok from its first success on.
  ReadResult prepare();
  // The history file open for reading with its meta loaded; `exists` false when there is none.
  ReadResult open(FsFile& file, ReadingStatsSlotFile::Meta& meta, bool& exists);
  // A new, empty history file, written as a temporary file and renamed into place, so a card never
  // holds half of one. Leaves `meta` as written: empty, seq 1, in copy A.
  bool createFresh(ReadingStatsSlotFile::Meta& meta);
  // Both web payloads: the head from the meta, then every book from its slot.
  ReadResult writeJson(Print& out, uint16_t today, bool dashboard);

  std::string path_;
  std::string legacyPath_;
  bool prepared_ = false;
  std::vector<RecentSnapshot> recent_;
  float pooledPace_ = 0.0f;
};

static_assert(ReadingStatsStore::kMaxBooks == ReadingStatsSlotFile::kEntryCount, "the directory holds the cap");
static_assert(ReadingStatsStore::kMaxBookDays == ReadingStatsSlotFile::kBookDayCapacity, "a slot holds the day cap");
static_assert(ReadingStatsStore::kMaxGlobalDays == ReadingStatsSlotFile::kGlobalDayCapacity,
              "the meta holds the global day cap");

#define READING_STATS ReadingStatsStore::getInstance()
```

- [ ] **Step 5: Rewrite the store's file handling**

In `src/ReadingStats.cpp`, make four edits.

**(a) Includes.** Replace the include block at the top of the file with:

```cpp
#include "ReadingStats.h"

#include <Arduino.h>  // millis()
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <ctime>

#include "ReadingStatsFile.h"
#include "ReadingStatsSlotFile.h"
```

**(b) Remove the old file helpers.** In the first anonymous namespace, delete four things whole, each with its comment block:
- `swapIn`;
- `recoverInterruptedSwap`;
- `kWriteChunk`;
- the `TimedPrint` class.

Replace `asidePathFor` (with its comment) with:

```cpp
// Where a history that cannot be read is set aside: reading-stats.bin -> reading-stats.corrupt.bin,
// reading-stats.json -> reading-stats.corrupt.json.
std::string asidePathFor(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path + ".corrupt";
  return path.substr(0, dot) + ".corrupt" + path.substr(dot);
}
```

Keep these unchanged: `parentDirOf`, `mergeDay`, `unmergeDay`, `runEndingAt`, `dayIndexFromLocaltime`, and everything from `localDayIndexFromEpoch` through `applyFinish`.

**(c) Replace everything after `applyFinish`.** Replace everything from the line `// ---- Updates ----…` to the end of the file with:

```cpp
namespace {

using ReadingStatsSlotFile::DocKey;
using ReadingStatsSlotFile::Entry;
using ReadingStatsSlotFile::kEntryCount;
using ReadingStatsSlotFile::kNoCopy;
using ReadingStatsSlotFile::kNoSlot;
using ReadingStatsSlotFile::kSlotSize;
using ReadingStatsSlotFile::Load;
using ReadingStatsSlotFile::Meta;

// Sets an unreadable file aside for forensics, rather than lose it or stall on it for ever.
bool setAside(const std::string& path) {
  const std::string aside = asidePathFor(path);
  Storage.remove(aside.c_str());
  if (!Storage.rename(path.c_str(), aside.c_str())) {
    LOG_ERR("RST", "%s unreadable and could not be set aside; left as is", path.c_str());
    return false;
  }
  LOG_ERR("RST", "%s unreadable; set aside as %s", path.c_str(), aside.c_str());
  return true;
}

// What survives of a book whose slot fails its check: the directory's figures. The title, author and
// days are lost; the book's next update writes a whole slot again.
BookReadingStats bookFromEntry(const Entry& entry) {
  BookReadingStats book;
  book.docId = ReadingStatsSlotFile::formatDocId(entry.key);
  book.totalSeconds = entry.totalSeconds;
  book.progress = entry.progress;
  book.finishedCount = entry.finishedCount;
  book.lastReadEpoch = static_cast<time_t>(entry.lastReadEpoch);
  return book;
}

// The book an entry names: from its slot, or from the directory when the slot is damaged.
void readBook(FsFile& file, const Entry& entry, uint8_t* image, BookReadingStats& book) {
  DocKey key{};
  if (ReadingStatsSlotFile::readSlot(file, entry.slot, image) && ReadingStatsSlotFile::decodeSlot(image, key, book) &&
      key == entry.key) {
    return;
  }
  LOG_ERR("RST", "Slot %u fails its check; %s keeps only its directory figures", static_cast<unsigned>(entry.slot),
          ReadingStatsSlotFile::formatDocId(entry.key).c_str());
  book = bookFromEntry(entry);
}

// The global pace from the directory alone.
float pooledPaceOf(const Meta& meta) {
  uint32_t seconds = 0;
  uint32_t percents = 0;
  for (size_t i = 0; i < kEntryCount; ++i) {
    const Entry entry = meta.entry(i);
    if (entry.used() && ReadingStatsStore::countsTowardPace(entry.progress)) {
      seconds += entry.totalSeconds;
      percents += entry.progress;
    }
  }
  return ReadingStatsStore::pooledSecondsPerPercent(meta.totalSeconds(), seconds, percents);
}

void summarize(const Meta& meta, ReadingStatsStore::Summary& out, const bool withIndex) {
  meta.readTotals(out);
  out.seq = meta.seq();
  if (withIndex) out.byTime.reserve(meta.bookCount());
  for (size_t i = 0; i < kEntryCount; ++i) {
    const Entry entry = meta.entry(i);
    if (!entry.used()) continue;
    ++out.bookCount;
    if (entry.finishedCount > 0) ++out.finishedBookCount;
    if (ReadingStatsStore::countsTowardPace(entry.progress)) {
      out.paceSeconds += entry.totalSeconds;
      out.pacePercents += entry.progress;
    }
    if (withIndex) out.byTime.push_back({entry.totalSeconds, entry.key});
  }
  std::stable_sort(out.byTime.begin(), out.byTime.end(),
                   [](const ReadingStatsStore::IndexEntry& a, const ReadingStatsStore::IndexEntry& b) {
                     return a.totalSeconds > b.totalSeconds;
                   });
}

// The entry the book cap evicts: the least recently read, ties to less time; the first of equals.
size_t victimOf(const Meta& meta) {
  size_t victim = kEntryCount;
  Entry worst;
  for (size_t i = 0; i < kEntryCount; ++i) {
    const Entry entry = meta.entry(i);
    if (!entry.used()) continue;
    if (victim == kEntryCount ||
        ReadingStatsStore::evictsBefore(static_cast<time_t>(entry.lastReadEpoch), entry.totalSeconds,
                                        static_cast<time_t>(worst.lastReadEpoch), worst.totalSeconds)) {
      victim = i;
      worst = entry;
    }
  }
  return victim;
}

}  // namespace

// ---- Updates ----------------------------------------------------------------------------------

ReadingStatsStore::WriteResult ReadingStatsStore::recordSession(const std::string& docId, const std::string& title,
                                                                const std::string& author,
                                                                const uint32_t sessionSeconds,
                                                                const uint32_t sessionPagesTurned,
                                                                const uint8_t progress, const time_t walltimeEpoch) {
  return write(Edit::Session, docId, [&](BookReadingStats& book, ReadingTotals& totals, const bool existed) {
    if (!existed && sessionSeconds == 0) return false;  // nothing to record
    applySession(book, totals, title, author, sessionSeconds, sessionPagesTurned, progress, walltimeEpoch);
    return true;
  });
}

ReadingStatsStore::WriteResult ReadingStatsStore::markFinished(const std::string& docId, const std::string& title,
                                                               const std::string& author, const time_t walltimeEpoch) {
  return write(Edit::Finish, docId, [&](BookReadingStats& book, ReadingTotals&, bool) {
    applyFinish(book, title, author, walltimeEpoch);
    return true;
  });
}

ReadingStatsStore::WriteResult ReadingStatsStore::removeBook(const std::string& docId) {
  return write(Edit::Remove, docId, [](BookReadingStats& book, ReadingTotals& totals, bool) {
    takeOut(book, totals);
    return true;
  });
}

ReadingStatsStore::WriteResult ReadingStatsStore::write(
    const Edit edit, const std::string& docId,
    const std::function<bool(BookReadingStats&, ReadingTotals&, bool)>& apply) {
  DocKey key{};
  if (!ReadingStatsSlotFile::parseDocId(docId, key)) {
    // KOReaderDocumentId always gives 32 hex characters; anything else is no book of ours.
    LOG_ERR("RST", "Not a document id: '%s'", docId.c_str());
    return edit == Edit::Remove ? WriteResult::NotFound : WriteResult::Failed;
  }
  [[maybe_unused]] const uint32_t started = millis();
  const ReadResult prepared = prepare();
  if (prepared == ReadResult::NoMemory) return WriteResult::NoMemory;
  if (prepared != ReadResult::Ok) return WriteResult::Failed;
  auto meta = Meta::create();
  auto image = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!meta || !image) {
    LOG_ERR("RST", "OOM: no room for the meta and a slot; history left as is");
    return WriteResult::NoMemory;
  }

  // 1. The history as it stands, and the book in it.
  uint8_t live = kNoCopy;
  bool fresh = true;  // no readable history: a write that adds reading starts one
  size_t index = kEntryCount;
  BookReadingStats book;
  if (Storage.exists(path_.c_str())) {
    FsFile file;
    if (!Storage.openFileForRead("RST", path_.c_str(), file)) return WriteResult::Failed;
    switch (ReadingStatsSlotFile::loadMeta(file, *meta, live)) {
      case Load::IoError:
        return WriteResult::Failed;
      case Load::Corrupt:
        // A removal has nothing to remove from an unreadable file; only a write that adds reading
        // starts a fresh history over it.
        if (edit == Edit::Remove) return WriteResult::Failed;
        break;
      case Load::Ok:
        fresh = false;
        index = meta->find(key);
        if (index != kEntryCount) readBook(file, meta->entry(index), image.get(), book);
        break;
    }
  }
  if (fresh) meta->clear();
  const bool existed = index != kEntryCount;
  if (!existed && edit == Edit::Remove) return WriteResult::NotFound;
  if (!existed) book.docId = docId;

  // 2. Merge the one book in memory.
  ReadingTotals totals;
  meta->readTotals(totals);
  if (!apply(book, totals, existed)) return WriteResult::Done;

  // 3. A history file to write into.
  if (fresh) {
    if (Storage.exists(path_.c_str()) && !setAside(path_)) return WriteResult::Failed;
    if (!createFresh(*meta)) return WriteResult::Failed;
    live = 0;
  }
  FsFile file;
  if (!Storage.openFileForUpdate("RST", path_.c_str(), file)) return WriteResult::Failed;
  [[maybe_unused]] const uint32_t loaded = millis();

  // 4. Copy-on-write: the book into a slot no entry refers to, then the meta into the copy that is
  // not the live one. Power lost before the meta is whole leaves the live copy, which never
  // referred to that slot: the update is lost, the history is not.
  if (edit == Edit::Remove) {
    meta->setEntry(index, Entry{});
  } else {
    // Chosen while the book's old slot, and the cap's victim's, are still referred to.
    const uint8_t slot = meta->freeSlot();
    if (slot == kNoSlot) {
      LOG_ERR("RST", "No free slot; history left as is");
      return WriteResult::Failed;
    }
    if (!existed) {
      index = meta->freeEntry();
      if (index == kEntryCount) {
        index = victimOf(*meta);
        LOG_INF("RST", "Book cap (%u) reached; dropping the least recently read: %s",
                static_cast<unsigned>(kMaxBooks), ReadingStatsSlotFile::formatDocId(meta->entry(index).key).c_str());
      }
    }
    ReadingStatsSlotFile::encodeSlot(key, book, image.get());
    if (!ReadingStatsSlotFile::writeSlot(file, slot, image.get())) return WriteResult::Failed;
    file.flush();  // on the card before any meta refers to it
    meta->setEntry(index, ReadingStatsSlotFile::entryFor(key, book, slot));
  }
  [[maybe_unused]] const uint32_t slotted = millis();
  if (!meta->writeTotals(totals)) {
    LOG_ERR("RST", "More global days than the file holds; history left as is");
    return WriteResult::Failed;
  }
  meta->setSeq(meta->seq() + 1);
  meta->seal();
  if (!ReadingStatsSlotFile::writeMeta(file, *meta, live == 0 ? 1 : 0)) return WriteResult::Failed;
  file.flush();
  [[maybe_unused]] const uint32_t done = millis();
  LOG_INF("RST", "write done in %lu ms: load %lu, slot %lu, meta %lu (%u books, free=%lu contig=%lu)",
          static_cast<unsigned long>(done - started), static_cast<unsigned long>(loaded - started),
          static_cast<unsigned long>(slotted - loaded), static_cast<unsigned long>(done - slotted),
          static_cast<unsigned>(meta->bookCount()), static_cast<unsigned long>(esp_get_free_heap_size()),
          static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT)));
  return WriteResult::Done;
}

// ---- The history file -------------------------------------------------------------------------

ReadingStatsStore::ReadResult ReadingStatsStore::prepare() {
  if (prepared_) return ReadResult::Ok;
  // A create or an import cut short leaves only its temporary file, never half a history.
  const std::string tmpPath = path_ + ".tmp";
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  if (!Storage.exists(path_.c_str()) && Storage.exists(legacyPath_.c_str())) {
    // Not imported yet (the import comes with the next task): nothing may start a history beside
    // the old one, or the import would never run.
    LOG_ERR("RST", "%s is not imported yet; the history is left as is", legacyPath_.c_str());
    return ReadResult::IoError;
  }
  prepared_ = true;
  return ReadResult::Ok;
}

ReadingStatsStore::ReadResult ReadingStatsStore::open(FsFile& file, Meta& meta, bool& exists) {
  exists = false;
  if (!Storage.exists(path_.c_str())) return ReadResult::Ok;  // no history yet
  if (!Storage.openFileForRead("RST", path_.c_str(), file)) return ReadResult::IoError;
  uint8_t live = kNoCopy;
  switch (ReadingStatsSlotFile::loadMeta(file, meta, live)) {
    case Load::Ok:
      exists = true;
      return ReadResult::Ok;
    case Load::Corrupt:
      return ReadResult::Corrupt;
    case Load::IoError:
      break;
  }
  return ReadResult::IoError;
}

bool ReadingStatsStore::createFresh(Meta& meta) {
  Storage.mkdir(parentDirOf(path_).c_str());
  const std::string tmpPath = path_ + ".tmp";
  meta.clear();
  meta.setSeq(1);
  meta.seal();
  bool written = ReadingStatsSlotFile::createZeroed(tmpPath.c_str());
  if (written) {
    FsFile file;
    written = Storage.openFileForUpdate("RST", tmpPath.c_str(), file) && ReadingStatsSlotFile::writeMeta(file, meta, 0);
    if (written) file.flush();
  }
  if (!written || !Storage.rename(tmpPath.c_str(), path_.c_str())) {
    LOG_ERR("RST", "Could not create %s", path_.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

// ---- Queries ----------------------------------------------------------------------------------

ReadingStatsStore::ReadResult ReadingStatsStore::querySummary(Summary& out, const bool withIndex) {
  out = Summary{};
  const ReadResult prepared = prepare();
  if (prepared != ReadResult::Ok) return prepared;
  auto meta = Meta::create();
  if (!meta) return ReadResult::NoMemory;
  FsFile file;
  bool exists = false;
  const ReadResult result = open(file, *meta, exists);
  if (result == ReadResult::Ok && exists) summarize(*meta, out, withIndex);
  return result;
}

ReadingStatsStore::ReadResult ReadingStatsStore::queryBook(const std::string& docId, BookQuery& out) {
  out = BookQuery{};
  DocKey key{};
  if (!ReadingStatsSlotFile::parseDocId(docId, key)) return ReadResult::Ok;  // no book of ours
  const ReadResult prepared = prepare();
  if (prepared != ReadResult::Ok) return prepared;
  auto meta = Meta::create();
  auto image = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!meta || !image) return ReadResult::NoMemory;
  FsFile file;
  bool exists = false;
  const ReadResult result = open(file, *meta, exists);
  if (result != ReadResult::Ok || !exists) return result;
  out.pooledPace = pooledPaceOf(*meta);
  const size_t index = meta->find(key);
  if (index == kEntryCount) return ReadResult::Ok;
  readBook(file, meta->entry(index), image.get(), out.book);
  out.found = true;
  return ReadResult::Ok;
}

ReadingStatsStore::ReadResult ReadingStatsStore::queryBooksAt(const std::vector<IndexEntry>& index,
                                                              const size_t first, const size_t count,
                                                              const uint32_t seq,
                                                              std::vector<BookReadingStats>& books) {
  books.clear();
  const size_t last = std::min(index.size(), first + count);
  // The rows outlive this call: on the heap before the meta's transient 6 KB (see prefetchRecent).
  books.reserve(last > first ? last - first : 0);
  const ReadResult prepared = prepare();
  if (prepared != ReadResult::Ok) return prepared;
  auto meta = Meta::create();
  auto image = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!meta || !image) return ReadResult::NoMemory;
  FsFile file;
  bool exists = false;
  const ReadResult result = open(file, *meta, exists);
  if (result != ReadResult::Ok) return result;
  // A book's old slot keeps a valid copy of it after the book moves, so only the generation can
  // tell the index is old.
  if ((exists ? meta->seq() : 0) != seq) return ReadResult::Stale;
  for (size_t i = first; i < last; ++i) {
    BookReadingStats book;
    const size_t at = meta->find(index[i].key);
    if (at != kEntryCount) readBook(file, meta->entry(at), image.get(), book);
    books.push_back(std::move(book));
  }
  return ReadResult::Ok;
}

// ---- Home -------------------------------------------------------------------------------------

void ReadingStatsStore::prefetchRecent(const std::vector<std::string>& docIds) {
  // The snapshots outlive this call, so they go on the heap before the meta's transient 6 KB: a
  // long-lived block placed after a freed one splits the free space it leaves.
  std::vector<RecentSnapshot> snapshots(docIds.size());
  for (size_t i = 0; i < docIds.size(); ++i) snapshots[i].docId = docIds[i];
  float pace = 0.0f;
  ReadResult result = prepare();
  if (result == ReadResult::Ok) {
    auto meta = Meta::create();
    FsFile file;
    bool exists = false;
    result = meta ? open(file, *meta, exists) : ReadResult::NoMemory;
    if (result == ReadResult::Ok && exists) {
      for (RecentSnapshot& snapshot : snapshots) {
        DocKey key{};
        if (!ReadingStatsSlotFile::parseDocId(snapshot.docId, key)) continue;
        const size_t index = meta->find(key);
        if (index == kEntryCount) continue;
        const Entry entry = meta->entry(index);
        snapshot.known = true;
        snapshot.totalSeconds = entry.totalSeconds;
        snapshot.knownDays = entry.dayCount;
        snapshot.lastReadEpoch = static_cast<time_t>(entry.lastReadEpoch);
        snapshot.progress = entry.progress;
      }
      pace = pooledPaceOf(*meta);
    }
  }
  if (result != ReadResult::Ok) {
    LOG_ERR("RST", "prefetchRecent: history not read (%u); Home draws none this time", static_cast<unsigned>(result));
  }
  recent_ = std::move(snapshots);
  pooledPace_ = pace;
}

const ReadingStatsStore::RecentSnapshot* ReadingStatsStore::recent(const std::string& docId) const {
  for (const auto& snapshot : recent_) {
    if (snapshot.docId == docId) return &snapshot;
  }
  return nullptr;
}

// ---- The web ----------------------------------------------------------------------------------

ReadingStatsStore::ReadResult ReadingStatsStore::writeDashboard(Print& out, const uint16_t today) {
  return writeJson(out, today, /*dashboard=*/true);
}

ReadingStatsStore::ReadResult ReadingStatsStore::writeExport(Print& out) {
  return writeJson(out, 0, /*dashboard=*/false);
}

ReadingStatsStore::ReadResult ReadingStatsStore::writeJson(Print& out, const uint16_t today, const bool dashboard) {
  const ReadResult prepared = prepare();
  if (prepared != ReadResult::Ok) return prepared;
  auto meta = Meta::create();
  auto image = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!meta || !image) return ReadResult::NoMemory;
  FsFile file;
  bool exists = false;
  const ReadResult result = open(file, *meta, exists);
  if (result != ReadResult::Ok) return result;
  Summary summary;
  if (exists) summarize(*meta, summary, /*withIndex=*/false);
  if (dashboard) {
    ReadingStatsFile::writeDashboardHead(out, summary, summary.bookCount, summary.finishedBookCount, today);
  } else {
    ReadingStatsFile::writeFileHead(out, summary);
  }
  const float pooled = pooledSecondsPerPercent(summary.totalSeconds, summary.paceSeconds, summary.pacePercents);
  bool first = true;
  for (size_t i = 0; exists && i < kEntryCount; ++i) {
    const Entry entry = meta->entry(i);
    if (!entry.used()) continue;
    BookReadingStats book;
    readBook(file, entry, image.get(), book);
    if (!first) ReadingStatsFile::writeBookSeparator(out);
    first = false;
    if (dashboard) {
      const float own = ownSecondsPerPercent(book.totalSeconds, book.progress);
      const float remaining = book.progress < 100 ? 100.0f - static_cast<float>(book.progress) : 0.0f;
      ReadingStatsFile::writeBook(out, book, etaSeconds(own > 0.0f ? own : pooled, remaining));
    } else {
      ReadingStatsFile::writeBook(out, book);
    }
  }
  ReadingStatsFile::writeTail(out);
  return ReadResult::Ok;
}
```

**(d) The `BufferedPrint` include is gone.** Step (a) replaced the include block, so `src/ReadingStats.cpp` no longer includes it.

- [ ] **Step 6: Run the store tests to watch them pass**

Run: `cmake --build build/test --target ReadingStatsStoreTest 2>&1 | tail -1 && ./build/test/reading_stats/ReadingStatsStoreTest.exe 2>&1 | tail -3`
Expected: `[  PASSED  ] 30 tests.`

- [ ] **Step 7: Switch the consumers**

`src/activities/settings/ReadingStatsActivity.h`: change `ReadingStatsFile::Summary summary_;` to `ReadingStatsStore::Summary summary_;`.

`src/activities/settings/ReadingStatsActivity.cpp`, `loadSummary()`: replace the body up to `RenderLock lock(*this);` with:

```cpp
  ReadingStatsStore::Summary summary;
  std::vector<BookReadingStats> top;
  const bool ok = READING_STATS.querySummary(summary, /*withIndex=*/true) == ReadingStatsStore::ReadResult::Ok;
  if (ok) {
    const size_t shown = std::min<size_t>(summary.byTime.size(), 3);
    if (READING_STATS.queryBooksAt(summary.byTime, 0, shown, summary.seq, top) != ReadingStatsStore::ReadResult::Ok) {
      top.clear();
    }
    for (BookReadingStats& book : top) book.days.clear();  // the card shows title and time only
    std::vector<ReadingStatsStore::IndexEntry>().swap(summary.byTime);
  }
```

`src/activities/settings/ReadingStatsBookListActivity.h`:
- change `std::vector<ReadingStatsFile::IndexEntry> index_;` to `std::vector<ReadingStatsStore::IndexEntry> index_;`;
- directly below that line, add:

```cpp
  uint32_t indexSeq_ = 0;  // the history's generation index_ was taken at
```

- next to `void ensureRowsFor(int index);`, add:

```cpp
  // Rows [first, first + page) of the index; false when the history changed since it was taken.
  bool readRows(int first, int page, std::vector<BookReadingStats>& rows) const;
```

`src/activities/settings/ReadingStatsBookListActivity.cpp`:
- In `rebuildIndex()`:
  - change `ReadingStatsFile::Summary summary;` to `ReadingStatsStore::Summary summary;`;
  - change `!= ReadingStatsFile::ScanResult::Ok` to `!= ReadingStatsStore::ReadResult::Ok`;
  - after `index_ = std::move(summary.byTime);`, add `indexSeq_ = summary.seq;`.
- Replace `ensureRowsFor` whole with:

```cpp
bool ReadingStatsBookListActivity::readRows(const int first, const int page,
                                            std::vector<BookReadingStats>& rows) const {
  const int last = std::min(first + page, static_cast<int>(index_.size()));
  const auto count = static_cast<size_t>(std::max(0, last - first));
  const auto result = READING_STATS.queryBooksAt(index_, static_cast<size_t>(first), count, indexSeq_, rows);
  if (result == ReadingStatsStore::ReadResult::Stale) return false;
  if (result != ReadingStatsStore::ReadResult::Ok) rows.assign(count, BookReadingStats{});
  for (BookReadingStats& row : rows) row.days.clear();  // a row shows title, author, time and the finished mark
  return true;
}

void ReadingStatsBookListActivity::ensureRowsFor(const int index) {
  if (index_.empty()) return;
  const int page = pageItems();
  int first = index / page * page;
  if (first == rowsFirst_ && !rows_.empty()) return;
  std::vector<BookReadingStats> rows;
  if (!readRows(first, page, rows)) {
    // The history changed under the list. Take the order again, once.
    rebuildIndex();
    first = selectedIndex / page * page;
    if (!readRows(first, page, rows)) rows.clear();
  }
  RenderLock lock(*this);
  rows_ = std::move(rows);
  rowsFirst_ = first;
}
```

`src/activities/settings/ReadingStatsBookDetailActivity.cpp`: change `!= ReadingStatsFile::ScanResult::Ok` to `!= ReadingStatsStore::ReadResult::Ok`.

`src/activities/home/BookInfoActivity.cpp`: change `== ReadingStatsFile::ScanResult::Ok` to `== ReadingStatsStore::ReadResult::Ok`.

`src/components/BookProgressPresentation.cpp`: change `const ReadingStatsFile::RecentSnapshot* historyOf(` to `const ReadingStatsStore::RecentSnapshot* historyOf(`.

`src/ReadingSessionTracker.cpp`: replace the comment `// A streamed rewrite of the history file: a few KB of heap whatever its size.` with `// One book and the global figures, written in place copy-on-write: ~10 KB of heap whatever the history holds.`

- [ ] **Step 8: Switch the web handlers**

In `src/network/CrossPointWebServer.cpp`, delete `#include "ReadingStatsFile.h"` and replace `handleStatsApi` and `handleStatsExport` whole with:

```cpp
void CrossPointWebServer::handleStatsApi() const {
  if (rejectIfLowMemory(server.get())) return;
  LOG_WEB_MEM("stats_api_enter");
  // The figures the on-device screens show, with streaks and todayDayIndex pre-computed so the
  // browser doesn't have to recreate the day-index math; the day arrays go across untouched so it
  // can render the sparkline.
  //
  // Generated from the history file one book at a time, never loaded whole: the meta and one slot,
  // whatever the history holds.
  ReadingStatsStore::Summary summary;
  if (READING_STATS.querySummary(summary) != ReadingStatsStore::ReadResult::Ok) {
    server->send(500, "application/json", "{\"error\":\"Reading stats could not be read\"}");
    return;
  }
  if (summary.bookCount == 0 && summary.totalSeconds == 0) {
    server->send(200, "application/json", "{\"totalSeconds\":0,\"books\":[]}");  // no history yet
    return;
  }
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  ChunkedResponse response(server.get());
  ChunkedPrint out(response);
  if (READING_STATS.writeDashboard(out, currentLocalDayIndex()) != ReadingStatsStore::ReadResult::Ok) {
    LOG_ERR("WEB", "Reading stats failed mid-response; dashboard truncated");
  }
  response.finish();
  LOG_WEB_MEM("stats_api_exit");
}
```

```cpp
void CrossPointWebServer::handleStatsExport() const {
  if (rejectIfLowMemory(server.get())) return;
  // The reading-stats.json format older firmware reads, generated from the history file: the
  // backup, and the way back after a downgrade.
  ReadingStatsStore::Summary summary;
  if (READING_STATS.querySummary(summary) != ReadingStatsStore::ReadResult::Ok) {
    server->send(500, "application/json", "{}");
    return;
  }
  if (summary.bookCount == 0 && summary.totalSeconds == 0) {
    server->send(404, "application/json", "{}");
    return;
  }
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Content-Disposition", "attachment; filename=\"reading-stats.json\"");
  server->send(200, "application/json", "");
  ChunkedResponse response(server.get());
  ChunkedPrint out(response);
  if (READING_STATS.writeExport(out) != ReadingStatsStore::ReadResult::Ok) {
    LOG_ERR("WEB", "Reading stats failed mid-export; file truncated");
  }
  response.finish();
}
```

In `src/network/ChunkedResponse.h`, change the comment `(ReadingStatsFile's dashboard)` to `(the reading-stats dashboard and export)`.

- [ ] **Step 9: Build and run everything**

Run: `grep -rn "ReadingStatsFile::\(ScanResult\|Summary\|IndexEntry\|RecentSnapshot\)" src | grep -v "^src/ReadingStatsFile" ; echo "grep done"`
Expected: only `grep done`. No consumer names the JSON layer's scan types any more.

Run the full host suite (command above).
Expected: every target builds except the known `epub_build_inventory`, and ctest reports `100% tests passed` apart from that target's `NOT_BUILT` entries.

Run the firmware build (PowerShell).
Expected: `SUCCESS`. Record the flash size (`Flash: [====…]  xx.x% (used N bytes …)`) in the ledger.

- [ ] **Step 10: Format and commit**

```bash
"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStats.h src/ReadingStats.cpp src/activities/settings/ReadingStatsActivity.h src/activities/settings/ReadingStatsActivity.cpp src/activities/settings/ReadingStatsBookListActivity.h src/activities/settings/ReadingStatsBookListActivity.cpp src/activities/settings/ReadingStatsBookDetailActivity.cpp src/activities/home/BookInfoActivity.cpp src/components/BookProgressPresentation.cpp src/ReadingSessionTracker.cpp src/network/CrossPointWebServer.cpp src/network/ChunkedResponse.h test/reading_stats/ReadingStatsStoreTest.cpp
git add src/ReadingStats.h src/ReadingStats.cpp src/activities/settings/ReadingStatsActivity.h src/activities/settings/ReadingStatsActivity.cpp src/activities/settings/ReadingStatsBookListActivity.h src/activities/settings/ReadingStatsBookListActivity.cpp src/activities/settings/ReadingStatsBookDetailActivity.cpp src/activities/home/BookInfoActivity.cpp src/components/BookProgressPresentation.cpp src/ReadingSessionTracker.cpp src/network/CrossPointWebServer.cpp src/network/ChunkedResponse.h test/reading_stats/ReadingStatsStoreTest.cpp test/reading_stats/CMakeLists.txt
git commit -m "feat(stats): keep the history in reading-stats.bin, copy-on-write

An update writes the book into a free slot, flushes, then writes the
meta into the copy that is not the live one: ~7 KB instead of the
whole history. A power cut loses at most that update. Queries read
the meta and single slots; the web generates today's JSON from them.
The legacy reading-stats.json is left alone until the import lands."
```

---

### Task 4: Import the legacy JSON once

**Files:**
- Modify: `src/ReadingStats.h` (two private declarations), `src/ReadingStats.cpp` (`prepare()`, the import, one helper)
- Test: `test/reading_stats/ReadingStatsStoreTest.cpp`

**Interfaces:**
- Consumes:
  - from `ReadingStatsFile`: `scan(HalFile&, Summary&, const ScanRequest&)` with `wantIndex`, `readBookAt(HalFile&, size_t, BookReadingStats&)`, `IndexEntry{totalSeconds, offset}`, `Summary` (a `ReadingTotals`), `ScanResult`;
  - from Task 1: `createZeroed`, `writeSlot`, `writeMeta`, `loadMeta`, `encodeSlot`, `entryFor`, `parseDocId`, `Meta`;
  - from Task 3: `prepare()`, `setAside()`, `parentDirOf()`, `asidePathFor()`.
- Produces: private `ReadResult importLegacy()` and `ReadResult writeImport(FsFile& in, ReadingStatsFile::Summary& legacy, const std::string& tmpPath, size_t& imported)`. `prepare()` now imports instead of refusing.

- [ ] **Step 1: Write the failing tests**

In `test/reading_stats/ReadingStatsStoreTest.cpp`, delete the test `LegacyHistoryIsLeftAloneUntilImported`. Inside the anonymous namespace, after `seqOfCopy`, add:

```cpp
// A history as older firmware wrote it: byte for byte what the device's JSON writer produces, so an
// import followed by an export gives it back.
std::string legacyBookA() {
  return R"({"docId":")" + id(1) +
         R"(","title":"Book A","author":"X","totalSeconds":1000,"pagesTurned":17,"sessions":2,)"
         R"("firstReadEpoch":0,"lastReadEpoch":0,"progress":25,"finishedCount":0,"lastFinishedEpoch":0,)"
         R"("finished":false,"days":[[20463,600],[20464,400]]})";
}

std::string legacyBookB() {
  return R"({"docId":")" + id(2) +
         R"(","title":"B","author":"","totalSeconds":300,"pagesTurned":5,"sessions":1,"firstReadEpoch":0,)"
         R"("lastReadEpoch":0,"progress":40,"finishedCount":1,"lastFinishedEpoch":0,"finished":true,)"
         R"("days":[[20463,300]]})";
}

std::string legacyFile() {
  return R"({"totalSeconds":1300,"totalSessions":3,"totalPagesTurned":22,"longestStreak":2,)"
         R"("globalDays":[[20463,900],[20464,400]],"books":[)" +
         legacyBookA() + "," + legacyBookB() + "]}";
}
```

Append these tests to the file:

```cpp
TEST_F(StoreTest, ImportsTheLegacyHistoryOnFirstUse) {
  writeBytes(legacy_, legacyFile());
  ReadingStatsStore store(path_, legacy_);

  const auto summary = summaryOf(store);

  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 1300u);
  EXPECT_EQ(summary.totalSessions, 3u);
  EXPECT_EQ(summary.longestStreak, 2);
  EXPECT_EQ(summary.globalDays.size(), 2u);
  EXPECT_EQ(summary.finishedBookCount, 1u);
  const auto a = bookOf(store, id(1));
  ASSERT_TRUE(a.found);
  EXPECT_EQ(a.book.title, "Book A");
  EXPECT_EQ(a.book.days.size(), 2u);
  EXPECT_TRUE(std::filesystem::exists(path_));
  EXPECT_FALSE(std::filesystem::exists(legacy_));
  EXPECT_EQ(readBytes(legacy_ + ".imported"), legacyFile());
}

TEST_F(StoreTest, ImportThenExportGivesTheFileBack) {
  writeBytes(legacy_, legacyFile());
  ReadingStatsStore store(path_, legacy_);
  StringPrint out;

  ASSERT_EQ(store.writeExport(out), ReadResult::Ok);

  EXPECT_EQ(out.text, legacyFile());
}

TEST_F(StoreTest, FirstSessionAfterTheUpdateImportsFirst) {
  writeBytes(legacy_, legacyFile());
  ReadingStatsStore store(path_, legacy_);

  ASSERT_EQ(store.recordSession(id(2), "B", "", 150, 3, 50, kNoon), WriteResult::Done);

  const auto summary = summaryOf(store);
  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 1450u);
  EXPECT_EQ(bookOf(store, id(2)).book.totalSeconds, 450u);
  EXPECT_EQ(bookOf(store, id(1)).book.totalSeconds, 1000u);
}

TEST_F(StoreTest, ImportSkipsBadAndDuplicateDocIds) {
  const std::string bad = R"({"docId":"a","title":"Bad","totalSeconds":5,"days":[]})";
  const std::string again = R"({"docId":")" + id(1) + R"(","title":"Again","totalSeconds":999,"days":[]})";
  writeBytes(legacy_, R"({"totalSeconds":2004,"totalSessions":4,"books":[)" + bad + "," + legacyBookA() + "," +
                          again + "]}");
  ReadingStatsStore store(path_, legacy_);

  const auto summary = summaryOf(store);

  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 2004u);  // the header's figures stand, as with an eviction
  const auto a = bookOf(store, id(1));
  EXPECT_EQ(a.book.title, "Book A");
  EXPECT_EQ(a.book.totalSeconds, 1000u);
}

TEST_F(StoreTest, ImportKeepsTheBooksTheCapWouldKeep) {
  std::string books;
  for (unsigned i = 0; i <= ReadingStatsStore::kMaxBooks; ++i) {
    if (i > 0) books += ",";
    const long long lastRead = i == 7 ? 5 : 1000 + static_cast<long long>(i);
    books += R"({"docId":")" + id(i) + R"(","totalSeconds":60,"lastReadEpoch":)" + std::to_string(lastRead) +
             R"(,"days":[]})";
  }
  writeBytes(legacy_, R"({"totalSeconds":6060,"books":[)" + books + "]}");
  ReadingStatsStore store(path_, legacy_);

  const auto summary = summaryOf(store);

  EXPECT_EQ(summary.bookCount, ReadingStatsStore::kMaxBooks);
  EXPECT_FALSE(bookOf(store, id(7)).found);
  EXPECT_TRUE(bookOf(store, id(ReadingStatsStore::kMaxBooks)).found);
}

TEST_F(StoreTest, MalformedLegacyIsSetAsideAndAFreshHistoryStarts) {
  const std::string broken = R"({"totalSeconds":12,"books":[{"docId":)";
  writeBytes(legacy_, broken);
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(summaryOf(store).bookCount, 0u);
  EXPECT_EQ(readBytes((dir_ / "reading-stats.corrupt.json").generic_string()), broken);

  ASSERT_EQ(store.recordSession(id(1), "A", "", 60, 1, 1, kNoon), WriteResult::Done);
  EXPECT_EQ(summaryOf(store).bookCount, 1u);
}

TEST_F(StoreTest, InterruptedImportIsRedone) {
  writeBytes(legacy_, legacyFile());
  writeBytes(path_ + ".tmp", "half an import");
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(summaryOf(store).bookCount, 2u);

  EXPECT_FALSE(std::filesystem::exists(path_ + ".tmp"));
}

TEST_F(StoreTest, ZeroByteLegacyIsAnEmptyHistory) {
  writeBytes(legacy_, "");
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(summaryOf(store).bookCount, 0u);

  ASSERT_EQ(store.recordSession(id(1), "A", "", 60, 1, 1, kNoon), WriteResult::Done);
  EXPECT_EQ(summaryOf(store).bookCount, 1u);
}

TEST_F(StoreTest, LegacyBesideAHistoryFileIsIgnored) {
  {
    ReadingStatsStore first(path_, legacy_);
    ASSERT_EQ(first.recordSession(id(5), "E", "", 60, 1, 1, kNoon), WriteResult::Done);
  }
  writeBytes(legacy_, legacyFile());
  ReadingStatsStore store(path_, legacy_);

  const auto summary = summaryOf(store);

  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_TRUE(bookOf(store, id(5)).found);
  EXPECT_EQ(readBytes(legacy_), legacyFile());
}

TEST_F(StoreTest, LongLegacyTitleIsCut) {
  const std::string book =
      R"({"docId":")" + id(1) + R"(","title":")" + std::string(400, 'x') + R"(","totalSeconds":60,"days":[]})";
  writeBytes(legacy_, R"({"totalSeconds":60,"books":[)" + book + "]}");
  ReadingStatsStore store(path_, legacy_);

  EXPECT_EQ(bookOf(store, id(1)).book.title, std::string(ReadingStatsSlotFile::kTitleMax, 'x'));
}
```

- [ ] **Step 2: Run the tests to watch them fail**

Run: `cmake --build build/test --target ReadingStatsStoreTest 2>&1 | tail -1 && ./build/test/reading_stats/ReadingStatsStoreTest.exe 2>&1 | grep -E "FAILED|PASSED" | head -12`
Expected: 9 of the 10 new tests FAIL. `prepare()` still refuses to run beside a legacy file, so they see `IoError` or `Failed`. `LegacyBesideAHistoryFileIsIgnored` passes already, because the `.bin` wins.

- [ ] **Step 3: Declare the import**

In `src/ReadingStats.h`, in the private section after `createFresh`, add:

```cpp
  // The reading-stats.json of older firmware, into a new history file: once, on the first call
  // that finds no history file beside it. The JSON is only read, then renamed *.imported.
  ReadResult importLegacy();
  // Writes the legacy books into a new file at `tmpPath`, one slot each in file order, then the
  // meta into copy A, and reads the meta back.
  ReadResult writeImport(FsFile& in, ReadingStatsFile::Summary& legacy, const std::string& tmpPath,
                         size_t& imported);
```

- [ ] **Step 4: Implement it**

In `src/ReadingStats.cpp`, in `prepare()`, replace the block

```cpp
  if (!Storage.exists(path_.c_str()) && Storage.exists(legacyPath_.c_str())) {
    // Not imported yet (the import comes with the next task): nothing may start a history beside
    // the old one, or the import would never run.
    LOG_ERR("RST", "%s is not imported yet; the history is left as is", legacyPath_.c_str());
    return ReadResult::IoError;
  }
```

with:

```cpp
  if (!Storage.exists(path_.c_str()) && Storage.exists(legacyPath_.c_str())) {
    const ReadResult imported = importLegacy();
    if (imported != ReadResult::Ok) return imported;
  }
```

In the second anonymous namespace (the one that holds `victimOf`), add:

```cpp
// More books than the cap (only a hand-made file has them): keep the ones the cap would keep.
ReadingStatsStore::ReadResult keepTheCapsBooks(FsFile& in, std::vector<ReadingStatsFile::IndexEntry>& entries) {
  struct Key {
    time_t lastRead;
    uint32_t seconds;
    uint32_t offset;
  };
  std::vector<Key> keys;
  keys.reserve(entries.size());
  for (const auto& at : entries) {
    BookReadingStats book;
    if (ReadingStatsFile::readBookAt(in, at.offset, book) != ReadingStatsFile::ScanResult::Ok) {
      return ReadingStatsStore::ReadResult::IoError;
    }
    keys.push_back({book.lastReadEpoch, book.totalSeconds, at.offset});
  }
  std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
    return ReadingStatsStore::evictsBefore(a.lastRead, a.seconds, b.lastRead, b.seconds);
  });
  std::vector<uint32_t> gone;
  for (size_t i = 0; i + ReadingStatsStore::kMaxBooks < keys.size(); ++i) gone.push_back(keys[i].offset);
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&gone](const ReadingStatsFile::IndexEntry& e) {
                                 return std::find(gone.begin(), gone.end(), e.offset) != gone.end();
                               }),
                entries.end());
  return ReadingStatsStore::ReadResult::Ok;
}
```

After `createFresh`, add:

```cpp
ReadingStatsStore::ReadResult ReadingStatsStore::importLegacy() {
  [[maybe_unused]] const uint32_t started = millis();
  const std::string tmpPath = path_ + ".tmp";
  bool malformed = false;
  size_t imported = 0;
  {
    FsFile in;
    if (!Storage.openFileForRead("RST", legacyPath_.c_str(), in)) return ReadResult::IoError;
    ReadingStatsFile::Summary legacy;
    if (in.size() > 0) {  // a zero-byte file is an empty history, as it always was
      ReadingStatsFile::ScanRequest request;
      request.wantIndex = true;
      const auto scanned = ReadingStatsFile::scan(in, legacy, request);
      if (scanned == ReadingStatsFile::ScanResult::NoMemory) return ReadResult::NoMemory;
      if (scanned == ReadingStatsFile::ScanResult::IoError) return ReadResult::IoError;
      malformed = scanned == ReadingStatsFile::ScanResult::Malformed;
    }
    if (!malformed) {
      const ReadResult written = writeImport(in, legacy, tmpPath, imported);
      if (written != ReadResult::Ok) {
        Storage.remove(tmpPath.c_str());
        return written;
      }
    }
  }
  if (malformed) {
    // As the JSON loader always did: set it aside, and the history starts afresh.
    return setAside(legacyPath_) ? ReadResult::Ok : ReadResult::IoError;
  }
  if (!Storage.rename(tmpPath.c_str(), path_.c_str())) {
    Storage.remove(tmpPath.c_str());
    return ReadResult::IoError;
  }
  const std::string done = legacyPath_ + ".imported";
  Storage.remove(done.c_str());
  if (!Storage.rename(legacyPath_.c_str(), done.c_str())) {
    LOG_ERR("RST", "Imported, but %s could not be renamed; it is ignored from now on", legacyPath_.c_str());
  }
  LOG_INF("RST", "Imported %u books from %s in %lu ms", static_cast<unsigned>(imported), legacyPath_.c_str(),
          static_cast<unsigned long>(millis() - started));
  return ReadResult::Ok;
}

ReadingStatsStore::ReadResult ReadingStatsStore::writeImport(FsFile& in, ReadingStatsFile::Summary& legacy,
                                                             const std::string& tmpPath, size_t& imported) {
  imported = 0;
  // File order, not time order: an import followed by an export gives the file back.
  std::stable_sort(legacy.byTime.begin(), legacy.byTime.end(),
                   [](const ReadingStatsFile::IndexEntry& a, const ReadingStatsFile::IndexEntry& b) {
                     return a.offset < b.offset;
                   });
  if (legacy.byTime.size() > kMaxBooks) {
    const ReadResult kept = keepTheCapsBooks(in, legacy.byTime);
    if (kept != ReadResult::Ok) return kept;
  }
  auto meta = Meta::create();
  auto image = makeUniqueNoThrow<uint8_t[]>(kSlotSize);
  if (!meta || !image) return ReadResult::NoMemory;
  if (!meta->writeTotals(legacy)) return ReadResult::IoError;  // the scan trimmed the days to the cap
  Storage.mkdir(parentDirOf(path_).c_str());
  if (!ReadingStatsSlotFile::createZeroed(tmpPath.c_str())) return ReadResult::IoError;
  FsFile out;
  if (!Storage.openFileForUpdate("RST", tmpPath.c_str(), out)) return ReadResult::IoError;
  for (const auto& at : legacy.byTime) {
    if (imported == kEntryCount) break;
    BookReadingStats book;
    const auto read = ReadingStatsFile::readBookAt(in, at.offset, book);
    if (read == ReadingStatsFile::ScanResult::NoMemory) return ReadResult::NoMemory;
    if (read != ReadingStatsFile::ScanResult::Ok) return ReadResult::IoError;
    DocKey key{};
    if (!ReadingStatsSlotFile::parseDocId(book.docId, key)) {
      LOG_ERR("RST", "Import: '%s' is not a document id; skipped", book.docId.c_str());
      continue;
    }
    if (meta->find(key) != kEntryCount) {
      LOG_ERR("RST", "Import: %s appears twice; the first kept", book.docId.c_str());
      continue;
    }
    const auto slot = static_cast<uint8_t>(imported);
    ReadingStatsSlotFile::encodeSlot(key, book, image.get());
    if (!ReadingStatsSlotFile::writeSlot(out, slot, image.get())) return ReadResult::IoError;
    meta->setEntry(imported, ReadingStatsSlotFile::entryFor(key, book, slot));
    ++imported;
  }
  meta->setSeq(1);
  meta->seal();
  if (!ReadingStatsSlotFile::writeMeta(out, *meta, 0)) return ReadResult::IoError;
  out.flush();
  uint8_t live = kNoCopy;
  if (ReadingStatsSlotFile::loadMeta(out, *meta, live) != Load::Ok || meta->bookCount() != imported) {
    LOG_ERR("RST", "Import did not read back; %s left as is", legacyPath_.c_str());
    return ReadResult::IoError;
  }
  return ReadResult::Ok;
}
```

- [ ] **Step 5: Run the tests to watch them pass**

Run: `cmake --build build/test --target ReadingStatsStoreTest 2>&1 | tail -1 && ./build/test/reading_stats/ReadingStatsStoreTest.exe 2>&1 | tail -3`
Expected: `[  PASSED  ] 39 tests.` That is the 30 from Task 3, minus the deleted guard test, plus the 10 import tests.

- [ ] **Step 6: Full suite, firmware, commit**

Run the full host suite and the firmware build.
Expected: as in Task 3 Step 9 (`100% tests passed` apart from `epub_build_inventory`; firmware `SUCCESS`).

```bash
"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStats.h src/ReadingStats.cpp test/reading_stats/ReadingStatsStoreTest.cpp
git add src/ReadingStats.h src/ReadingStats.cpp test/reading_stats/ReadingStatsStoreTest.cpp
git commit -m "feat(stats): import the legacy reading-stats.json once

The first call that finds no reading-stats.bin but a JSON history
imports it: each book into the next slot in file order, the meta into
copy A, read back, renamed into place. The JSON is kept as
reading-stats.json.imported. Bad and duplicate ids are skipped; a
hand-made file past the cap keeps what the cap would keep."
```

---

### Task 5: Remove the rewrite path; `ReadingStatsFile` becomes `ReadingStatsJson`

Nothing calls the streamed rewrite, the byte-copy dashboard or the scan's target, victim and recents after Task 3. This task deletes them, then renames the JSON layer to what it now is.

**Files:**
- Modify then rename: `src/ReadingStatsFile.h` → `src/ReadingStatsJson.h`, `src/ReadingStatsFile.cpp` → `src/ReadingStatsJson.cpp`
- Modify then rename: `test/reading_stats/ReadingStatsFileTest.cpp` → `test/reading_stats/ReadingStatsJsonTest.cpp`
- Modify: `src/ReadingStats.h`, `src/ReadingStats.cpp`, `test/reading_stats/CMakeLists.txt`, `test/reading_stats/ReadingStatsTest.cpp` (comment), `test/reading_stats/ReadingStatsStoreTest.cpp`, `src/ReadingStatsTypes.h` (comment), `src/activities/reader/EpubReaderActivity.cpp` (comment)

**Interfaces:**
- Consumes: nothing new.
- Produces (namespace `ReadingStatsJson`, which replaces `ReadingStatsFile`):
  - `kPath`, `ScanResult`, `IndexEntry{totalSeconds, offset}`;
  - `ScanRequest{wantIndex}`;
  - `Summary : ReadingTotals{bookCount, finishedBookCount, paceSeconds, pacePercents, byTime}`;
  - `scan`, `readBookAt`;
  - `writeBook(out, book, eta = -1)`, `writeFileHead`, `writeDashboardHead`, `writeBookSeparator`, `writeTail`.
  - Gone: `RecentSnapshot`, `summarize`, `writeDashboard`, `writeWithoutTarget`, `Rewrite`, `writeRewrite`, `kNoEntry`, and the `ScanRequest` fields `findDocId`, `wantVictim`, `recentDocIds`, as well as the `Summary` fields `found`, `target`, `targetFirst`, `hasVictim`, `victimDocId`, `victimFirst` and `recents`.

- [ ] **Step 1: Delete the dead code in `src/ReadingStatsFile.cpp`**

1. Delete the classes `DashboardCopy` and `RewriteCopy` whole, with their comments.
2. Delete the functions `summarize`, `writeDashboard`, `writeWithoutTarget` and `writeRewrite` whole.
3. In `class Scanner`, delete:
   - the line `        onByte(bytes[i], offset, insideBooks(offset));` in `run()`;
   - the declarations `virtual void onBookStart(size_t) {}` and `virtual void onByte(char, size_t, bool) {}`, with the two comment lines above `onBookEnd`/`onByte` that describe `onByte` and `inBooks`;
   - the function `insideBooks(...)`;
   - the call `      onBookStart(offset_);`;
   - the two lines that set `booksOpen_` and `booksClose_`;
   - the members `size_t booksOpen_ = kNone;` and `size_t booksClose_ = kNone;`.

   Where the class comment above `Scanner` mentions `onByte()`, cut that clause. If `kNone` is then unused, delete it too.
4. Replace `class SummaryScan` whole with:

```cpp
class SummaryScan final : public Scanner {
 public:
  SummaryScan(Summary& summary, const ScanRequest& request) : summary_(summary), request_(request) {}

  // After the pass: the index in time order.
  void settle() {
    std::stable_sort(summary_.byTime.begin(), summary_.byTime.end(),
                     [](const IndexEntry& a, const IndexEntry& b) { return a.totalSeconds > b.totalSeconds; });
  }

 private:
  void onCounter(const Top top, const uint32_t value) override {
    switch (top) {
      case Top::TotalSeconds:
        summary_.totalSeconds = value;
        break;
      case Top::TotalSessions:
        summary_.totalSessions = value;
        break;
      case Top::TotalPagesTurned:
        summary_.totalPagesTurned = value;
        break;
      case Top::LongestStreak:
        summary_.longestStreak = static_cast<uint16_t>(std::min<uint32_t>(value, 0xFFFF));
        break;
      default:
        break;
    }
  }

  void onGlobalDay(const uint16_t day, const uint32_t seconds) override {
    summary_.globalDays.push_back({day, seconds});
  }

  void onBookEnd(const Book& book, size_t) override {
    // The loader skips an entry without a docId; so does everything counted here.
    if (book.docId.empty()) return;
    ++summary_.bookCount;
    if (book.finished()) ++summary_.finishedBookCount;
    if (ReadingStatsStore::countsTowardPace(book.progress)) {
      summary_.paceSeconds += book.totalSeconds;
      summary_.pacePercents += book.progress;
    }
    if (request_.wantIndex) summary_.byTime.push_back({book.totalSeconds, static_cast<uint32_t>(book.first)});
  }

  Summary& summary_;
  const ScanRequest& request_;
};
```

- [ ] **Step 2: Rewrite the header's types and comment**

In `src/ReadingStatsFile.h`:
- Replace the namespace comment (the paragraph starting `// The reading-stats file, streamed rather than loaded.` and the one after it) with:

```cpp
// The reading history as JSON: the reading-stats.json of older firmware, read once to import it
// (ReadingStatsStore::prepare()), and the same format written for the web -- the /api/stats payload
// and the export, which older firmware reads back. Reading is streamed: one parser, a small read
// buffer and the global day buckets, however many books the file has.
```

- Delete `struct RecentSnapshot` with its comment.
- Replace `struct ScanRequest` with:

```cpp
// What a pass collects beyond the global figures.
struct ScanRequest {
  bool wantIndex = false;  // Summary::byTime
};
```

- In `struct Summary`, delete the members `found`, `target`, `targetFirst`, `hasVictim`, `victimDocId`, `victimFirst` and `recents`, with their comments.
- Delete the declarations of `summarize`, `writeDashboard`, `writeWithoutTarget`, `kNoEntry`, `struct Rewrite` and `writeRewrite`, with their comments.

- [ ] **Step 3: Delete the tests of what is gone, convert the rest**

In `test/reading_stats/ReadingStatsFileTest.cpp`:

1. Delete these tests whole:
   - `ReadingStatsFileSummary.FindsTheBookAskedFor`, `UnknownBookIsNotFound` and `RejectsATruncatedFile`;
   - all six `ReadingStatsFileRemove.*`, all three `ReadingStatsFileDashboard.*` and all four `ReadingStatsFileRewrite.*`;
   - `ReadingStatsFileScan.VictimIsTheLeastRecentlyReadThenTheLeastRead` and `RecentsReportKnownAndUnknownBooks`.

   Also delete the now-unused helpers `removeFrom`, `dashboardOf`, `rewriteOf`, `kBookBAfter` and `bookBAfter`. Keep `open`, `bookD`, `kBookD`, `totalsOf`, `scanOf`, `pairsOf` and `StringPrint`.
2. In `AddsUpTheFile`, replace the lines

```cpp
  HalFile in = HalFile::fromString(kFile);
  ReadingStatsFile::Summary summary;

  ASSERT_TRUE(ReadingStatsFile::summarize(in, summary));
```

with `  const auto summary = scanOf(kFile, {});`, and delete its last line `  EXPECT_FALSE(summary.found);`.
3. In `RejectsSomethingThatIsNotJson`, replace the body with `  scanOf("not a stats file", {}, ScanResult::Malformed);`.
4. In `DecodesTheWholeTarget`:
   - Replace the four lines from `  ReadingStatsFile::ScanRequest request;` through `  const BookReadingStats& b = summary.target;` (that is: the request, `request.findDocId = "d";`, the blank line, the `scanOf` call, the blank line, `ASSERT_TRUE(summary.found);` and the `b` binding) with:

```cpp
  HalFile in = HalFile::fromString(head + dated + "]}");
  BookReadingStats b;

  ASSERT_EQ(ReadingStatsFile::readBookAt(in, head.size(), b), ScanResult::Ok);
```

   - Delete its last line, `  EXPECT_EQ(summary.targetFirst, head.size());`.
   - Leave the fixture lines and the title assertion untouched: they hold escaped text that must not be retyped.
5. Replace `LegacyFinishedFlagCountsAsOneFinish`'s body with:

```cpp
  const std::string legacy = R"({"docId":"l","totalSeconds":60,"progress":100,"finished":true,"days":[]})";
  const std::string head = R"({"totalSeconds":60,"books":[)";
  const std::string file = head + legacy + "]}";
  HalFile in = HalFile::fromString(file);
  BookReadingStats book;

  ASSERT_EQ(ReadingStatsFile::readBookAt(in, head.size(), book), ScanResult::Ok);

  EXPECT_EQ(book.finishedCount, 1);
  EXPECT_EQ(scanOf(file, {}).finishedBookCount, 1u);
```

6. Replace the file's opening comment (the first three lines) with:

```cpp
// ReadingStatsJson: the reading history as JSON -- the legacy file scanned for the import, one entry
// decoded at an offset, and the writers the web payloads are built from, pinned to hand-derived text.
```

- [ ] **Step 4: Build and run with the old name, to see the deletion green on its own**

Run: `cmake --build build/test --target ReadingStatsTest ReadingStatsStoreTest 2>&1 | grep -E "error|warning: unused" | head; ./build/test/reading_stats/ReadingStatsTest.exe 2>&1 | tail -1; ./build/test/reading_stats/ReadingStatsStoreTest.exe 2>&1 | tail -1`
Expected: no errors and no unused warnings. The store test reports `[  PASSED  ] 39 tests.` and `ReadingStatsTest` reports `[  PASSED  ] 21 tests.` That is 39 minus the 18 deleted tests: 3 Summary, 6 Remove, 3 Dashboard, 4 Rewrite and 2 Scan.

- [ ] **Step 5: Rename**

```bash
git mv src/ReadingStatsFile.h src/ReadingStatsJson.h
git mv src/ReadingStatsFile.cpp src/ReadingStatsJson.cpp
git mv test/reading_stats/ReadingStatsFileTest.cpp test/reading_stats/ReadingStatsJsonTest.cpp
sed -i 's/ReadingStatsFile/ReadingStatsJson/g' src/ReadingStatsJson.h src/ReadingStatsJson.cpp src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsTypes.h test/reading_stats/ReadingStatsJsonTest.cpp test/reading_stats/ReadingStatsStoreTest.cpp test/reading_stats/ReadingStatsTest.cpp test/reading_stats/CMakeLists.txt
grep -rn "ReadingStatsFile\b\|ReadingStatsFile::\|ReadingStatsFile\.h\|ReadingStatsFileTest" src test lib | grep -v "ReadingStatsSlotFile"; echo "grep done"
```

Expected: only `grep done`. (`ReadingStatsSlotFile` does not contain the substring `ReadingStatsFile`, so the `sed` leaves it alone.)

- [ ] **Step 6: Stale comments**

- In `src/ReadingStatsTypes.h`, the comment now reads `(ReadingStatsJson.h)`. Change the whole comment to: `// The reading history's plain data, shared by the store (ReadingStats.h), the slot file (ReadingStatsSlotFile.h) and the JSON codec (ReadingStatsJson.h) without any of them including another's.`
- In `src/activities/reader/EpubReaderActivity.cpp`, replace the comment above `globalReadingSessionTracker().end();` at the end of `onExit()` with:

```cpp
  // Flush the reading-stats session LAST: end() writes the book and the global figures in place
  // (~10 KB of heap whatever the history holds), and it needs nothing of the reader (the tracker
  // copied the id, title and author at begin()). Here it has the heap the teardown just freed.
  // Sleep paths that bypass onExit() still end up here on resume because the activity is
  // recreated.
```

- In `test/reading_stats/CMakeLists.txt`, delete the two `# BufferedPrint.h` / `${REPO_ROOT}/lib/Serialization` include lines from the `ReadingStatsTest` target, and the `${REPO_ROOT}/lib/Serialization` line from the `ReadingStatsStoreTest` target. The stats code no longer uses BufferedPrint.

- [ ] **Step 7: Full suite and firmware**

Run: `cmake -S test -B build/test > /dev/null` and then the full host suite.
Expected: `100% tests passed` apart from `epub_build_inventory`.

Run the firmware build (PowerShell).
Expected: `SUCCESS`. Record the flash size in the ledger and compare it with Task 3's. The deletion should bring it down.

- [ ] **Step 8: Format and commit**

```bash
"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStatsJson.h src/ReadingStatsJson.cpp src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsTypes.h src/activities/reader/EpubReaderActivity.cpp test/reading_stats/ReadingStatsJsonTest.cpp test/reading_stats/ReadingStatsStoreTest.cpp test/reading_stats/ReadingStatsTest.cpp
git add -A src/ReadingStatsJson.h src/ReadingStatsJson.cpp src/ReadingStatsFile.h src/ReadingStatsFile.cpp src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsTypes.h src/activities/reader/EpubReaderActivity.cpp test/reading_stats/
git commit -m "refactor(stats): drop the JSON rewrite; ReadingStatsFile is now ReadingStatsJson

Nothing rewrites or byte-copies the JSON history any more: the
rewrite, the dashboard copy and the scan's target, victim and recents
go. What is left reads the legacy file for the import and writes the
web's JSON, so it is named for that."
```

---

### Task 6: Device checks and measurement

The user's hardware is required for every step. Ask for each one, and record every result in the ledger. Flash the firmware from Task 5.

- [ ] **Step 1: Import on the worst case (X3).** Put the 100 × 60 history on the card as `/.crosspoint/reading-stats.json` (`python scripts/gen_reading_stats_history.py --books 100 --days 60 --out reading-stats.json`; the user copies it over) with no `reading-stats.bin`. Boot to Home.
  - Expected: one log line `RST Imported 100 books from /.crosspoint/reading-stats.json in N ms`, with N around 2–3 s.
  - Expected: Home shows the recent books' history lines, `reading-stats.json.imported` is on the card, and a second boot does not import again.
- [ ] **Step 2: Reader exit, worst case (X3 and X4).** Read a page and leave the book.
  - Expected: `RST write done in N ms: load …, slot …, meta … (100 books, free=… contig=…)` with N under 300 ms. Record both boards.
- [ ] **Step 3: Sleep from a book.** Press power while reading.
  - Expected: the sleep screen appears without the old delay, the session is recorded (the `write done` line), and after waking the Book Info screen shows the added time.
- [ ] **Step 4: New book at the cap.** Open a book not in the history (100 books) and read briefly.
  - Expected: `Book cap (100) reached; dropping …` and `write done`; the summary still says 100 books.
- [ ] **Step 5: Stats screens.**
  - The summary opens; the top 3 match the list's first three.
  - The All-books list pages forwards and backwards (buttons and touch).
  - A book screen opens.
  - Remove from stats: the list shrinks by one and the summary total drops.
- [ ] **Step 6: Mark finished with real reading and a synced clock.**
  - Expected: finishing a book records a finish, visible on its stats screen, with a date.
- [ ] **Step 7: Web.**
  - `/stats` renders every book with a time-to-finish.
  - Export downloads a `reading-stats.json` that opens as valid JSON with 100 books.
  - Removal from the web works and the device list reflects it.
  - Record `LOG_WEB_MEM` `stats_api_enter/exit` free and contiguous figures.
- [ ] **Step 8: Record and hand over.** Put the measured numbers (import time, write times on X3 and X4, web heap) in the ledger. Then run the final review and the finishing-branch step per superpowers:executing-plans.
