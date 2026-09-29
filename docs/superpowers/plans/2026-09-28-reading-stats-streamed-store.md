# Reading Stats Without a Resident History — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every on-device reading-stats consumer streams `reading-stats.json` instead of loading the whole history, so memory stays flat up to the 100-book cap and no session is lost to a failed load.

**Architecture:** `ReadingStatsFile` (already on the base branch) grows from a web-only helper into the file layer for everything: tri-state scans, a time-ordered index, reading one entry at a byte offset, a JSON writer for one book and an edited copy of the file. `ReadingStatsStore` loses its resident vectors and becomes query functions, streamed rewrites (scan → merge one book → write temp → read back → swap) and a ~1 KB recent-books cache that Home fills once and every write keeps current. Consumers change one at a time; the resident store is deleted last.

**Tech Stack:** C++20, ESP32-C3 firmware via PlatformIO (pioarduino), host tests with GoogleTest via CMake + Ninja (MSYS2 UCRT64), `lib/JsonParser/StreamingJsonParser`.

**Spec:** `docs/superpowers/specs/2026-09-28-reading-stats-streamed-store-design.md`

## Global Constraints

- **The file format does not change.** Same keys, same order (`docId, title, author, totalSeconds, pagesTurned, sessions, firstReadEpoch, lastReadEpoch, progress, finishedCount, lastFinishedEpoch, finished, days`; header `totalSeconds, totalSessions, totalPagesTurned, longestStreak, globalDays, books`). Older firmware must read every file the new code writes.
- **Caps unchanged:** `kMaxBooks = 100`, `kMaxBookDays = 60`, `kMaxGlobalDays = 400`.
- **Deleting a book from the card never touches its history.** No hook in `FileBrowserActivity` or `CrossPointWebServer::handleDelete`.
- **No SD access from any `render()`.** Screens scan in `onEnter()`/`loop()` and draw from members, swapped in under `RenderLock`.
- **`StreamingJsonParser` is not modified** (`ReleaseJsonParser` depends on its behaviour). Titles are decoded for display only; untouched entries are copied byte for byte.
- **Memory:** stack locals < 256 B; heap through `makeUniqueNoThrow` (lib/Memory); no exceptions (`-fno-exceptions`); never `try/catch`.
- **No new user-facing strings.** Everything reuses existing `STR_*`.
- **Tooling (this machine):** host tests are built and run from **Git Bash**; firmware from **PowerShell** with `PLATFORMIO_CORE_DIR=C:\pio`; clang-format at `/c/Program Files/LLVM/bin/clang-format.exe`. The host target `epub_build_inventory` fails on Windows (`dlfcn.h`) — pre-existing, ignore it.
- **Commands used throughout:**
  - Configure host tests: `cmake -S test -B build/test`
  - Build one host test: `cmake --build build/test --target ReadingStatsTest`
  - Run one host test: `./build/test/reading_stats/ReadingStatsTest.exe --gtest_filter='<Suite>.*'`
  - Full host suite: `cmake --build build/test -j 8 -- -k 0 && ctest --test-dir build/test -j 8`
  - Firmware (PowerShell): `$env:PLATFORMIO_CORE_DIR='C:\pio'; $env:PYTHONUTF8='1'; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e default`
  - Format: `"/c/Program Files/LLVM/bin/clang-format.exe" -i <files>`
- **Commits:** conventional style with scope, e.g. `feat(stats): …`, body wrapped at 72.

## Review Focus

1. **The very first session, with no stats file on the card** — must create the file with that one book (Task 7, `FirstSessionCreatesTheFile`).
2. **A title containing quotes, backslashes, newlines or control characters** — the rewritten file must still parse and the title must read back (Task 2 `EscapesTheTitleAndKeepsTheFieldOrder`, Task 7 `AwkwardTitleSurvivesARewrite`).
3. **A session recorded while the clock has never synced (`walltime == 0`)** — totals move, no day bucket, no dates (Task 3 `SessionWithoutAClockLeavesDaysAndDatesAlone`).
4. **A recent book that has no stats entry** — Home must not rescan the card for it on every visit (Task 4 `PrefetchRemembersBooksWithoutHistory`).
5. **A zero-byte `reading-stats.json`** (interrupted copy, user-created) — an empty history for queries, a fresh file on the next write (Task 4 `EmptyFileIsAnEmptyHistory`, Task 7 `EmptyFileTakesTheFirstSession`).

---

### Task 1: Scan everything the device needs from the file

Tri-state scans, full decoding of one book, the time-ordered index, the eviction candidate, recent-book snapshots and reading one entry at an offset. Also moves the plain data types into their own header so `ReadingStats.h` can include `ReadingStatsFile.h` later without a cycle.

**Files:**
- Create: `src/ReadingStatsTypes.h`
- Modify: `src/ReadingStats.h` (types move out; add `evictsBefore`)
- Modify: `src/ReadingStats.cpp` (define `evictsBefore`)
- Modify: `src/ReadingStatsFile.h`, `src/ReadingStatsFile.cpp`
- Test: `test/reading_stats/ReadingStatsFileTest.cpp`, `test/reading_stats/ReadingStatsTest.cpp`

**Interfaces:**
- Consumes: `ReadingStatsStore::trimBookDays`, `trimGlobalDays`, `countsTowardPace` (base branch).
- Produces:
  - `struct ReadingTotals { uint32_t totalSeconds, totalSessions, totalPagesTurned; uint16_t longestStreak; std::vector<DayBucket> globalDays; }` in `ReadingStatsTypes.h`
  - `static bool ReadingStatsStore::evictsBefore(time_t aLastRead, uint32_t aSeconds, time_t bLastRead, uint32_t bSeconds)`
  - `enum class ReadingStatsFile::ScanResult : uint8_t { Ok, NoMemory, IoError, Malformed }`
  - `struct ReadingStatsFile::IndexEntry { uint32_t totalSeconds; uint32_t offset; }`
  - `struct ReadingStatsFile::RecentSnapshot { std::string docId; bool known; uint32_t totalSeconds; uint16_t knownDays; time_t lastReadEpoch; uint8_t progress; }`
  - `struct ReadingStatsFile::ScanRequest { std::string findDocId; bool wantIndex; bool wantVictim; std::vector<std::string> recentDocIds; }`
  - `struct ReadingStatsFile::Summary : ReadingTotals` with `bookCount, finishedBookCount, paceSeconds, pacePercents, found, target, targetFirst, cutFirst, cutLast, byTime, hasVictim, victimDocId, victimFirst, recents`
  - `ScanResult ReadingStatsFile::scan(HalFile& in, Summary& summary, const ScanRequest& request)`
  - `ScanResult ReadingStatsFile::readBookAt(HalFile& in, size_t offset, BookReadingStats& book)`
  - `bool ReadingStatsFile::summarize(...)` keeps its signature (now `scan(...) == Ok`)

- [ ] **Step 1: Create `src/ReadingStatsTypes.h` and move the types into it**

```cpp
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// The reading history's plain data, shared by the store (ReadingStats.h) and the file layer
// (ReadingStatsFile.h) without either header including the other's.

// Day buckets are keyed by an ordinal day count (days since 1970-01-01 in
// LOCAL time, computed by localDayIndex() below). A "reading day" is the
// calendar day the session ENDED in — phase 2 keeps this simple and doesn't
// model the KOReader "day shift" / hour cutoff setting yet.
struct DayBucket {
  uint16_t dayIndex = 0;
  uint32_t seconds = 0;
};

// Per-book reading statistics. Keyed by KOReader document hash (or filename
// hash fallback) so a renamed/moved file keeps its history.
struct BookReadingStats {
  std::string docId;
  std::string title;
  std::string author;
  uint32_t totalSeconds = 0;  // idle-clamped, sum across all sessions
  uint32_t pagesTurned = 0;   // forward + backward
  uint32_t sessions = 0;      // session-open count
  // 0 if HalClock was never synced when the session ran. Treat as "unknown".
  time_t firstReadEpoch = 0;
  time_t lastReadEpoch = 0;
  uint8_t progress = 0;          // 0-100, snapshot of last known progress
  uint16_t finishedCount = 0;    // number of times the user has marked it finished
  time_t lastFinishedEpoch = 0;  // wallclock of the most recent finish (0 if unknown)
  // Sparse day buckets, sorted ascending by dayIndex. Only days with reading
  // are stored — the typical case is a few dozen entries.
  std::vector<DayBucket> days;
};

// The history's global figures: what every book adds to, and what a removal takes back.
struct ReadingTotals {
  uint32_t totalSeconds = 0;
  uint32_t totalSessions = 0;
  uint32_t totalPagesTurned = 0;
  // Longest run of consecutive reading days ever seen, persisted: globalDays keeps only the
  // newest kMaxGlobalDays buckets, so a record older than that window lives on as this number.
  uint16_t longestStreak = 0;
  std::vector<DayBucket> globalDays;  // sorted ascending
};
```

In `src/ReadingStats.h`, delete the `DayBucket` and `BookReadingStats` definitions (lines 7–14 and 20–40 of the base branch) and add `#include "ReadingStatsTypes.h"` after the standard includes. Keep the `localDayIndexFromEpoch` / `currentLocalDayIndex` declarations where they are.

In `src/ReadingStatsFile.h`, replace `#include "ReadingStats.h"` with `#include "ReadingStatsTypes.h"`, and add `#include "ReadingStats.h"` to the includes of `src/ReadingStatsFile.cpp` (the .cpp uses the store's static helpers).

- [ ] **Step 2: Build the existing host tests to confirm the move is neutral**

Run: `cmake --build build/test --target ReadingStatsTest && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: builds; `[  PASSED  ] 21 tests.`

- [ ] **Step 3: Write the failing tests**

Append to `test/reading_stats/ReadingStatsTest.cpp`:

```cpp
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
```

Append to `test/reading_stats/ReadingStatsFileTest.cpp` (inside the file's anonymous namespace add the helper; the tests go after the existing ones):

```cpp
// (anonymous namespace)
using ScanResult = ReadingStatsFile::ScanResult;

ReadingStatsFile::Summary scanOf(const std::string& file, const ReadingStatsFile::ScanRequest& request,
                                 ScanResult expected = ScanResult::Ok) {
  HalFile in = HalFile::fromString(file);
  ReadingStatsFile::Summary summary;
  EXPECT_EQ(ReadingStatsFile::scan(in, summary, request), expected);
  return summary;
}
```

```cpp
TEST(ReadingStatsFileScan, DecodesTheWholeTarget) {
  const std::string head = R"({"totalSeconds":900,"books":[)";
  const std::string dated =
      R"({"docId":"d","title":"Say \"hi\" \u00e9","author":"X","totalSeconds":900,"pagesTurned":9,"sessions":3,)"
      R"("firstReadEpoch":1767225600,"lastReadEpoch":1768046400,"progress":100,"finishedCount":2,)"
      R"("lastFinishedEpoch":1768046400,"finished":true,"days":[[20463,900]]})";
  ReadingStatsFile::ScanRequest request;
  request.findDocId = "d";

  const auto summary = scanOf(head + dated + "]}", request);

  ASSERT_TRUE(summary.found);
  const BookReadingStats& b = summary.target;
  // The parser passes \u escapes through undecoded; titles are for display only.
  EXPECT_EQ(b.title, "Say \"hi\" \\u00e9");
  EXPECT_EQ(b.author, "X");
  EXPECT_EQ(b.totalSeconds, 900u);
  EXPECT_EQ(b.pagesTurned, 9u);
  EXPECT_EQ(b.sessions, 3u);
  EXPECT_EQ(b.firstReadEpoch, 1767225600);
  EXPECT_EQ(b.lastReadEpoch, 1768046400);
  EXPECT_EQ(b.progress, 100);
  EXPECT_EQ(b.finishedCount, 2);
  EXPECT_EQ(b.lastFinishedEpoch, 1768046400);
  EXPECT_EQ(pairsOf(b.days), (std::vector<std::pair<uint16_t, uint32_t>>{{20463, 900}}));
  EXPECT_EQ(summary.targetFirst, head.size());
}

TEST(ReadingStatsFileScan, LegacyFinishedFlagCountsAsOneFinish) {
  const std::string legacy = R"({"docId":"l","totalSeconds":60,"progress":100,"finished":true,"days":[]})";
  ReadingStatsFile::ScanRequest request;
  request.findDocId = "l";

  const auto summary = scanOf(R"({"totalSeconds":60,"books":[)" + legacy + "]}", request);

  ASSERT_TRUE(summary.found);
  EXPECT_EQ(summary.target.finishedCount, 1);
  EXPECT_EQ(summary.finishedBookCount, 1u);
}

TEST(ReadingStatsFileScan, IndexOrdersBooksByTime) {
  ReadingStatsFile::ScanRequest request;
  request.wantIndex = true;

  const auto summary = scanOf(kFile, request);

  std::vector<std::pair<uint32_t, uint32_t>> index;
  for (const auto& e : summary.byTime) index.emplace_back(e.totalSeconds, e.offset);
  EXPECT_EQ(index, (std::vector<std::pair<uint32_t, uint32_t>>{
                       {1000, static_cast<uint32_t>(kFile.find(kBookA))},
                       {300, static_cast<uint32_t>(kFile.find(kBookB))},
                       {50, static_cast<uint32_t>(kFile.find(kBookC))}}));
}

TEST(ReadingStatsFileScan, VictimIsTheLeastRecentlyReadThenTheLeastRead) {
  const std::string x = R"({"docId":"x","totalSeconds":10,"lastReadEpoch":300,"days":[]})";
  const std::string y = R"({"docId":"y","totalSeconds":300,"lastReadEpoch":100,"days":[]})";
  const std::string z = R"({"docId":"z","totalSeconds":50,"lastReadEpoch":100,"days":[]})";
  const std::string file = R"({"totalSeconds":360,"books":[)" + x + "," + y + "," + z + "]}";
  ReadingStatsFile::ScanRequest request;
  request.wantVictim = true;

  const auto summary = scanOf(file, request);

  ASSERT_TRUE(summary.hasVictim);
  EXPECT_EQ(summary.victimDocId, "z");
  EXPECT_EQ(summary.victimFirst, file.find(z));
}

TEST(ReadingStatsFileScan, RecentsReportKnownAndUnknownBooks) {
  ReadingStatsFile::ScanRequest request;
  request.recentDocIds = {"b", "zz"};

  const auto summary = scanOf(kFile, request);

  ASSERT_EQ(summary.recents.size(), 2u);
  EXPECT_EQ(summary.recents[0].docId, "b");
  EXPECT_TRUE(summary.recents[0].known);
  EXPECT_EQ(summary.recents[0].totalSeconds, 300u);
  EXPECT_EQ(summary.recents[0].knownDays, 1);
  EXPECT_EQ(summary.recents[0].progress, 40);
  EXPECT_EQ(summary.recents[1].docId, "zz");
  EXPECT_FALSE(summary.recents[1].known);
}

TEST(ReadingStatsFileScan, TruncatedFileIsMalformed) {
  scanOf(kFile.substr(0, kFile.size() / 2), {}, ScanResult::Malformed);
}

TEST(ReadingStatsFileBookAt, DecodesTheEntryAtAnOffset) {
  HalFile in = HalFile::fromString(kFile);
  BookReadingStats book;

  ASSERT_EQ(ReadingStatsFile::readBookAt(in, kFile.find(kBookB), book), ScanResult::Ok);

  EXPECT_EQ(book.docId, "b");
  EXPECT_EQ(book.title, "B");
  EXPECT_EQ(book.totalSeconds, 300u);
  EXPECT_EQ(book.finishedCount, 1);
  EXPECT_EQ(pairsOf(book.days), (std::vector<std::pair<uint16_t, uint32_t>>{{20463, 300}}));
}

TEST(ReadingStatsFileBookAt, CutOffEntryIsMalformed) {
  const std::string cut = kFile.substr(0, kFile.find(kBookA) + kBookA.size() / 2);
  HalFile in = HalFile::fromString(cut);
  BookReadingStats book;

  EXPECT_EQ(ReadingStatsFile::readBookAt(in, kFile.find(kBookA), book), ScanResult::Malformed);
}
```

- [ ] **Step 4: Run to verify they fail**

Run: `cmake --build build/test --target ReadingStatsTest`
Expected: compile errors — `evictsBefore`, `ScanResult`, `ScanRequest`, `scan`, `readBookAt`, `IndexEntry` are not declared.

- [ ] **Step 5: Implement `evictsBefore`**

In `src/ReadingStats.h`, next to `countsTowardPace`:

```cpp
  // The cap's eviction order: the least recently read book goes first (an entry never read with
  // the clock set, lastReadEpoch 0, is the oldest); on the same date, the one with less time.
  static bool evictsBefore(time_t aLastRead, uint32_t aSeconds, time_t bLastRead, uint32_t bSeconds);
```

In `src/ReadingStats.cpp`:

```cpp
bool ReadingStatsStore::evictsBefore(const time_t aLastRead, const uint32_t aSeconds, const time_t bLastRead,
                                     const uint32_t bSeconds) {
  if (aLastRead != bLastRead) return aLastRead < bLastRead;
  return aSeconds < bSeconds;
}
```

and make the in-memory eviction in `recordSession` use it:

```cpp
      auto victim =
          std::min_element(books.begin(), books.end(), [](const BookReadingStats& a, const BookReadingStats& b) {
            return evictsBefore(a.lastReadEpoch, a.totalSeconds, b.lastReadEpoch, b.totalSeconds);
          });
```

- [ ] **Step 6: Replace the declarations in `src/ReadingStatsFile.h`**

Replace everything between `constexpr char kPath[] = …;` and `// The /api/stats payload:` with:

```cpp
// How a pass over the file ended. NoMemory and IoError are transient — the file may be fine and a
// later pass may succeed; Malformed is the file's own fault.
enum class ScanResult : uint8_t { Ok, NoMemory, IoError, Malformed };

// One book's place in the file, for the list ordered by time.
struct IndexEntry {
  uint32_t totalSeconds = 0;
  uint32_t offset = 0;  // byte offset of the entry's opening brace
};

// What Home draws for one recent book.
struct RecentSnapshot {
  std::string docId;
  bool known = false;  // false: the history holds no entry for this book
  uint32_t totalSeconds = 0;
  uint16_t knownDays = 0;  // dated reading days, as many as a load would keep
  time_t lastReadEpoch = 0;
  uint8_t progress = 0;
};

// What a pass collects beyond the global figures.
struct ScanRequest {
  std::string findDocId;                  // Summary::target
  bool wantIndex = false;                 // Summary::byTime
  bool wantVictim = false;                // Summary::hasVictim / victimDocId / victimFirst
  std::vector<std::string> recentDocIds;  // Summary::recents, one per id, in this order
};

// One pass over the file: the global figures and whatever the request asked for. Days are
// filtered and trimmed the way a load would.
struct Summary : ReadingTotals {
  uint32_t bookCount = 0;
  uint32_t finishedBookCount = 0;
  // Over the books far enough in to count toward the global pace (see ReadingStatsStore).
  uint32_t paceSeconds = 0;
  uint32_t pacePercents = 0;

  // The requested book, every field decoded (title and author for display only).
  bool found = false;
  BookReadingStats target;
  size_t targetFirst = 0;  // offset of its opening brace
  // Inclusive byte range that removes it from the books array, one separating comma included.
  size_t cutFirst = 0;
  size_t cutLast = 0;

  std::vector<IndexEntry> byTime;  // descending by time; equal times in file order

  // The book the cap would evict (ReadingStatsStore::evictsBefore order).
  bool hasVictim = false;
  std::string victimDocId;
  size_t victimFirst = 0;

  std::vector<RecentSnapshot> recents;
};

ScanResult scan(HalFile& in, Summary& summary, const ScanRequest& request);

// scan() for the global figures and, optionally, one book. False unless the scan was Ok.
bool summarize(HalFile& in, Summary& summary, const std::string& findDocId = "");

// Decodes the single entry whose opening brace is at `offset` (an IndexEntry::offset).
ScanResult readBookAt(HalFile& in, size_t offset, BookReadingStats& book);
```

Delete the old `struct Summary` and the old `bool summarize(...)` declaration from that range.

- [ ] **Step 7: Rewrite the scanner in `src/ReadingStatsFile.cpp`**

Add `#include <algorithm>` and `#include "ReadingStats.h"` to the includes. Add below `toCount`:

```cpp
time_t toEpoch(const char* text) {
  const long long v = strtoll(text, nullptr, 10);
  return v > 0 ? static_cast<time_t>(v) : 0;
}
```

Replace the whole `class Scanner { … };` with:

```cpp
// Walks the file with the SAX parser, keeping track of where it is in the stats layout, and hands
// the subclasses what they need: the top-level counters, the global day buckets, and each book
// with the offsets of its braces. Fed a byte at a time so that every event knows the offset of the
// byte that caused it, and onByte() sees each byte after the parser has.
//
//   { "totalSeconds": n, ..., "globalDays": [[d, s], ...], "books": [ { ..., "days": [[d, s]] } ] }
//   depth 1                   2            3              2          3            4  5
//
// In single-book mode (readBookAt) the input starts at one entry's opening brace: the book is at
// depth 1, its days at 2 and 3, and the pass ends when that object closes.
class Scanner {
 public:
  struct Book {
    std::string docId;
    std::string title;
    std::string author;
    uint32_t totalSeconds = 0;
    uint32_t pagesTurned = 0;
    uint32_t sessions = 0;
    time_t firstReadEpoch = 0;
    time_t lastReadEpoch = 0;
    uint8_t progress = 0;
    uint16_t finishedCount = 0;
    bool hasFinishedCount = false;
    bool finishedFlag = false;  // the legacy bool, read only when finishedCount is absent
    time_t lastFinishedEpoch = 0;
    std::vector<DayBucket> days;
    size_t first = 0;  // offset of '{'
    bool hasFields = false;

    bool finished() const { return hasFinishedCount ? finishedCount > 0 : finishedFlag; }

    // As a load would hold it.
    BookReadingStats toStats() const {
      BookReadingStats s;
      s.docId = docId;
      s.title = title;
      s.author = author;
      s.totalSeconds = totalSeconds;
      s.pagesTurned = pagesTurned;
      s.sessions = sessions;
      s.firstReadEpoch = firstReadEpoch;
      s.lastReadEpoch = lastReadEpoch;
      s.progress = progress;
      s.finishedCount = hasFinishedCount ? finishedCount : (finishedFlag ? 1 : 0);
      s.lastFinishedEpoch = lastFinishedEpoch;
      s.days = days;
      ReadingStatsStore::trimBookDays(s.days);
      return s;
    }
  };

  explicit Scanner(const bool singleBook = false) : singleBook_(singleBook) {}
  virtual ~Scanner() = default;

  ScanResult run(HalFile& in, const size_t start = 0) {
    auto parser = makeUniqueNoThrow<StreamingJsonParser>(callbacks());
    // Large reads: every file call costs ~1.5 ms on SD whatever its size (BufferedFileIO.h), and a
    // full history is ~100 KB.
    auto block = makeUniqueNoThrow<char[]>(kReadBlock);
    if (!parser || !block) {
      LOG_ERR("RSF", "OOM: stats file parser");
      return ScanResult::NoMemory;
    }
    if (!in.seekSet(start)) return ScanResult::IoError;
    char* const bytes = block.get();
    size_t offset = start;
    while (!done_) {
      const int n = in.read(bytes, kReadBlock);
      if (n < 0) return ScanResult::IoError;
      if (n == 0) break;
      for (int i = 0; i < n && !done_; ++i, ++offset) {
        offset_ = offset;
        parser->feed(&bytes[i], 1);
        onByte(bytes[i], offset, insideBooks(offset));
      }
    }
    const bool complete = singleBook_ ? done_ : (rootClosed_ && depth_ == 0);
    return !parser->hasError() && complete ? ScanResult::Ok : ScanResult::Malformed;
  }

 protected:
  enum class Top : uint8_t { Other, TotalSeconds, TotalSessions, TotalPagesTurned, LongestStreak, GlobalDays, Books };

  virtual void onCounter(Top, uint32_t) {}
  virtual void onGlobalDay(uint16_t, uint32_t) {}
  virtual void onBookStart(size_t) {}
  // While the parser takes in the book's closing brace, before onByte() sees it.
  virtual void onBookEnd(const Book&, size_t) {}
  // `inBooks`: strictly between the brackets of the books array.
  virtual void onByte(char, size_t, bool) {}

 private:
  enum class Field : uint8_t {
    Other,
    DocId,
    Title,
    Author,
    TotalSeconds,
    PagesTurned,
    Sessions,
    FirstRead,
    LastRead,
    Progress,
    FinishedCount,
    Finished,
    LastFinished,
    Days
  };

  static Top topFor(const char* key) {
    if (strcmp(key, "totalSeconds") == 0) return Top::TotalSeconds;
    if (strcmp(key, "totalSessions") == 0) return Top::TotalSessions;
    if (strcmp(key, "totalPagesTurned") == 0) return Top::TotalPagesTurned;
    if (strcmp(key, "longestStreak") == 0) return Top::LongestStreak;
    if (strcmp(key, "globalDays") == 0) return Top::GlobalDays;
    if (strcmp(key, "books") == 0) return Top::Books;
    return Top::Other;
  }

  static Field fieldFor(const char* key) {
    if (strcmp(key, "docId") == 0) return Field::DocId;
    if (strcmp(key, "title") == 0) return Field::Title;
    if (strcmp(key, "author") == 0) return Field::Author;
    if (strcmp(key, "totalSeconds") == 0) return Field::TotalSeconds;
    if (strcmp(key, "pagesTurned") == 0) return Field::PagesTurned;
    if (strcmp(key, "sessions") == 0) return Field::Sessions;
    if (strcmp(key, "firstReadEpoch") == 0) return Field::FirstRead;
    if (strcmp(key, "lastReadEpoch") == 0) return Field::LastRead;
    if (strcmp(key, "progress") == 0) return Field::Progress;
    if (strcmp(key, "finishedCount") == 0) return Field::FinishedCount;
    if (strcmp(key, "finished") == 0) return Field::Finished;
    if (strcmp(key, "lastFinishedEpoch") == 0) return Field::LastFinished;
    if (strcmp(key, "days") == 0) return Field::Days;
    return Field::Other;
  }

  int bookDepth() const { return singleBook_ ? 1 : 3; }
  bool inBookScope() const { return singleBook_ || top_ == Top::Books; }
  bool atBook() const { return inBookScope() && depth_ == bookDepth(); }
  bool atBookDayPair() const { return inBookScope() && field_ == Field::Days && depth_ == bookDepth() + 2; }
  bool atGlobalDayPair() const { return !singleBook_ && top_ == Top::GlobalDays && depth_ == 3; }

  bool insideBooks(const size_t offset) const {
    return booksOpen_ != kNone && offset > booksOpen_ && (booksClose_ == kNone || offset < booksClose_);
  }

  void objectStart() {
    ++depth_;
    if (!singleBook_ && depth_ == 1) rootSeen_ = true;
    if (atBook()) {
      book_ = Book{};
      book_.first = offset_;
      field_ = Field::Other;
      onBookStart(offset_);
    }
  }

  void objectEnd() {
    if (atBook()) onBookEnd(book_, offset_);
    if (depth_ == 1) {
      if (singleBook_) done_ = true;
      if (!singleBook_ && rootSeen_) rootClosed_ = true;
    }
    if (depth_ > 0) --depth_;
  }

  void arrayStart() {
    ++depth_;
    if (!singleBook_ && depth_ == 2 && top_ == Top::Books && booksOpen_ == kNone) booksOpen_ = offset_;
    if (atGlobalDayPair() || atBookDayPair()) {
      pairIndex_ = 0;
      pairDay_ = 0;
      pairSeconds_ = 0;
    }
  }

  void arrayEnd() {
    if ((atGlobalDayPair() || atBookDayPair()) && pairIndex_ >= 2 && pairDay_ != 0 && pairSeconds_ != 0) {
      // Same filter as the loader: a zero day or zero seconds is not a bucket.
      if (atGlobalDayPair()) {
        onGlobalDay(pairDay_, pairSeconds_);
      } else {
        book_.days.push_back({pairDay_, pairSeconds_});
      }
    }
    if (!singleBook_ && depth_ == 2 && top_ == Top::Books && booksClose_ == kNone) booksClose_ = offset_;
    if (depth_ > 0) --depth_;
  }

  void key(const char* text) {
    if (!singleBook_ && depth_ == 1) {
      top_ = topFor(text);
    } else if (atBook()) {
      field_ = fieldFor(text);
      book_.hasFields = true;
    }
  }

  void number(const char* text) {
    if (!singleBook_ && depth_ == 1) {
      onCounter(top_, toCount(text));
    } else if (atGlobalDayPair() || atBookDayPair()) {
      if (pairIndex_ == 0) pairDay_ = static_cast<uint16_t>(std::min<uint32_t>(toCount(text), 0xFFFF));
      if (pairIndex_ == 1) pairSeconds_ = toCount(text);
      ++pairIndex_;
    } else if (atBook()) {
      switch (field_) {
        case Field::TotalSeconds:
          book_.totalSeconds = toCount(text);
          break;
        case Field::PagesTurned:
          book_.pagesTurned = toCount(text);
          break;
        case Field::Sessions:
          book_.sessions = toCount(text);
          break;
        case Field::FirstRead:
          book_.firstReadEpoch = toEpoch(text);
          break;
        case Field::LastRead:
          book_.lastReadEpoch = toEpoch(text);
          break;
        case Field::Progress:
          book_.progress = static_cast<uint8_t>(std::min<uint32_t>(toCount(text), 0xFF));
          break;
        case Field::FinishedCount:
          book_.finishedCount = static_cast<uint16_t>(std::min<uint32_t>(toCount(text), 0xFFFF));
          book_.hasFinishedCount = true;
          break;
        case Field::LastFinished:
          book_.lastFinishedEpoch = toEpoch(text);
          break;
        default:
          break;
      }
    }
  }

  void string(const char* text) {
    if (!atBook()) return;
    if (field_ == Field::DocId) book_.docId = text;
    if (field_ == Field::Title) book_.title = text;
    if (field_ == Field::Author) book_.author = text;
  }

  void boolean(const bool value) {
    if (atBook() && field_ == Field::Finished) book_.finishedFlag = value;
  }

  JsonCallbacks callbacks() {
    JsonCallbacks cb{};
    cb.ctx = this;
    cb.onKey = [](void* ctx, const char* v, size_t) { static_cast<Scanner*>(ctx)->key(v); };
    cb.onString = [](void* ctx, const char* v, size_t) { static_cast<Scanner*>(ctx)->string(v); };
    cb.onNumber = [](void* ctx, const char* v, size_t) { static_cast<Scanner*>(ctx)->number(v); };
    cb.onBool = [](void* ctx, bool v) { static_cast<Scanner*>(ctx)->boolean(v); };
    cb.onObjectStart = [](void* ctx) { static_cast<Scanner*>(ctx)->objectStart(); };
    cb.onObjectEnd = [](void* ctx) { static_cast<Scanner*>(ctx)->objectEnd(); };
    cb.onArrayStart = [](void* ctx) { static_cast<Scanner*>(ctx)->arrayStart(); };
    cb.onArrayEnd = [](void* ctx) { static_cast<Scanner*>(ctx)->arrayEnd(); };
    return cb;
  }

  const bool singleBook_;
  size_t offset_ = 0;
  int depth_ = 0;
  bool rootSeen_ = false;
  bool rootClosed_ = false;
  bool done_ = false;
  Top top_ = Top::Other;
  Field field_ = Field::Other;
  size_t booksOpen_ = kNone;
  size_t booksClose_ = kNone;
  uint8_t pairIndex_ = 0;
  uint16_t pairDay_ = 0;
  uint32_t pairSeconds_ = 0;
  Book book_;
};
```

- [ ] **Step 8: Rewrite `SummaryScan`, `summarize` and add `scan`/`readBookAt`**

Replace `class SummaryScan final : public Scanner { … };` with:

```cpp
class SummaryScan final : public Scanner {
 public:
  SummaryScan(Summary& summary, const ScanRequest& request) : summary_(summary), request_(request) {
    summary_.recents.resize(request.recentDocIds.size());
    for (size_t i = 0; i < request.recentDocIds.size(); ++i) summary_.recents[i].docId = request.recentDocIds[i];
  }

  // After the pass: the target's cut range, taking one separating comma with it (the one before
  // it when it has a predecessor, else the one after it), and the index in time order.
  void settle() {
    if (summary_.found) {
      if (prevLast_ != kNone) {
        summary_.cutFirst = prevLast_ + 1;
        summary_.cutLast = targetLast_;
      } else if (nextFirst_ != kNone) {
        summary_.cutFirst = summary_.targetFirst;
        summary_.cutLast = nextFirst_ - 1;
      } else {
        summary_.cutFirst = summary_.targetFirst;
        summary_.cutLast = targetLast_;
      }
    }
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

  void onGlobalDay(const uint16_t day, const uint32_t seconds) override { summary_.globalDays.push_back({day, seconds}); }

  void onBookStart(const size_t first) override {
    if (summary_.found && nextFirst_ == kNone) nextFirst_ = first;
  }

  void onBookEnd(const Book& book, const size_t last) override {
    // The loader skips an entry without a docId; so does everything counted here.
    if (!book.docId.empty()) {
      ++summary_.bookCount;
      if (book.finished()) ++summary_.finishedBookCount;
      if (ReadingStatsStore::countsTowardPace(book.progress)) {
        summary_.paceSeconds += book.totalSeconds;
        summary_.pacePercents += book.progress;
      }
      if (request_.wantIndex) summary_.byTime.push_back({book.totalSeconds, static_cast<uint32_t>(book.first)});
      if (request_.wantVictim &&
          (!summary_.hasVictim ||
           ReadingStatsStore::evictsBefore(book.lastReadEpoch, book.totalSeconds, victimLastRead_, victimSeconds_))) {
        summary_.hasVictim = true;
        summary_.victimDocId = book.docId;
        summary_.victimFirst = book.first;
        victimLastRead_ = book.lastReadEpoch;
        victimSeconds_ = book.totalSeconds;
      }
      for (RecentSnapshot& recent : summary_.recents) {
        if (recent.known || recent.docId != book.docId) continue;
        recent.known = true;
        recent.totalSeconds = book.totalSeconds;
        recent.knownDays = static_cast<uint16_t>(std::min(book.days.size(), ReadingStatsStore::kMaxBookDays));
        recent.lastReadEpoch = book.lastReadEpoch;
        recent.progress = book.progress;
      }
      if (!summary_.found && !request_.findDocId.empty() && book.docId == request_.findDocId) {
        summary_.found = true;
        summary_.target = book.toStats();
        summary_.targetFirst = book.first;
        targetLast_ = last;
        prevLast_ = lastBookLast_;
      }
    }
    lastBookLast_ = last;
  }

  Summary& summary_;
  const ScanRequest& request_;
  size_t lastBookLast_ = kNone;
  size_t prevLast_ = kNone;
  size_t targetLast_ = kNone;
  size_t nextFirst_ = kNone;
  time_t victimLastRead_ = 0;
  uint32_t victimSeconds_ = 0;
};

class BookAt final : public Scanner {
 public:
  explicit BookAt(BookReadingStats& out) : Scanner(/*singleBook=*/true), out_(out) {}

 private:
  void onBookEnd(const Book& book, size_t) override { out_ = book.toStats(); }

  BookReadingStats& out_;
};
```

Replace the old `bool summarize(...)` definition with:

```cpp
ScanResult scan(HalFile& in, Summary& summary, const ScanRequest& request) {
  summary = Summary{};
  SummaryScan pass(summary, request);
  const ScanResult result = pass.run(in);
  if (result != ScanResult::Ok) return result;
  // What a load would hold: the newest kMaxGlobalDays buckets, the record folded in first.
  ReadingStatsStore::trimGlobalDays(summary.globalDays, summary.longestStreak);
  pass.settle();
  return ScanResult::Ok;
}

bool summarize(HalFile& in, Summary& summary, const std::string& findDocId) {
  ScanRequest request;
  request.findDocId = findDocId;
  return scan(in, summary, request) == ScanResult::Ok;
}

ScanResult readBookAt(HalFile& in, const size_t offset, BookReadingStats& book) {
  book = BookReadingStats{};
  BookAt pass(book);
  return pass.run(in, offset);
}
```

In `writeDashboard` and `writeWithoutTarget`, the second pass now returns a `ScanResult`; change both checks from `if (!copy.run(in)) {` to `if (copy.run(in) != ScanResult::Ok) {`.

- [ ] **Step 9: Run the tests**

Run: `"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStatsTypes.h src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/*.cpp && cmake --build build/test --target ReadingStatsTest && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: `[  PASSED  ] 30 tests.` (21 existing + 9 new).

- [ ] **Step 10: Firmware build**

Run the firmware command (PowerShell). Expected: `[SUCCESS]`.

- [ ] **Step 11: Commit**

```bash
git add src/ReadingStatsTypes.h src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsTest.cpp test/reading_stats/ReadingStatsFileTest.cpp
git commit -m "feat(stats): scan everything the device needs from the stats file

Tri-state scans (Ok, NoMemory, IoError, Malformed), a book decoded in
full, the index ordered by time, the cap's eviction candidate, recent
book snapshots and one entry read at its byte offset. The data types
move to ReadingStatsTypes.h so the store can include the file layer."
```

---

### Task 2: Write one book and an edited copy of the file

**Files:**
- Modify: `src/ReadingStatsFile.h`, `src/ReadingStatsFile.cpp`
- Test: `test/reading_stats/ReadingStatsFileTest.cpp`

**Interfaces:**
- Consumes: `Scanner`, `ScanResult`, `Summary::targetFirst` (Task 1); `ReadingStatsStore::takeOut` (base branch, still with separate references).
- Produces:
  - `void ReadingStatsFile::writeBook(Print& out, const BookReadingStats& book)`
  - `constexpr size_t ReadingStatsFile::kNoEntry`
  - `struct ReadingStatsFile::Rewrite { ReadingTotals totals; size_t replaceAt; const BookReadingStats* replacement; size_t dropAt; const BookReadingStats* append; }`
  - `ScanResult ReadingStatsFile::writeRewrite(HalFile* in, const Rewrite& rewrite, Print& out)` — `in == nullptr` means no existing entries
  - `Summary::cutFirst` / `cutLast` are deleted (the element copy re-emits separators itself)

- [ ] **Step 1: Write the failing tests**

Add to `test/reading_stats/ReadingStatsFileTest.cpp`:

```cpp
// (anonymous namespace) B after one more session, as the device would write it.
const std::string kBookBAfter =
    R"({"docId":"b","title":"B","author":"","totalSeconds":450,"pagesTurned":8,"sessions":2,"firstReadEpoch":0,)"
    R"("lastReadEpoch":0,"progress":50,"finishedCount":1,"lastFinishedEpoch":0,"finished":true,)"
    R"("days":[[20463,300],[20464,150]]})";
const std::string kBookD =
    R"({"docId":"d","title":"D","author":"","totalSeconds":120,"pagesTurned":3,"sessions":1,"firstReadEpoch":0,)"
    R"("lastReadEpoch":0,"progress":5,"finishedCount":0,"lastFinishedEpoch":0,"finished":false,)"
    R"("days":[[20464,120]]})";

BookReadingStats bookBAfter() {
  BookReadingStats b;
  b.docId = "b";
  b.title = "B";
  b.totalSeconds = 450;
  b.pagesTurned = 8;
  b.sessions = 2;
  b.progress = 50;
  b.finishedCount = 1;
  b.days = {{20463, 300}, {20464, 150}};
  return b;
}

BookReadingStats bookD() {
  BookReadingStats d;
  d.docId = "d";
  d.title = "D";
  d.totalSeconds = 120;
  d.pagesTurned = 3;
  d.sessions = 1;
  d.progress = 5;
  d.days = {{20464, 120}};
  return d;
}

ReadingTotals totalsOf(uint32_t seconds, uint32_t sessions, uint32_t pages, std::vector<DayBucket> days) {
  ReadingTotals t;
  t.totalSeconds = seconds;
  t.totalSessions = sessions;
  t.totalPagesTurned = pages;
  t.longestStreak = 2;
  t.globalDays = std::move(days);
  return t;
}

std::string rewriteOf(const std::string* file, const ReadingStatsFile::Rewrite& rewrite) {
  StringPrint out;
  if (file == nullptr) {
    EXPECT_EQ(ReadingStatsFile::writeRewrite(nullptr, rewrite, out), ReadingStatsFile::ScanResult::Ok);
  } else {
    HalFile in = HalFile::fromString(*file);
    EXPECT_EQ(ReadingStatsFile::writeRewrite(&in, rewrite, out), ReadingStatsFile::ScanResult::Ok);
  }
  return out.text;
}
```

```cpp
TEST(ReadingStatsFileWriteBook, EscapesTheTitleAndKeepsTheFieldOrder) {
  BookReadingStats b;
  b.docId = "e";
  b.title = "A \"q\" \\ x\ny\x01";
  b.author = "Ö";
  b.totalSeconds = 5;
  b.pagesTurned = 2;
  b.sessions = 1;
  b.firstReadEpoch = 1767225600;
  b.lastReadEpoch = 1768046400;
  b.progress = 7;
  b.days = {{20463, 5}};
  StringPrint out;

  ReadingStatsFile::writeBook(out, b);

  EXPECT_EQ(out.text,
            R"({"docId":"e","title":"A \"q\" \\ x\ny\u0001","author":"Ö","totalSeconds":5,"pagesTurned":2,)"
            R"("sessions":1,"firstReadEpoch":1767225600,"lastReadEpoch":1768046400,"progress":7,"finishedCount":0,)"
            R"("lastFinishedEpoch":0,"finished":false,"days":[[20463,5]]})");
}

TEST(ReadingStatsFileRewrite, ReplacesAnEntryInPlace) {
  const BookReadingStats after = bookBAfter();
  ReadingStatsFile::Rewrite rewrite;
  rewrite.totals = totalsOf(1500, 5, 26, {{20463, 950}, {20464, 550}});
  rewrite.replaceAt = kFile.find(kBookB);
  rewrite.replacement = &after;

  EXPECT_EQ(rewriteOf(&kFile, rewrite),
            R"({"totalSeconds":1500,"totalSessions":5,"totalPagesTurned":26,"longestStreak":2,)"
            R"("globalDays":[[20463,950],[20464,550]],"books":[)" +
                kBookA + "," + kBookBAfter + "," + kBookC + "]}");
}

TEST(ReadingStatsFileRewrite, DropsOneEntryAndAppendsAnother) {
  const BookReadingStats d = bookD();
  ReadingStatsFile::Rewrite rewrite;
  rewrite.totals = totalsOf(1470, 5, 26, {{20463, 950}, {20464, 520}});
  rewrite.dropAt = kFile.find(kBookC);
  rewrite.append = &d;

  EXPECT_EQ(rewriteOf(&kFile, rewrite),
            R"({"totalSeconds":1470,"totalSessions":5,"totalPagesTurned":26,"longestStreak":2,)"
            R"("globalDays":[[20463,950],[20464,520]],"books":[)" +
                kBookA + "," + kBookB + "," + kBookD + "]}");
}

TEST(ReadingStatsFileRewrite, WithoutAnInputWritesJustTheNewBook) {
  const BookReadingStats d = bookD();
  ReadingStatsFile::Rewrite rewrite;
  rewrite.totals = totalsOf(120, 1, 3, {{20464, 120}});
  rewrite.append = &d;

  EXPECT_EQ(rewriteOf(nullptr, rewrite),
            R"({"totalSeconds":120,"totalSessions":1,"totalPagesTurned":3,"longestStreak":2,)"
            R"("globalDays":[[20464,120]],"books":[)" +
                kBookD + "]}");
}

TEST(ReadingStatsFileRewrite, OutputScansBack) {
  const BookReadingStats after = bookBAfter();
  ReadingStatsFile::Rewrite rewrite;
  rewrite.totals = totalsOf(1500, 5, 26, {{20463, 950}, {20464, 550}});
  rewrite.replaceAt = kFile.find(kBookB);
  rewrite.replacement = &after;
  const std::string written = rewriteOf(&kFile, rewrite);
  ReadingStatsFile::ScanRequest request;
  request.findDocId = "b";

  const auto summary = scanOf(written, request);

  EXPECT_EQ(summary.bookCount, 3u);
  ASSERT_TRUE(summary.found);
  EXPECT_EQ(summary.target.totalSeconds, 450u);
  EXPECT_EQ(summary.totalSeconds, 1500u);
}
```

Change the expectation of the existing `ReadingStatsFileRemove.CopesWithWhitespaceBetweenBooks`: entries are now copied one by one and the separators re-emitted, so the whitespace goes:

```cpp
  EXPECT_EQ(removeFrom(file, "a"),
            R"({"totalSeconds":300,"totalSessions":1,"totalPagesTurned":5,"longestStreak":2,)"
            R"("globalDays":[[20463,300]],"books":[)" +
                kBookB + "]}");
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build/test --target ReadingStatsTest`
Expected: compile errors — `writeBook`, `Rewrite`, `writeRewrite` not declared.

- [ ] **Step 3: Declare the writer in `src/ReadingStatsFile.h`**

Add after `readBookAt`:

```cpp
// One book as a JSON object, in the field order the device has always written. Strings are
// escaped the way ArduinoJson does: quote, backslash and control characters; UTF-8 passes through.
void writeBook(Print& out, const BookReadingStats& book);

constexpr size_t kNoEntry = static_cast<size_t>(-1);

// A rewrite of the file: new global figures and at most one entry replaced, one dropped and one
// appended. Entries are identified by the offset of their opening brace, as a scan reported them.
struct Rewrite {
  ReadingTotals totals;
  size_t replaceAt = kNoEntry;
  const BookReadingStats* replacement = nullptr;
  size_t dropAt = kNoEntry;
  const BookReadingStats* append = nullptr;
};

// Writes the whole new file to `out`: every untouched entry copied byte for byte, separators
// re-emitted. `in` may be null (no existing entries). Anything but Ok means the copy stopped early;
// the caller must not use the output.
ScanResult writeRewrite(HalFile* in, const Rewrite& rewrite, Print& out);
```

Delete `cutFirst` and `cutLast` (and their comment) from `Summary`.

- [ ] **Step 4: Implement in `src/ReadingStatsFile.cpp`**

Add to the anonymous namespace, after `emitDays`:

```cpp
void emitString(Print& out, const std::string& text) {
  emit(out, "\"");
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        emit(out, "\\\"");
        break;
      case '\\':
        emit(out, "\\\\");
        break;
      case '\b':
        emit(out, "\\b");
        break;
      case '\f':
        emit(out, "\\f");
        break;
      case '\n':
        emit(out, "\\n");
        break;
      case '\r':
        emit(out, "\\r");
        break;
      case '\t':
        emit(out, "\\t");
        break;
      default:
        if (c < 0x20) {
          char escaped[8];
          const int n = snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          emit(out, escaped, static_cast<size_t>(n));
        } else {
          out.write(c);
        }
    }
  }
  emit(out, "\"");
}
```

Replace `class CopyWithout final : public Scanner { … };` with:

```cpp
// Copies the entries through one by one, re-emitting the separators, with the rewrite's edits.
class RewriteCopy final : public Scanner {
 public:
  RewriteCopy(Print& out, const Rewrite& rewrite) : out_(out), rewrite_(rewrite) {}

  void appendAfterLast(const BookReadingStats& book) {
    separate();
    writeBook(out_, book);
  }

 private:
  enum class Mode : uint8_t { Between, Copy, Skip };

  void onBookStart(const size_t first) override {
    if (first == rewrite_.dropAt) {
      mode_ = Mode::Skip;
      return;
    }
    separate();
    if (first == rewrite_.replaceAt && rewrite_.replacement != nullptr) {
      writeBook(out_, *rewrite_.replacement);
      mode_ = Mode::Skip;
      return;
    }
    mode_ = Mode::Copy;
  }

  void onBookEnd(const Book&, size_t) override { ending_ = true; }

  void onByte(const char c, size_t, bool) override {
    if (mode_ == Mode::Copy) out_.write(static_cast<uint8_t>(c));
    if (ending_) {
      mode_ = Mode::Between;
      ending_ = false;
    }
  }

  void separate() {
    if (wroteAny_) emit(out_, ",");
    wroteAny_ = true;
  }

  Print& out_;
  const Rewrite& rewrite_;
  Mode mode_ = Mode::Between;
  bool ending_ = false;
  bool wroteAny_ = false;
};
```

Delete the target-cut computation from `SummaryScan` (`settle()` keeps only the sort; remove `prevLast_`, `targetLast_`, `nextFirst_`, `lastBookLast_` and the `onBookStart` override), and add at namespace scope (outside the anonymous namespace):

```cpp
void writeBook(Print& out, const BookReadingStats& book) {
  emit(out, "{\"docId\":");
  emitString(out, book.docId);
  emit(out, ",\"title\":");
  emitString(out, book.title);
  emit(out, ",\"author\":");
  emitString(out, book.author);
  char numbers[192];
  const int n = snprintf(numbers, sizeof(numbers),
                         ",\"totalSeconds\":%lu,\"pagesTurned\":%lu,\"sessions\":%lu,\"firstReadEpoch\":%lld,"
                         "\"lastReadEpoch\":%lld,\"progress\":%u,\"finishedCount\":%u,\"lastFinishedEpoch\":%lld,"
                         "\"finished\":%s,\"days\":",
                         static_cast<unsigned long>(book.totalSeconds), static_cast<unsigned long>(book.pagesTurned),
                         static_cast<unsigned long>(book.sessions), static_cast<long long>(book.firstReadEpoch),
                         static_cast<long long>(book.lastReadEpoch), book.progress, book.finishedCount,
                         static_cast<long long>(book.lastFinishedEpoch), book.finishedCount > 0 ? "true" : "false");
  emit(out, numbers, static_cast<size_t>(n));
  emitDays(out, book.days);
  emit(out, "}");
}

ScanResult writeRewrite(HalFile* in, const Rewrite& rewrite, Print& out) {
  char head[160];
  const int n = snprintf(head, sizeof(head),
                         "{\"totalSeconds\":%lu,\"totalSessions\":%lu,\"totalPagesTurned\":%lu,\"longestStreak\":%u,"
                         "\"globalDays\":",
                         static_cast<unsigned long>(rewrite.totals.totalSeconds),
                         static_cast<unsigned long>(rewrite.totals.totalSessions),
                         static_cast<unsigned long>(rewrite.totals.totalPagesTurned), rewrite.totals.longestStreak);
  emit(out, head, static_cast<size_t>(n));
  emitDays(out, rewrite.totals.globalDays);
  emit(out, ",\"books\":[");
  RewriteCopy copy(out, rewrite);
  const ScanResult result = in == nullptr ? ScanResult::Ok : copy.run(*in);
  if (rewrite.append != nullptr) copy.appendAfterLast(*rewrite.append);
  emit(out, "]}");
  return result;
}
```

Replace the body of `writeWithoutTarget` with:

```cpp
void writeWithoutTarget(HalFile& in, const Summary& summary, Print& out) {
  Rewrite rewrite;
  rewrite.totals = summary;  // the ReadingTotals part
  ReadingStatsStore::takeOut(summary.target, rewrite.totals.totalSeconds, rewrite.totals.totalSessions,
                             rewrite.totals.totalPagesTurned, rewrite.totals.globalDays);
  rewrite.dropAt = summary.targetFirst;
  if (writeRewrite(&in, rewrite, out) != ScanResult::Ok) {
    LOG_ERR("RSF", "Stats file changed or failed between passes; copy truncated");
  }
}
```

- [ ] **Step 5: Run the tests**

Run: `"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsFileTest.cpp && cmake --build build/test --target ReadingStatsTest && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: `[  PASSED  ] 35 tests.`

- [ ] **Step 6: Firmware build** — Expected: `[SUCCESS]`.

- [ ] **Step 7: Commit**

```bash
git add src/ReadingStatsFile.h src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsFileTest.cpp
git commit -m "feat(stats): write one book and an edited copy of the stats file

writeBook() emits one entry in the device's field order with JSON
escaping; writeRewrite() writes new global figures and copies every
untouched entry byte for byte, with one entry replaced, one dropped
and one appended. The web removal now goes through it; separators are
re-emitted, so hand-added whitespace between entries does not survive."
```

---

### Task 3: The session and finish arithmetic on plain data

**Files:**
- Modify: `src/ReadingStats.h`, `src/ReadingStats.cpp`, `src/ReadingStatsFile.cpp`
- Test: `test/reading_stats/ReadingStatsTest.cpp`

**Interfaces:**
- Consumes: `ReadingTotals` (Task 1).
- Produces:
  - `static void ReadingStatsStore::applySession(BookReadingStats& book, ReadingTotals& totals, const std::string& title, const std::string& author, uint32_t sessionSeconds, uint32_t sessionPagesTurned, uint8_t progress, time_t walltimeEpoch)`
  - `static void ReadingStatsStore::applyFinish(BookReadingStats& book, const std::string& title, const std::string& author, time_t walltimeEpoch)`
  - `static void ReadingStatsStore::takeOut(const BookReadingStats& book, ReadingTotals& totals)` (replaces the four-reference form)
  - The store's globals become one member `ReadingTotals totals_`

- [ ] **Step 1: Write the failing tests**

Append to `test/reading_stats/ReadingStatsTest.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build/test --target ReadingStatsTest`
Expected: compile errors — `applySession`, `applyFinish`, `takeOut(book, totals)` not declared.

- [ ] **Step 3: Declare in `src/ReadingStats.h`**

Replace the four-reference `takeOut` declaration with:

```cpp
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
```

Replace the private members `globalTotalSeconds`, `globalTotalSessions`, `globalTotalPagesTurned`, `globalDays`, `longestStreak_` (and their comments) with:

```cpp
  ReadingTotals totals_;
```

and point the getters at it:

```cpp
  uint16_t getLongestStreakSeen() const { return totals_.longestStreak; }
  uint32_t getGlobalTotalSeconds() const { return totals_.totalSeconds; }
  uint32_t getGlobalTotalSessions() const { return totals_.totalSessions; }
  uint32_t getGlobalTotalPagesTurned() const { return totals_.totalPagesTurned; }
  const std::vector<DayBucket>& getGlobalDays() const { return totals_.globalDays; }
```

- [ ] **Step 4: Implement in `src/ReadingStats.cpp`**

```cpp
void ReadingStatsStore::takeOut(const BookReadingStats& book, ReadingTotals& totals) {
  totals.totalSeconds -= std::min(totals.totalSeconds, book.totalSeconds);
  totals.totalSessions -= std::min(totals.totalSessions, book.sessions);
  totals.totalPagesTurned -= std::min(totals.totalPagesTurned, book.pagesTurned);
  for (const DayBucket& day : book.days) unmergeDay(totals.globalDays, day.dayIndex, day.seconds);
}

void ReadingStatsStore::applySession(BookReadingStats& book, ReadingTotals& totals, const std::string& title,
                                     const std::string& author, const uint32_t sessionSeconds,
                                     const uint32_t sessionPagesTurned, const uint8_t progress,
                                     const time_t walltimeEpoch) {
  if (!title.empty()) book.title = title;
  if (!author.empty()) book.author = author;
  book.totalSeconds += sessionSeconds;
  book.pagesTurned += sessionPagesTurned;
  book.progress = progress;
  if (sessionSeconds > 0) {
    book.sessions += 1;
    totals.totalSessions += 1;
  }
  if (walltimeEpoch != 0) {
    if (book.firstReadEpoch == 0) book.firstReadEpoch = walltimeEpoch;
    book.lastReadEpoch = walltimeEpoch;
    const uint16_t day = localDayIndexFromEpoch(walltimeEpoch);
    mergeDay(book.days, day, sessionSeconds, kMaxBookDays);
    mergeDay(totals.globalDays, day, sessionSeconds, kMaxGlobalDays);
    // Fold this day's run into the persisted longest streak while the whole run is still in the
    // window (a run longer than the window is already the record).
    const uint16_t run = runEndingAt(totals.globalDays, day);
    if (run > totals.longestStreak) totals.longestStreak = run;
  }
  totals.totalSeconds += sessionSeconds;
  totals.totalPagesTurned += sessionPagesTurned;
}

void ReadingStatsStore::applyFinish(BookReadingStats& book, const std::string& title, const std::string& author,
                                    const time_t walltimeEpoch) {
  if (!title.empty()) book.title = title;
  if (!author.empty()) book.author = author;
  book.finishedCount += 1;
  book.progress = 100;
  if (walltimeEpoch != 0) {
    book.lastFinishedEpoch = walltimeEpoch;
    if (book.lastReadEpoch < walltimeEpoch) book.lastReadEpoch = walltimeEpoch;
  }
}
```

Make the in-memory members use them:
- `recordSession`: keep the lookup, the zero-second early return and the eviction; for a new book push `fresh` with only `docId` set; then replace everything from `it->totalSeconds += sessionSeconds;` to the end of the function with `applySession(*it, totals_, title, author, sessionSeconds, sessionPagesTurned, progress, walltimeEpoch);` (and delete the `else { if (!title.empty()) … }` branch — `applySession` does it).
- `markFinished`: same shape: lookup or push a `fresh` with only `docId`; then `applyFinish(*it, title, author, walltimeEpoch);`.
- `removeBook`: `takeOut(*it, totals_);`
- `getSecondsForDay`, `computeCurrentStreak`, `computeLongestStreak`, `globalAvgSecondsPerPercent`: read `totals_.globalDays` / `totals_.totalSeconds` / `totals_.longestStreak`.
- `replaceLoaded`: assign the four scalars and `globalDays` into `totals_`; `trimGlobalDays(totals_.globalDays, totals_.longestStreak);`.
- `release`: `totals_ = ReadingTotals{};` (move assignment frees the old buckets) instead of the per-field resets.

In `src/ReadingStatsFile.cpp`, `writeWithoutTarget` becomes `ReadingStatsStore::takeOut(summary.target, rewrite.totals);`.

- [ ] **Step 5: Run the tests**

Run: `"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsTest.cpp && cmake --build build/test --target ReadingStatsTest && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: `[  PASSED  ] 41 tests.` — the seven in-memory `ReadingStatsRemoveBook.*` tests prove the refactored store unchanged.

- [ ] **Step 6: Firmware build** — Expected: `[SUCCESS]`.

- [ ] **Step 7: Commit**

```bash
git add src/ReadingStats.h src/ReadingStats.cpp src/ReadingStatsFile.cpp test/reading_stats/ReadingStatsTest.cpp
git commit -m "refactor(stats): session and finish arithmetic on plain data

applySession(), applyFinish() and takeOut() work on a book and a
ReadingTotals, which the store now holds its global figures in. The
in-memory store calls them, so behaviour is unchanged; a streamed
write can call them without a store."
```

---

### Task 4: Store queries and the recent-books cache

**Files:**
- Modify: `src/ReadingStats.h`, `src/ReadingStats.cpp`
- Create: `test/reading_stats/ReadingStatsStoreTest.cpp`
- Modify: `test/reading_stats/CMakeLists.txt`

**Interfaces:**
- Consumes: `ReadingStatsFile::scan`, `readBookAt`, `Summary`, `ScanRequest`, `RecentSnapshot` (Task 1).
- Produces:
  - `explicit ReadingStatsStore(std::string path = ReadingStatsFile::kPath)`
  - `struct ReadingStatsStore::BookQuery { bool found; BookReadingStats book; float pooledPace; }`
  - `ReadingStatsFile::ScanResult querySummary(ReadingStatsFile::Summary& out, bool withIndex = false) const`
  - `ReadingStatsFile::ScanResult queryBook(const std::string& docId, BookQuery& out) const`
  - `ReadingStatsFile::ScanResult queryBookAt(uint32_t offset, BookReadingStats& book) const`
  - `void prefetchRecent(const std::vector<std::string>& docIds)`
  - `const ReadingStatsFile::RecentSnapshot* recent(const std::string& docId) const`
  - `float recentPooledPace() const`
  - `static constexpr size_t kRecentCacheSize = 12`
  - private `void invalidateRecent()`; private `ReadingStatsFile::ScanResult scanFile(const ReadingStatsFile::ScanRequest&, ReadingStatsFile::Summary&) const`

- [ ] **Step 1: Add the file-backed test target**

Append to `test/reading_stats/CMakeLists.txt`:

```cmake
# The store against real files in a temporary directory: write, read back, rename, cache.
add_executable(ReadingStatsStoreTest
  ReadingStatsStoreTest.cpp
  ${REPO_ROOT}/src/ReadingStats.cpp
  ${REPO_ROOT}/src/ReadingStatsFile.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(ReadingStatsStoreTest PRIVATE
  ${REPO_ROOT}/src
  # HalStorage.h over stdio and std::filesystem. Must precede test/shims, whose HalStorage.h is the
  # no-op one, and lib/hal, whose HalStorage.h wants SdFat.
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/GfxRenderer
)

if(MINGW)
  target_compile_definitions(ReadingStatsStoreTest PRIVATE _POSIX_THREAD_SAFE_FUNCTIONS)
endif()

target_link_libraries(ReadingStatsStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingStatsStoreTest)
```

- [ ] **Step 2: Write the failing tests** — create `test/reading_stats/ReadingStatsStoreTest.cpp`:

```cpp
// ReadingStatsStore against real files: the streamed queries, the recent-books cache and (from the
// streamed-writes task on) every update, through the file-backed HalStorage shim.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "HalClock.h"
#include "JsonSettingsIO.h"
#include "ReadingStats.h"

// Link stubs. The resident store's load/save still reference the JSON layer until the streamed
// writes replace them; nothing here calls them.
namespace HalClock {
time_t now() { return 0; }
bool isSynced() { return false; }
}  // namespace HalClock

namespace JsonSettingsIO {
bool saveReadingStats(const ReadingStatsStore&, HalFile&) { return false; }
ReadingStatsLoad loadReadingStats(ReadingStatsStore&, HalFile&) { return ReadingStatsLoad::Corrupt; }
}  // namespace JsonSettingsIO

namespace {

using ScanResult = ReadingStatsFile::ScanResult;

const std::string kBookA =
    R"({"docId":"a","title":"Book A","author":"X","totalSeconds":1000,"pagesTurned":17,"sessions":2,)"
    R"("firstReadEpoch":0,"lastReadEpoch":0,"progress":25,"finishedCount":0,"lastFinishedEpoch":0,"finished":false,)"
    R"("days":[[20463,600],[20464,400]]})";
const std::string kBookB =
    R"({"docId":"b","title":"B","author":"","totalSeconds":300,"pagesTurned":5,"sessions":1,"firstReadEpoch":0,)"
    R"("lastReadEpoch":0,"progress":40,"finishedCount":1,"lastFinishedEpoch":0,"finished":true,"days":[[20463,300]]})";
const std::string kFile = R"({"totalSeconds":1300,"totalSessions":3,"totalPagesTurned":22,"longestStreak":2,)"
                          R"("globalDays":[[20463,900],[20464,400]],"books":[)" +
                          kBookA + "," + kBookB + "]}";

class StoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = std::filesystem::temp_directory_path() / (std::string("rst-") + info->test_suite_name() + "-" + info->name());
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    path_ = (dir_ / "reading-stats.json").generic_string();
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  void writeFile(const std::string& text) const { std::ofstream(path_, std::ios::binary) << text; }
  std::string readFile() const {
    std::ifstream f(path_, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
  }

  std::filesystem::path dir_;
  std::string path_;
};

}  // namespace

TEST_F(StoreTest, SummaryOfAnAbsentFileIsAnEmptyHistory) {
  ReadingStatsStore store(path_);
  ReadingStatsFile::Summary summary;

  EXPECT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 0u);
  EXPECT_EQ(summary.totalSeconds, 0u);
}

TEST_F(StoreTest, EmptyFileIsAnEmptyHistory) {
  writeFile("");
  ReadingStatsStore store(path_);
  ReadingStatsFile::Summary summary;

  EXPECT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 0u);
}

TEST_F(StoreTest, QueryBookFindsItAndTheGlobalPace) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  ReadingStatsStore::BookQuery query;

  ASSERT_EQ(store.queryBook("b", query), ScanResult::Ok);

  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 300u);
  // (1000 s + 300 s) over (25 % + 40 %) = 20 s per percent.
  EXPECT_FLOAT_EQ(query.pooledPace, 20.0f);
}

TEST_F(StoreTest, QueryBookAtReadsTheIndexedEntry) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary, /*withIndex=*/true), ScanResult::Ok);
  ASSERT_EQ(summary.byTime.size(), 2u);
  BookReadingStats book;

  ASSERT_EQ(store.queryBookAt(summary.byTime[0].offset, book), ScanResult::Ok);

  EXPECT_EQ(book.docId, "a");
  EXPECT_EQ(book.title, "Book A");
}

TEST_F(StoreTest, PrefetchRemembersBooksWithoutHistory) {
  writeFile(kFile);
  ReadingStatsStore store(path_);

  store.prefetchRecent({"b", "zz"});

  const auto* b = store.recent("b");
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(b->known);
  EXPECT_EQ(b->totalSeconds, 300u);
  const auto* zz = store.recent("zz");
  ASSERT_NE(zz, nullptr);
  EXPECT_FALSE(zz->known);
  EXPECT_FLOAT_EQ(store.recentPooledPace(), 20.0f);
}

TEST_F(StoreTest, WarmPrefetchDoesNotReadTheCard) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  store.prefetchRecent({"b", "zz"});
  // A second prefetch for the same books must be served from the cache: with the file gone, a
  // rescan would report both as unknown.
  std::filesystem::remove(path_);

  store.prefetchRecent({"b", "zz"});

  ASSERT_NE(store.recent("b"), nullptr);
  EXPECT_TRUE(store.recent("b")->known);
}

TEST_F(StoreTest, CacheIsBounded) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  std::vector<std::string> many;
  for (int i = 0; i < 20; ++i) many.push_back("id" + std::to_string(i));
  store.prefetchRecent(many);

  store.prefetchRecent({"b"});

  EXPECT_NE(store.recent("b"), nullptr);
  size_t cached = 0;
  for (const auto& id : many) cached += store.recent(id) != nullptr ? 1 : 0;
  EXPECT_LE(cached + 1, ReadingStatsStore::kRecentCacheSize);
}
```

- [ ] **Step 3: Run to verify they fail**

Run: `cmake -S test -B build/test && cmake --build build/test --target ReadingStatsStoreTest`
Expected: compile errors — no `ReadingStatsStore(std::string)` constructor, no `querySummary`, `BookQuery`, `prefetchRecent`, `recent`.

- [ ] **Step 4: Declare in `src/ReadingStats.h`**

Add `#include "ReadingStatsFile.h"` to the includes. In the public section:

```cpp
  explicit ReadingStatsStore(std::string path = ReadingStatsFile::kPath) : path_(std::move(path)) {}

  // ---- Streamed queries -------------------------------------------------------------------------
  //
  // Each reads the file once and returns only what was asked for; nothing stays resident. An absent
  // or empty file is an empty history (Ok). Call from the loop task, never from render().
  struct BookQuery {
    bool found = false;
    BookReadingStats book;
    float pooledPace = 0.0f;  // the global pace, for a book without one of its own
  };
  ReadingStatsFile::ScanResult querySummary(ReadingStatsFile::Summary& out, bool withIndex = false) const;
  ReadingStatsFile::ScanResult queryBook(const std::string& docId, BookQuery& out) const;
  ReadingStatsFile::ScanResult queryBookAt(uint32_t offset, BookReadingStats& book) const;

  // ---- Recent-books cache -----------------------------------------------------------------------
  //
  // What Home draws for its recent books, so the themes read no file from render(). Home calls
  // prefetchRecent() on entry; it scans only for books not cached yet (books without history are
  // cached as unknown). Bounded: the books asked for stay, others go past kRecentCacheSize.
  static constexpr size_t kRecentCacheSize = 12;
  void prefetchRecent(const std::vector<std::string>& docIds);
  const ReadingStatsFile::RecentSnapshot* recent(const std::string& docId) const;
  float recentPooledPace() const { return pooledPace_; }
```

In the private section:

```cpp
  ReadingStatsFile::ScanResult scanFile(const ReadingStatsFile::ScanRequest& request,
                                        ReadingStatsFile::Summary& summary) const;
  void invalidateRecent();

  std::string path_;
  std::vector<ReadingStatsFile::RecentSnapshot> recent_;
  float pooledPace_ = 0.0f;
  bool paceKnown_ = false;
```

- [ ] **Step 5: Implement in `src/ReadingStats.cpp`**

Replace every use of `READING_STATS_FILE` in the file with `path_.c_str()` (and `std::string(READING_STATS_FILE)` with `path_`), delete the `READING_STATS_FILE` constant, and give `swapIn()` and `readsBackWithout()` the path as a parameter:

```cpp
bool swapIn(const std::string& path, const std::string& tmpPath) {
  Storage.remove(path.c_str());
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("RST", "Could not rename %s into place", tmpPath.c_str());
    return false;
  }
  return true;
}
```

The corrupt-file path in `loadFromFile()` becomes `path_` with `.json` replaced by `.corrupt.json`; add to the anonymous namespace:

```cpp
std::string asidePathFor(const std::string& path) {
  constexpr char kSuffix[] = ".json";
  const size_t n = sizeof(kSuffix) - 1;
  if (path.size() > n && path.compare(path.size() - n, n, kSuffix) == 0) {
    return path.substr(0, path.size() - n) + ".corrupt.json";
  }
  return path + ".corrupt";
}

std::string parentDirOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}
```

and `saveToFile()`'s `Storage.mkdir("/.crosspoint")` becomes `Storage.mkdir(parentDirOf(path_).c_str())`.

Then add the queries and the cache:

```cpp
ReadingStatsFile::ScanResult ReadingStatsStore::scanFile(const ReadingStatsFile::ScanRequest& request,
                                                        ReadingStatsFile::Summary& summary) const {
  summary = ReadingStatsFile::Summary{};
  const auto emptyHistory = [&]() {
    for (const auto& id : request.recentDocIds) {
      ReadingStatsFile::RecentSnapshot unknown;
      unknown.docId = id;
      summary.recents.push_back(std::move(unknown));
    }
    return ReadingStatsFile::ScanResult::Ok;
  };
  if (!Storage.exists(path_.c_str())) return emptyHistory();
  FsFile in;
  if (!Storage.openFileForRead("RST", path_.c_str(), in)) return ReadingStatsFile::ScanResult::IoError;
  if (in.size() == 0) return emptyHistory();
  return ReadingStatsFile::scan(in, summary, request);
}

ReadingStatsFile::ScanResult ReadingStatsStore::querySummary(ReadingStatsFile::Summary& out,
                                                            const bool withIndex) const {
  ReadingStatsFile::ScanRequest request;
  request.wantIndex = withIndex;
  return scanFile(request, out);
}

ReadingStatsFile::ScanResult ReadingStatsStore::queryBook(const std::string& docId, BookQuery& out) const {
  out = BookQuery{};
  ReadingStatsFile::ScanRequest request;
  request.findDocId = docId;
  ReadingStatsFile::Summary summary;
  const auto result = scanFile(request, summary);
  if (result != ReadingStatsFile::ScanResult::Ok) return result;
  out.found = summary.found;
  out.book = std::move(summary.target);
  out.pooledPace = pooledSecondsPerPercent(summary.totalSeconds, summary.paceSeconds, summary.pacePercents);
  return result;
}

ReadingStatsFile::ScanResult ReadingStatsStore::queryBookAt(const uint32_t offset, BookReadingStats& book) const {
  FsFile in;
  if (!Storage.openFileForRead("RST", path_.c_str(), in)) return ReadingStatsFile::ScanResult::IoError;
  return ReadingStatsFile::readBookAt(in, offset, book);
}

void ReadingStatsStore::prefetchRecent(const std::vector<std::string>& docIds) {
  ReadingStatsFile::ScanRequest request;
  for (const auto& id : docIds) {
    if (!id.empty() && recent(id) == nullptr) request.recentDocIds.push_back(id);
  }
  if (request.recentDocIds.empty() && paceKnown_) return;
  ReadingStatsFile::Summary summary;
  const auto result = scanFile(request, summary);
  if (result != ReadingStatsFile::ScanResult::Ok) {
    LOG_ERR("RST", "prefetchRecent: scan failed (%u); Home draws no history this time", static_cast<unsigned>(result));
    return;
  }
  for (auto& snapshot : summary.recents) recent_.push_back(std::move(snapshot));
  pooledPace_ = pooledSecondsPerPercent(summary.totalSeconds, summary.paceSeconds, summary.pacePercents);
  paceKnown_ = true;
  for (auto it = recent_.begin(); recent_.size() > kRecentCacheSize && it != recent_.end();) {
    if (std::find(docIds.begin(), docIds.end(), it->docId) == docIds.end()) {
      it = recent_.erase(it);
    } else {
      ++it;
    }
  }
}

const ReadingStatsFile::RecentSnapshot* ReadingStatsStore::recent(const std::string& docId) const {
  for (const auto& snapshot : recent_) {
    if (snapshot.docId == docId) return &snapshot;
  }
  return nullptr;
}

void ReadingStatsStore::invalidateRecent() {
  std::vector<ReadingStatsFile::RecentSnapshot>().swap(recent_);
  paceKnown_ = false;
}
```

- [ ] **Step 6: Run the tests**

Run: `"/c/Program Files/LLVM/bin/clang-format.exe" -i src/ReadingStats.h src/ReadingStats.cpp test/reading_stats/ReadingStatsStoreTest.cpp && cmake --build build/test --target ReadingStatsStoreTest --target ReadingStatsTest && ./build/test/reading_stats/ReadingStatsStoreTest.exe && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: `ReadingStatsStoreTest`: `[  PASSED  ] 7 tests.`; `ReadingStatsTest`: `[  PASSED  ] 41 tests.`

- [ ] **Step 7: Firmware build** — Expected: `[SUCCESS]`.

- [ ] **Step 8: Commit**

```bash
git add src/ReadingStats.h src/ReadingStats.cpp test/reading_stats/CMakeLists.txt test/reading_stats/ReadingStatsStoreTest.cpp
git commit -m "feat(stats): streamed store queries and a recent-books cache

querySummary(), queryBook() and queryBookAt() read the file once and
hold nothing. prefetchRecent() fills a bounded cache of what Home
draws for its recent books, books without history included, and
scans only for books it does not hold yet. The store takes its path as
a constructor argument; ReadingStatsStoreTest runs it on real files."
```

---

### Task 5: Home and Book Info read through the queries

**Files:**
- Modify: `src/components/BookProgressPresentation.cpp`
- Modify: `src/activities/home/HomeActivity.h`, `src/activities/home/HomeActivity.cpp`
- Modify: `src/activities/home/BookInfoActivity.h`, `src/activities/home/BookInfoActivity.cpp`
- Modify: `src/ReadingStats.cpp` (transitional cache invalidation)
- Test: `test/reading_stats/ReadingStatsStoreTest.cpp`

**Interfaces:**
- Consumes: `prefetchRecent`, `recent`, `recentPooledPace`, `queryBook`, `BookQuery` (Task 4); `ownSecondsPerPercent`, `etaSeconds` (base branch).
- Produces: nothing new. Until Task 7, `saveToFile()` and `removeBookFromFile()` clear the cache after every successful write so Home never shows figures older than the file.

- [ ] **Step 1: Write the failing test** — append to `test/reading_stats/ReadingStatsStoreTest.cpp`:

```cpp
TEST_F(StoreTest, ARemovalDropsTheCachedFigures) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  store.prefetchRecent({"a"});
  ASSERT_NE(store.recent("a"), nullptr);

  ASSERT_EQ(store.removeBookFromFile("a"), ReadingStatsStore::FileRemoval::Removed);

  EXPECT_EQ(store.recent("a"), nullptr);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/test --target ReadingStatsStoreTest && ./build/test/reading_stats/ReadingStatsStoreTest.exe --gtest_filter='StoreTest.ARemovalDropsTheCachedFigures'`
Expected: FAIL — `store.recent("a")` is still the cached snapshot.

- [ ] **Step 3: Invalidate on every write (transitional)**

In `src/ReadingStats.cpp`: in `saveToFile()`, replace the final `return swapIn(path_, tmpPath);` with

```cpp
  if (!swapIn(path_, tmpPath)) return false;
  // Until the writes keep the cache current themselves, a write empties it.
  const_cast<ReadingStatsStore*>(this)->invalidateRecent();
  return true;
```

and in `removeBookFromFile()`, before `return swapIn(...) ? FileRemoval::Removed : FileRemoval::Failed;` in the streamed branch, write

```cpp
  if (!swapIn(path_, tmpPath)) return FileRemoval::Failed;
  invalidateRecent();
  return FileRemoval::Removed;
```

(the loaded branch goes through `saveToFile()`, which already invalidates).

- [ ] **Step 4: Run the test** — Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 5: `BookProgressPresentation` reads the cache**

In `src/components/BookProgressPresentation.cpp`, replace `bookEtaSuffix` and add a lookup helper in the same anonymous namespace:

```cpp
// The cached figures for a recent book (HomeActivity::onEnter() prefetched them), or null when the
// book has no history. Never reads the card: this runs from the themes' render().
const ReadingStatsFile::RecentSnapshot* historyOf(const RecentBook& book) {
  const auto* snapshot = READING_STATS.recent(KOReaderDocumentId::calculateFromFilename(book.path));
  return snapshot != nullptr && snapshot->known ? snapshot : nullptr;
}

// Pace-based "time to finish" suffix for a book, e.g. "~45m". Empty when the
// book is finished, has no progress data, or has too little history to estimate.
std::string bookEtaSuffix(const RecentBook& book, int progressPercent) {
  if (progressPercent < 0 || progressPercent >= 100) {
    return {};
  }
  const auto* history = historyOf(book);
  const float own =
      history != nullptr ? ReadingStatsStore::ownSecondsPerPercent(history->totalSeconds, history->progress) : 0.0f;
  const float pace = own > 0.0f ? own : READING_STATS.recentPooledPace();
  const uint32_t etaSeconds = ReadingStatsStore::etaSeconds(pace, 100.0f - static_cast<float>(progressPercent));
  if (etaSeconds == 0) {
    return {};
  }
  return "~" + formatEtaShort(etaSeconds);
}
```

In `historyLine()` and `historyLineCompact()`, replace

```cpp
  const BookReadingStats* stats = READING_STATS.findBook(KOReaderDocumentId::calculateFromFilename(book.path));
  if (stats == nullptr || stats->totalSeconds == 0) {
```

with

```cpp
  const auto* stats = historyOf(book);
  if (stats == nullptr || stats->totalSeconds == 0) {
```

and replace each day-counting loop

```cpp
  size_t knownDays = 0;
  for (const auto& day : stats->days) {
    if (day.dayIndex != 0) ++knownDays;
  }
```

with `const size_t knownDays = stats->knownDays;` (the scanner already drops undated buckets). The comment above the loop in `historyLine` stays.

- [ ] **Step 6: Home prefetches instead of loading**

In `src/activities/home/HomeActivity.h`, delete the `statsLoad_` member and its comment. In `src/activities/home/HomeActivity.cpp`:
- delete `statsLoad_.emplace();` and the comment block above it in `onEnter()`;
- delete `statsLoad_.reset();` in `onExit()`;
- add `#include "KOReaderDocumentId.h"` if it is not already included;
- directly after `loadRecentBooks(metrics.homeRecentBooksCount);` in `onEnter()` insert:

```cpp
  // The themes draw each recent book's history and pace from the stats cache; fill it here, on the
  // loop task, so their render() reads no file. A warm cache makes this free.
  std::vector<std::string> recentDocIds;
  recentDocIds.reserve(recentBooks.size());
  for (const auto& book : recentBooks) recentDocIds.push_back(KOReaderDocumentId::calculateFromFilename(book.path));
  READING_STATS.prefetchRecent(recentDocIds);
```

- [ ] **Step 7: Book Info queries its one book**

In `src/activities/home/BookInfoActivity.h`, delete the `statsLoad_` member and its comment. In `src/activities/home/BookInfoActivity.cpp`, replace

```cpp
    if (const BookReadingStats* stats = READING_STATS.findBook(docId)) {
      hasReadingStats = stats->totalSeconds > 0 || stats->sessions > 0;
      statTotalSeconds = stats->totalSeconds;
      statProgress = stats->progress;
      statLastReadEpoch = stats->lastReadEpoch;
    }
```

with

```cpp
    ReadingStatsStore::BookQuery stats;
    if (READING_STATS.queryBook(docId, stats) == ReadingStatsFile::ScanResult::Ok && stats.found) {
      hasReadingStats = stats.book.totalSeconds > 0 || stats.book.sessions > 0;
      statTotalSeconds = stats.book.totalSeconds;
      statProgress = stats.book.progress;
      statLastReadEpoch = stats.book.lastReadEpoch;
    }
```

- [ ] **Step 8: Build everything**

Run: clang-format on the changed files; the full host suite; the firmware build.
Expected: host suite all green except the known `epub_build_inventory` build failure; firmware `[SUCCESS]`.

- [ ] **Step 9: Device check**

Flash; open Home with at least one recent book that has history. Expected: the history line ("Read 3h 18m over 5 days …") and the "~45m" badge as before; the serial log shows no `RST` load lines on Home entry. Open Book Info on a read book: its stats rows as before.

- [ ] **Step 10: Commit**

```bash
git add src/components/BookProgressPresentation.cpp src/activities/home/HomeActivity.h src/activities/home/HomeActivity.cpp src/activities/home/BookInfoActivity.h src/activities/home/BookInfoActivity.cpp src/ReadingStats.cpp test/reading_stats/ReadingStatsStoreTest.cpp
git commit -m "feat(stats): Home and Book Info read the history without loading it

Home prefetches its recent books into the stats cache on entry and the
themes draw from it, so no render() reads the card; Book Info queries
its one book. Until the writes keep it current, every write empties
the cache."
```

---

### Task 6: The stats screens read through the queries

**Files:**
- Modify: `src/activities/settings/ReadingStatsActivity.h`, `.cpp`
- Modify: `src/activities/settings/ReadingStatsBookListActivity.h`, `.cpp`
- Modify: `src/activities/settings/ReadingStatsBookDetailActivity.h`, `.cpp`

**Interfaces:**
- Consumes: `querySummary`, `queryBookAt`, `queryBook`, `BookQuery`, `IndexEntry` (Tasks 1, 4); `currentStreakIn`, `longestStreakIn`, `secondsOn`, `ownSecondsPerPercent`, `etaSeconds`; `removeBookFromFile` (base branch).
- Produces: none for other tasks.

These are activities — not host-testable here; verification is the firmware build plus the device checks in Step 5.

- [ ] **Step 1: Summary screen**

`ReadingStatsActivity.h`: replace the `statsLoad_` member and its comment with

```cpp
  // What the screen draws, read once on entry (and again when the list may have removed books).
  // Written on the loop task under RenderLock; read by render().
  ReadingStatsFile::Summary summary_;
  bool summaryOk_ = false;
  std::vector<BookReadingStats> topBooks_;  // up to three, most time first
  void loadSummary();
```

`ReadingStatsActivity.cpp`:

```cpp
void ReadingStatsActivity::loadSummary() {
  ReadingStatsFile::Summary summary;
  std::vector<BookReadingStats> top;
  const bool ok = READING_STATS.querySummary(summary, /*withIndex=*/true) == ReadingStatsFile::ScanResult::Ok;
  if (ok) {
    const size_t shown = std::min<size_t>(summary.byTime.size(), 3);
    top.reserve(shown);
    for (size_t i = 0; i < shown; ++i) {
      BookReadingStats book;
      if (READING_STATS.queryBookAt(summary.byTime[i].offset, book) == ReadingStatsFile::ScanResult::Ok) {
        book.days.clear();  // the card shows title and time only
        top.push_back(std::move(book));
      }
    }
    std::vector<ReadingStatsFile::IndexEntry>().swap(summary.byTime);
  }
  RenderLock lock(*this);
  summary_ = std::move(summary);
  summaryOk_ = ok;
  topBooks_ = std::move(top);
}
```

- `onEnter()`: replace `statsLoad_.emplace();` and its comment with `loadSummary();`.
- `loop()`: the Confirm condition becomes `summary_.bookCount > 0`; the list's result callback becomes `[this](const ActivityResult&) { loadSummary(); requestUpdate(); }`.
- `render()`: delete `const auto& store = READING_STATS;` and substitute:
  - `store.getGlobalTotalSeconds()` → `summary_.totalSeconds`
  - `store.getGlobalTotalSessions()` → `summary_.totalSessions`
  - `store.getGlobalTotalPagesTurned()` → `summary_.totalPagesTurned`
  - `store.getBookCount()` → `summary_.bookCount`
  - `store.getFinishedBookCount()` → `summary_.finishedBookCount`
  - `store.getGlobalDays()` → `summary_.globalDays`
  - `store.computeCurrentStreak(today)` → `ReadingStatsStore::currentStreakIn(summary_.globalDays, today)`
  - `store.computeLongestStreak()` → `ReadingStatsStore::longestStreakIn(summary_.globalDays, summary_.longestStreak)`
  - `store.getSecondsForDay(d)` → `ReadingStatsStore::secondsOn(summary_.globalDays, d)`
  - The top-books block: delete the `sorted` vector and its sort; the card iterates `topBooks_` (`for (const auto& bk : topBooks_)`, with `bk.` instead of `bk->`); the guard becomes `if (!topBooks_.empty())`.
  - `btn2` becomes `summary_.bookCount == 0 ? "" : tr(STR_READING_STATS_BOOK_LIST)`.

- [ ] **Step 2: All-books list**

`ReadingStatsBookListActivity.h`: replace the `statsLoad_`, `sortedBooks` members, their comments and `rebuildSortedBooks()` with

```cpp
  // The order by time, from one scan (8 bytes a book), and the decoded rows of the page on screen.
  // Rows are decoded on the loop task and swapped in under RenderLock; render() only reads them.
  std::vector<ReadingStatsFile::IndexEntry> index_;
  std::vector<BookReadingStats> rows_;
  int rowsFirst_ = 0;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void rebuildIndex();
  void ensureRowsFor(int index);
  int pageItems() const;
  const BookReadingStats* rowAt(int index) const;
```

`ReadingStatsBookListActivity.cpp` (add `#include "components/themes/ListTouchBand.h"`):

```cpp
void ReadingStatsBookListActivity::rebuildIndex() {
  ReadingStatsFile::Summary summary;
  if (READING_STATS.querySummary(summary, /*withIndex=*/true) != ReadingStatsFile::ScanResult::Ok) {
    summary.byTime.clear();
  }
  RenderLock lock(*this);
  index_ = std::move(summary.byTime);
  rows_.clear();
  rowsFirst_ = 0;
  selectedIndex = std::min(selectedIndex, std::max(0, static_cast<int>(index_.size()) - 1));
}

// The rows BaseTheme::drawList() / LyraTheme::drawList() will draw: pages of
// min(height / rowHeight, ListTouchBand::kMaxRows) rows, the one holding the selection.
int ReadingStatsBookListActivity::pageItems() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  const int contentHeight =
      contentRect.height - (metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2);
  return std::max(1, std::min(contentHeight / metrics.listWithSubtitleRowHeight, ListTouchBand::kMaxRows));
}

void ReadingStatsBookListActivity::ensureRowsFor(const int index) {
  if (index_.empty()) return;
  const int page = pageItems();
  const int first = index / page * page;
  if (first == rowsFirst_ && !rows_.empty()) return;
  const int last = std::min(first + page, static_cast<int>(index_.size()));
  std::vector<BookReadingStats> rows;
  rows.reserve(static_cast<size_t>(last - first));
  for (int i = first; i < last; ++i) {
    BookReadingStats row;
    if (READING_STATS.queryBookAt(index_[i].offset, row) != ReadingStatsFile::ScanResult::Ok) {
      row = BookReadingStats{};
    }
    row.days.clear();  // a row shows title, author, time and the finished mark
    rows.push_back(std::move(row));
  }
  RenderLock lock(*this);
  rows_ = std::move(rows);
  rowsFirst_ = first;
}

const BookReadingStats* ReadingStatsBookListActivity::rowAt(const int index) const {
  const int at = index - rowsFirst_;
  return at >= 0 && at < static_cast<int>(rows_.size()) ? &rows_[static_cast<size_t>(at)] : nullptr;
}
```

- `onEnter()`: `rebuildIndex(); ensureRowsFor(selectedIndex); requestUpdate();`
- `loop()`: use `index_.size()` for the list size; the navigator callbacks become `[this]() { ensureRowsFor(selectedIndex); requestUpdate(); }`; Confirm takes `const BookReadingStats* row = rowAt(selectedIndex); if (row == nullptr || row->docId.empty()) return;` and opens the detail with `row->docId`; the detail's result callback becomes `[this](const ActivityResult&) { rebuildIndex(); ensureRowsFor(selectedIndex); requestUpdate(); }`.
- `selectListRow()`: after `ListRowTap::apply(...)`, call `ensureRowsFor(selectedIndex);` before returning the result.
- `render()`: `sortedBooks.empty()` → `index_.empty()`; item count `static_cast<int>(index_.size())`; the lambdas read `rowAt(index)`: title `row == nullptr ? std::string("…") : (row->title.empty() ? row->docId : row->title)` with the `"✓ "` prefix when `row->finishedCount > 0`; subtitle `row == nullptr ? std::string() : row->author`; value `row == nullptr ? std::string() : formatDuration(row->totalSeconds)`.
- Update the class comment in the header: rows come from the stats file a page at a time.

- [ ] **Step 3: Book stats screen**

`ReadingStatsBookDetailActivity.h`: replace the `statsLoad_` member and its comment with

```cpp
  // The book, read once on entry. render() draws from it.
  ReadingStatsStore::BookQuery query_;
```

`ReadingStatsBookDetailActivity.cpp`:
- `onEnter()` becomes:

```cpp
void ReadingStatsBookDetailActivity::onEnter() {
  Activity::onEnter();
  ReadingStatsStore::BookQuery query;
  if (READING_STATS.queryBook(docId, query) != ReadingStatsFile::ScanResult::Ok) query = {};
  {
    RenderLock lock(*this);
    query_ = std::move(query);
  }
  requestUpdate();
}
```
- `canRemove()`: `if (!query_.found) return false;` instead of the `findBook` line.
- `confirmRemove()`: `const std::string title = query_.book.title.empty() ? docId : query_.book.title;`; in the result handler replace the `RenderLock` block and the `saveToFile()` check with

```cpp
        if (READING_STATS.removeBookFromFile(docId) != ReadingStatsStore::FileRemoval::Removed) {
          LOG_ERR("RST", "remove failed doc=%s", docId.c_str());
          requestUpdate();
          return;
        }
        // Nothing left to show; the list underneath rebuilds when it regains the screen.
        finish();
```

- `render()`: delete `const auto& store = READING_STATS;`; `const BookReadingStats* book = query_.found ? &query_.book : nullptr;`; the ETA becomes

```cpp
        const float own = ReadingStatsStore::ownSecondsPerPercent(book->totalSeconds, book->progress);
        const uint32_t etaSeconds =
            ReadingStatsStore::etaSeconds(own > 0.0f ? own : query_.pooledPace, remainingPercent);
```

- [ ] **Step 4: Build everything**

Run: clang-format on the six files; the full host suite; the firmware build.
Expected: host suite green (known exception only); firmware `[SUCCESS]`.

- [ ] **Step 5: Device check**

Flash; Reading stats → the summary matches the previous numbers (totals, streaks, 30-day chart, top 3); All books → rows, paging with Down across a page boundary, touch selection; open a book → its stats, ETA; Remove a book → the list comes back without it and the summary's totals drop when you go Back. From the reader menu, "Reading stats" for the open book shows it without "Remove".

- [ ] **Step 6: Commit**

```bash
git add src/activities/settings/ReadingStatsActivity.h src/activities/settings/ReadingStatsActivity.cpp src/activities/settings/ReadingStatsBookListActivity.h src/activities/settings/ReadingStatsBookListActivity.cpp src/activities/settings/ReadingStatsBookDetailActivity.h src/activities/settings/ReadingStatsBookDetailActivity.cpp
git commit -m "feat(stats): the stats screens read the history without loading it

The summary reads the global figures and its top three from one scan;
the list keeps the order by time (8 bytes a book) and decodes only the
page on screen; the book screen reads its one book. Removal goes
through the streamed rewrite."
```

---

### Task 7: Streamed writes; the resident store goes

**Files:**
- Modify: `src/ReadingStats.h`, `src/ReadingStats.cpp`
- Modify: `src/ReadingSessionTracker.cpp`
- Modify: `src/activities/settings/ReadingStatsBookDetailActivity.cpp`
- Modify: `src/network/CrossPointWebServer.cpp`
- Modify: `src/JsonSettingsIO.h`, `src/JsonSettingsIO.cpp`, `src/main.cpp`
- Test: `test/reading_stats/ReadingStatsStoreTest.cpp`, `test/reading_stats/ReadingStatsTest.cpp`, `test/reading_stats/CMakeLists.txt`

**Interfaces:**
- Consumes: `scan`, `ScanRequest::wantVictim`, `Summary::targetFirst/victimFirst/victimDocId/hasVictim`, `writeRewrite`, `Rewrite` (Tasks 1–2); `applySession`, `applyFinish`, `takeOut` (Task 3); `scanFile`, cache members (Task 4).
- Produces:
  - `enum class ReadingStatsStore::WriteResult : uint8_t { Done, NotFound, NoMemory, Failed }`
  - `WriteResult recordSession(const std::string& docId, const std::string& title, const std::string& author, uint32_t sessionSeconds, uint32_t sessionPagesTurned, uint8_t progress, time_t walltimeEpoch)`
  - `WriteResult markFinished(const std::string& docId, const std::string& title, const std::string& author, time_t walltimeEpoch)`
  - `WriteResult removeBook(const std::string& docId)`
  - Deleted: `removeBookFromFile`, `FileRemoval`, `findBook`, `getBooks`, all `getGlobal*`/`getBookCount`/`getFinishedBookCount`/`getSecondsForDay`/`compute*`/`avgSecondsPerPercent`/`globalAvgSecondsPerPercent`/`estimateRemainingSeconds`/`getLongestStreakSeen` members, `replaceLoaded`, `saveToFile`, `loadFromFile`, `ensureLoaded`, `release`, `isLoaded`, `ScopedLoad`, the `books` and `totals_` members, `JsonSettingsIO::saveReadingStats` / `loadReadingStats` / `ReadingStatsLoad`.

- [ ] **Step 1: Write the failing tests** — append to `test/reading_stats/ReadingStatsStoreTest.cpp`, and add to its anonymous namespace:

```cpp
constexpr time_t kNoon = 1768046400;
constexpr time_t kDay = 86400;

// A history at the book cap in which "id7" was read longest ago; 60 s each, 6000 s in all.
std::string fullHistory() {
  std::string books;
  for (size_t i = 0; i < ReadingStatsStore::kMaxBooks; ++i) {
    if (i > 0) books += ",";
    const long long lastRead = i == 7 ? 1000 : 2000 + static_cast<long long>(i);
    books += R"({"docId":"id)" + std::to_string(i) + R"(","totalSeconds":60,"lastReadEpoch":)" +
             std::to_string(lastRead) + R"(,"days":[]})";
  }
  return R"({"totalSeconds":6000,"totalSessions":100,"books":[)" + books + "]}";
}
```

The tests:

```cpp
TEST_F(StoreTest, FirstSessionCreatesTheFile) {
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.recordSession("a", "Book A", "X", 600, 10, 20, kNoon), ReadingStatsStore::WriteResult::Done);

  ReadingStatsStore::BookQuery query;
  ASSERT_EQ(store.queryBook("a", query), ScanResult::Ok);
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.totalSeconds, 600u);
  EXPECT_EQ(query.book.title, "Book A");
  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 600u);
}

TEST_F(StoreTest, EmptyFileTakesTheFirstSession) {
  writeFile("");
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.recordSession("a", "Book A", "", 60, 1, 1, kNoon), ReadingStatsStore::WriteResult::Done);

  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 1u);
}

TEST_F(StoreTest, SessionMergesIntoItsBookInPlace) {
  writeFile(kFile);
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.recordSession("b", "B", "", 150, 3, 50, kNoon + kDay), ReadingStatsStore::WriteResult::Done);

  ReadingStatsFile::Summary summary;
  ReadingStatsFile::ScanRequest request;
  request.findDocId = "b";
  request.wantIndex = true;
  FsFile in;
  ASSERT_TRUE(Storage.openFileForRead("T", path_.c_str(), in));
  ASSERT_EQ(ReadingStatsFile::scan(in, summary, request), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 2u);
  EXPECT_EQ(summary.totalSeconds, 1450u);
  EXPECT_EQ(summary.totalSessions, 4u);
  ASSERT_TRUE(summary.found);
  EXPECT_EQ(summary.target.totalSeconds, 450u);
  EXPECT_EQ(summary.target.progress, 50);
  // Still second in the file: updated where it was, not moved to the end.
  EXPECT_GT(summary.targetFirst, readFile().find("\"docId\":\"a\""));
}

TEST_F(StoreTest, ZeroSecondSessionForANewBookWritesNothing) {
  ReadingStatsStore store(path_);

  EXPECT_EQ(store.recordSession("a", "Book A", "", 0, 0, 3, kNoon), ReadingStatsStore::WriteResult::Done);

  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(StoreTest, NewBookAtTheCapEvictsTheLeastRecentlyRead) {
  writeFile(fullHistory());
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.recordSession("new", "New", "", 30, 1, 1, kNoon), ReadingStatsStore::WriteResult::Done);

  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, ReadingStatsStore::kMaxBooks);
  // The evicted book's reading still counts: eviction frees the slot, not the history's totals.
  EXPECT_EQ(summary.totalSeconds, 6030u);
  ReadingStatsStore::BookQuery gone;
  ASSERT_EQ(store.queryBook("id7", gone), ScanResult::Ok);
  EXPECT_FALSE(gone.found);
  ReadingStatsStore::BookQuery added;
  ASSERT_EQ(store.queryBook("new", added), ScanResult::Ok);
  EXPECT_TRUE(added.found);
}

TEST_F(StoreTest, MarkFinishedRespectsTheCap) {
  // The resident store's markFinished() never evicted, so a finish could grow the history past the
  // cap; the shared write path applies it.
  writeFile(fullHistory());
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.markFinished("new", "New", "", kNoon), ReadingStatsStore::WriteResult::Done);

  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, ReadingStatsStore::kMaxBooks);
  ReadingStatsStore::BookQuery gone;
  ASSERT_EQ(store.queryBook("id7", gone), ScanResult::Ok);
  EXPECT_FALSE(gone.found);
}

TEST_F(StoreTest, MarkFinishedCountsAndCreatesTheEntry) {
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.markFinished("a", "Book A", "", kNoon), ReadingStatsStore::WriteResult::Done);

  ReadingStatsStore::BookQuery query;
  ASSERT_EQ(store.queryBook("a", query), ScanResult::Ok);
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.finishedCount, 1);
  EXPECT_EQ(query.book.progress, 100);
}

TEST_F(StoreTest, RemoveTakesTheBookOutOfTheTotals) {
  writeFile(kFile);
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.removeBook("a"), ReadingStatsStore::WriteResult::Done);

  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 300u);
  EXPECT_EQ(store.removeBook("a"), ReadingStatsStore::WriteResult::NotFound);
}

TEST_F(StoreTest, MalformedFileIsSetAsideAndAFreshHistoryStarts) {
  writeFile(R"({"totalSeconds":12,"books":[{"docId":)");
  ReadingStatsStore store(path_);

  ASSERT_EQ(store.recordSession("a", "Book A", "", 60, 1, 1, kNoon), ReadingStatsStore::WriteResult::Done);

  EXPECT_TRUE(std::filesystem::exists(dir_ / "reading-stats.corrupt.json"));
  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 1u);
  EXPECT_EQ(summary.totalSeconds, 60u);
}

TEST_F(StoreTest, AwkwardTitleSurvivesARewrite) {
  ReadingStatsStore store(path_);
  const std::string title = "Say \"hi\" \\ back\nslash";
  ASSERT_EQ(store.recordSession("a", title, "", 60, 1, 1, kNoon), ReadingStatsStore::WriteResult::Done);
  // A control character must not break the file either (it reads back escaped, not decoded).
  ASSERT_EQ(store.recordSession("b", std::string("ctl\x01"), "", 60, 1, 1, kNoon),
            ReadingStatsStore::WriteResult::Done);

  ReadingStatsStore::BookQuery query;
  ASSERT_EQ(store.queryBook("a", query), ScanResult::Ok);
  ASSERT_TRUE(query.found);
  EXPECT_EQ(query.book.title, title);
  ReadingStatsFile::Summary summary;
  ASSERT_EQ(store.querySummary(summary), ScanResult::Ok);
  EXPECT_EQ(summary.bookCount, 2u);
}

TEST_F(StoreTest, WritesKeepTheCacheCurrent) {
  writeFile(kFile);
  ReadingStatsStore store(path_);
  store.prefetchRecent({"a", "b"});

  ASSERT_EQ(store.recordSession("a", "Book A", "X", 200, 2, 30, kNoon), ReadingStatsStore::WriteResult::Done);
  ASSERT_EQ(store.removeBook("b"), ReadingStatsStore::WriteResult::Done);

  ASSERT_NE(store.recent("a"), nullptr);
  EXPECT_EQ(store.recent("a")->totalSeconds, 1200u);
  EXPECT_EQ(store.recent("a")->progress, 30);
  EXPECT_EQ(store.recent("b"), nullptr);
  // Only "a" left, at 30 %: its own pace, 1200 s / 30 % = 40 s per percent.
  EXPECT_FLOAT_EQ(store.recentPooledPace(), 40.0f);
}
```

Delete `TEST_F(StoreTest, ARemovalDropsTheCachedFigures)` (superseded by `WritesKeepTheCacheCurrent`) and the `JsonSettingsIO` stub namespace plus `#include "JsonSettingsIO.h"` at the top of the file.

In `test/reading_stats/ReadingStatsTest.cpp`, delete the seven `ReadingStatsRemoveBook.*` tests (they drive the resident store, which goes; `ReadingStatsFileRemove.*`, `ReadingStatsTakeOut.*` and `StoreTest.RemoveTakesTheBookOutOfTheTotals` cover removal), delete the `JsonSettingsIO` stub namespace and its include. In `test/reading_stats/CMakeLists.txt`, add `${REPO_ROOT}/lib/GfxRenderer` to `ReadingStatsTest`'s include directories if it is not there (ReadingStats.cpp includes BufferedPrint.h).

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build/test --target ReadingStatsStoreTest`
Expected: compile errors — `recordSession` returns `void` (no `WriteResult`), `removeBook` returns `bool`.

- [ ] **Step 3: Replace the store's interface in `src/ReadingStats.h`**

Replace the class body from `static ReadingStatsStore instance;` down to (not including) the `// ---- Streamed queries` block with:

```cpp
  static ReadingStatsStore instance;

 public:
  static ReadingStatsStore& getInstance() { return instance; }

  // The store holds no history. Every query streams the file (ReadingStatsFile) and returns only
  // what was asked for; every update is a streamed rewrite — scan, merge the one book, write a
  // temporary file, read it back, swap it in — so memory does not grow with the number of books.
  // The only resident state is the recent-books cache, which every update keeps current.
  //
  // Deleting a book from the card never touches its history (spec section 5): only removeBook()
  // takes a book out.
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
  // the totals; the persisted longest-streak record stays.
  WriteResult removeBook(const std::string& docId);
```

Keep the rate constants, the static helpers, the caps, the constructor, the query and cache sections. Delete everything listed under *Deleted* in this task's Interfaces, including the `ScopedLoad` class and the `loaded_` member. The private section keeps `scanFile`, `invalidateRecent`, `path_`, `recent_`, `pooledPace_`, `paceKnown_` and gains:

```cpp
  enum class Edit : uint8_t { Session, Finish, Remove };
  WriteResult write(Edit edit, const std::string& docId,
                    const std::function<bool(BookReadingStats& book, ReadingTotals& totals, bool existed)>& apply);
  void rememberRecent(const BookReadingStats& book);
  void forgetRecent(const std::string& docId);
```

(add `#include <functional>`).

- [ ] **Step 4: Implement the writes in `src/ReadingStats.cpp`**

Delete the in-memory `recordSession`, `markFinished`, `removeBook`, `removeBookFromFile`, `findBook`, `getFinishedBookCount`, `getSecondsForDay`, `computeCurrentStreak`, `computeLongestStreak`, `globalAvgSecondsPerPercent`, `avgSecondsPerPercent`, `estimateRemainingSeconds`, `replaceLoaded`, `saveToFile`, `loadFromFile`, `ensureLoaded`, `release`, the `readsBackWithout` helper, and `#include <JsonSettingsIO.h>`. Keep `swapIn`, `asidePathFor`, `parentDirOf`. Add `#include <Arduino.h>  // millis()` (the host shim in test/shims provides it too). `esp_get_free_heap_size()` and `heap_caps_get_largest_free_block()` appear only inside `LOG_INF` arguments: they compile out on host, and on device the deleted `ensureLoaded()` already used them through the same transitive includes, so no further include is needed. Then:

```cpp
ReadingStatsStore::WriteResult ReadingStatsStore::recordSession(
    const std::string& docId, const std::string& title, const std::string& author, const uint32_t sessionSeconds,
    const uint32_t sessionPagesTurned, const uint8_t progress, const time_t walltimeEpoch) {
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
  if (docId.empty()) return WriteResult::NotFound;
  const uint32_t started = millis();

  // 1. Scan: the header, the book, and the cap's victim should the book be new.
  ReadingStatsFile::ScanRequest request;
  request.findDocId = docId;
  request.wantVictim = edit != Edit::Remove;
  ReadingStatsFile::Summary summary;
  bool haveInput = Storage.exists(path_.c_str());
  auto result = scanFile(request, summary);
  if (result == ReadingStatsFile::ScanResult::NoMemory) return WriteResult::NoMemory;
  if (result == ReadingStatsFile::ScanResult::IoError) return WriteResult::Failed;
  // A removal has nothing to remove from an unreadable file; only a write that adds reading starts
  // a fresh history over it.
  if (result == ReadingStatsFile::ScanResult::Malformed && edit == Edit::Remove) return WriteResult::Failed;
  if (result == ReadingStatsFile::ScanResult::Malformed) {
    // Permanent: set it aside for forensics rather than lose it or stall on it for ever, and start
    // a fresh history with this change.
    const std::string aside = asidePathFor(path_);
    Storage.remove(aside.c_str());
    if (!Storage.rename(path_.c_str(), aside.c_str())) {
      LOG_ERR("RST", "History file unreadable and could not be set aside; left as is");
      return WriteResult::Failed;
    }
    LOG_ERR("RST", "History file unreadable; set aside as %s, starting a fresh history", aside.c_str());
    summary = ReadingStatsFile::Summary{};
    haveInput = false;
  }
  if (!summary.found && edit == Edit::Remove) return WriteResult::NotFound;

  // 2. Merge the one book in memory.
  BookReadingStats book = summary.found ? summary.target : BookReadingStats{};
  if (!summary.found) book.docId = docId;
  ReadingStatsFile::Rewrite rewrite;
  rewrite.totals = summary;  // the ReadingTotals part
  if (!apply(book, rewrite.totals, summary.found)) return WriteResult::Done;
  uint32_t expectedBooks = summary.bookCount;
  std::string evicted;
  if (edit == Edit::Remove) {
    rewrite.dropAt = summary.targetFirst;
    --expectedBooks;
  } else if (summary.found) {
    rewrite.replaceAt = summary.targetFirst;
    rewrite.replacement = &book;
  } else {
    rewrite.append = &book;
    ++expectedBooks;
    if (summary.bookCount >= kMaxBooks && summary.hasVictim) {
      rewrite.dropAt = summary.victimFirst;
      evicted = summary.victimDocId;
      --expectedBooks;
      LOG_INF("RST", "Book cap (%u) reached; dropping the least recently read: %s", static_cast<unsigned>(kMaxBooks),
              evicted.c_str());
    }
  }

  // 3. Write the temporary file.
  Storage.mkdir(parentDirOf(path_).c_str());
  const std::string tmpPath = path_ + ".tmp";
  bool written = false;
  {
    FsFile in;
    const bool inOpen = haveInput && Storage.openFileForRead("RST", path_.c_str(), in) && in.size() > 0;
    FsFile out;
    if (!Storage.openFileForWrite("RST", tmpPath.c_str(), out)) return WriteResult::Failed;
    BufferedPrint buffered(out, 1024);
    const auto copied = ReadingStatsFile::writeRewrite(inOpen ? &in : nullptr, rewrite, buffered);
    written = buffered.flushBuffer() && copied == ReadingStatsFile::ScanResult::Ok;
  }

  // 4. Read it back; swap it in only if it holds what it should.
  ReadingStatsFile::Summary check;
  if (written) {
    ReadingStatsFile::ScanRequest verify;
    verify.findDocId = docId;
    FsFile back;
    written = Storage.openFileForRead("RST", tmpPath.c_str(), back) &&
              ReadingStatsFile::scan(back, check, verify) == ReadingStatsFile::ScanResult::Ok &&
              check.bookCount == expectedBooks &&
              (edit == Edit::Remove ? !check.found : (check.found && check.target.totalSeconds == book.totalSeconds));
  }
  if (!written) {
    LOG_ERR("RST", "Rewritten history did not read back; left as is");
    Storage.remove(tmpPath.c_str());
    return WriteResult::Failed;
  }
  if (!swapIn(path_, tmpPath)) return WriteResult::Failed;

  // The cache follows the file without a scan of its own.
  if (edit == Edit::Remove) {
    forgetRecent(docId);
  } else {
    rememberRecent(book);
  }
  if (!evicted.empty()) forgetRecent(evicted);
  pooledPace_ = pooledSecondsPerPercent(check.totalSeconds, check.paceSeconds, check.pacePercents);
  paceKnown_ = true;
  LOG_INF("RST", "write done in %lu ms (%u books, free=%lu contig=%lu)", static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(expectedBooks), static_cast<unsigned long>(esp_get_free_heap_size()),
          static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT)));
  return WriteResult::Done;
}

void ReadingStatsStore::rememberRecent(const BookReadingStats& book) {
  ReadingStatsFile::RecentSnapshot snapshot;
  snapshot.docId = book.docId;
  snapshot.known = true;
  snapshot.totalSeconds = book.totalSeconds;
  snapshot.knownDays = static_cast<uint16_t>(std::min(book.days.size(), kMaxBookDays));
  snapshot.lastReadEpoch = book.lastReadEpoch;
  snapshot.progress = book.progress;
  forgetRecent(book.docId);
  // The book just read is the likeliest one on Home: it goes to the front, the oldest entry goes.
  recent_.insert(recent_.begin(), std::move(snapshot));
  if (recent_.size() > kRecentCacheSize) recent_.pop_back();
}

void ReadingStatsStore::forgetRecent(const std::string& docId) {
  recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                               [&docId](const ReadingStatsFile::RecentSnapshot& s) { return s.docId == docId; }),
                recent_.end());
}
```

`prefetchRecent()`'s trimming keeps working with the front insertion (it drops entries not asked for). Remove the transitional `invalidateRecent()` calls added in Task 5 (the function stays for nothing else — delete it too if unused).

- [ ] **Step 5: Callers**

`src/ReadingSessionTracker.cpp` — `markFinished()`:

```cpp
void ReadingSessionTracker::markFinished() {
  if (!active) return;
  const int64_t walltime = HalClock::isSynced() ? static_cast<int64_t>(HalClock::now()) : 0;
  const auto result = READING_STATS.markFinished(docId, title, author, static_cast<time_t>(walltime));
  if (result != ReadingStatsStore::WriteResult::Done) {
    LOG_ERR("RST", "markFinished not recorded (%u) doc=%s wall=%lld", static_cast<unsigned>(result), docId.c_str(),
            (long long)walltime);
  }
  LOG_DBG("RST", "Marked finished doc=%s wall=%lld", docId.c_str(), (long long)walltime);
}
```

and in `end()` replace the `ScopedLoad` block with

```cpp
  if (!docId.empty()) {
    const auto result = READING_STATS.recordSession(docId, title, author, seconds, pagesTurnedThisSession,
                                                    lastKnownProgress, static_cast<time_t>(walltime));
    if (result != ReadingStatsStore::WriteResult::Done) {
      LOG_ERR("RST", "Session not recorded (%u) doc=%s secs=%u pages=%u wall=%lld", static_cast<unsigned>(result),
              docId.c_str(), seconds, pagesTurnedThisSession, (long long)walltime);
    }
  }
```

`src/activities/settings/ReadingStatsBookDetailActivity.cpp` — in `confirmRemove()`'s handler: `if (READING_STATS.removeBook(docId) != ReadingStatsStore::WriteResult::Done) {`.

`src/network/CrossPointWebServer.cpp` — `handleStatsRemove()`'s switch:

```cpp
  switch (READING_STATS.removeBook(docId)) {
    case ReadingStatsStore::WriteResult::Done:
      LOG_DBG("WEB", "Removed from reading stats: %s", docId.c_str());
      server->send(200, "application/json", "{\"ok\":true}");
      return;
    case ReadingStatsStore::WriteResult::NotFound:
      server->send(404, "application/json", "{\"error\":\"Book not found\"}");
      return;
    case ReadingStatsStore::WriteResult::NoMemory:
    case ReadingStatsStore::WriteResult::Failed:
      server->send(500, "application/json", "{\"error\":\"Could not update the reading stats\"}");
      return;
  }
```

`src/JsonSettingsIO.h` — delete `class ReadingStatsStore;`, the `// ReadingStatsStore` block (`saveReadingStats`, `ReadingStatsLoad`, `loadReadingStats`). `src/JsonSettingsIO.cpp` — delete the `// ---- ReadingStatsStore ----` section and `#include "ReadingStats.h"`.

`src/main.cpp` — replace the comment block that starts `// READING_STATS is deliberately NOT loaded here.` with

```cpp
  // READING_STATS holds no history: every consumer streams the file (ReadingStatsStore), so there
  // is nothing to load here. Keeping it resident used to cost, measured on X4 with 36 books,
  // ~15 KB of heap and largest8 65524 -> 26612 before the first book was even opened.
```

Grep for leftovers: `grep -rn "ScopedLoad\|ensureLoaded\|findBook\|getBooks()\|removeBookFromFile\|saveReadingStats\|loadReadingStats" src` — expected: no matches.

- [ ] **Step 6: Run the tests**

Run: clang-format on every changed file; `cmake -S test -B build/test && cmake --build build/test --target ReadingStatsTest --target ReadingStatsStoreTest && ./build/test/reading_stats/ReadingStatsStoreTest.exe && ./build/test/reading_stats/ReadingStatsTest.exe`
Expected: `ReadingStatsStoreTest`: `[  PASSED  ] 18 tests.`; `ReadingStatsTest`: `[  PASSED  ] 34 tests.` Then the full host suite (known exception only).

- [ ] **Step 7: Firmware build** — Expected: `[SUCCESS]`. Note the flash figure for Task 8.

- [ ] **Step 8: Device check**

Flash; read a book for a minute; exit. Expected log: `RST write done in … ms (N books, free=… contig=…)`, no `Session not recorded`. Home shows the new time at once. Mark a book finished from the reader menu: `write done`. Remove a book on the device and on the web: both `write done`; the dashboard and the device agree.

- [ ] **Step 9: Commit**

```bash
git add -A src/ReadingStats.h src/ReadingStats.cpp src/ReadingSessionTracker.cpp src/activities/settings/ReadingStatsBookDetailActivity.cpp src/network/CrossPointWebServer.cpp src/JsonSettingsIO.h src/JsonSettingsIO.cpp src/main.cpp test/reading_stats
git commit -m "feat(stats): streamed writes; the store no longer holds the history

recordSession(), markFinished() and removeBook() scan the file, merge
the one book, write a temporary file, read it back and swap it in, so
the session end needs a few KB whatever the history's size instead of
~1.5 KB a book. markFinished() now respects the 100-book cap. A
malformed file is set aside and a fresh history started, as the loader
did. Every write keeps the recent-books cache current.

Gone: the resident vectors, ScopedLoad and the whole-file JSON load
and save."
```

---

### Task 8: Measure on the device and settle the cap

**Files:**
- Create: `scripts/gen_reading_stats_history.py`
- Modify: `docs/superpowers/specs/2026-09-28-reading-stats-streamed-store-design.md` (record the numbers, settle the cap question)

**Interfaces:**
- Consumes: the `RST write done in … ms` log line (Task 7).
- Produces: measured numbers; a cap decision (the constant may change in a follow-up commit).

- [ ] **Step 1: Write the generator**

```python
#!/usr/bin/env python3
"""Writes a synthetic reading-stats.json in the device's format, for measuring the streamed store.

    python scripts/gen_reading_stats_history.py --books 100 --days 60 --out reading-stats.json

Copy the result to the card as /.crosspoint/reading-stats.json (back up the real one first).
"""
import argparse
import json

BASE_DAY = 20000          # a day index well inside the uint16 range
BASE_EPOCH = 1728000000   # any plausible epoch; only ordering matters


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--books", type=int, default=100)
    ap.add_argument("--days", type=int, default=60)
    ap.add_argument("--out", default="reading-stats.json")
    args = ap.parse_args()

    books, global_days = [], {}
    for i in range(args.books):
        days = []
        for d in range(args.days):
            day = BASE_DAY + i * 3 + d
            days.append([day, 600])
            global_days[day] = global_days.get(day, 0) + 600
        books.append({
            "docId": f"{i:032x}", "title": f"Synthetic book number {i} with a title of typical length",
            "author": "Test Author", "totalSeconds": 600 * args.days, "pagesTurned": 20 * args.days,
            "sessions": args.days, "firstReadEpoch": BASE_EPOCH + i * 86400,
            "lastReadEpoch": BASE_EPOCH + (i + args.days) * 86400, "progress": (i * 7) % 100,
            "finishedCount": 0, "lastFinishedEpoch": 0, "finished": False, "days": days,
        })
    doc = {
        "totalSeconds": sum(b["totalSeconds"] for b in books),
        "totalSessions": sum(b["sessions"] for b in books),
        "totalPagesTurned": sum(b["pagesTurned"] for b in books),
        "longestStreak": 1,
        "globalDays": sorted([d, s] for d, s in global_days.items())[-400:],
        "books": books,
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, separators=(",", ":"), ensure_ascii=False)
    print(f"wrote {args.out}: {args.books} books x {args.days} days")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Measure**

Generate `--books 100 --days 60` (worst case) and `--books 100 --days 12` (typical); for each, copy to the card, boot, and record from the serial log (COM6):
1. Reader exit after a one-minute session: `RST write done in … ms`, `free`, `contig`.
2. First Home entry after boot (prefetch): add a temporary `LOG_INF` with `millis()` around `prefetchRecent()` if needed.
3. Reading stats summary, All books (first page and a page turn), a book's screen: wall time by eye or log.
4. Web: `/api/stats` load and one removal.
Compare with the same files on the base branch: reader exit there should log a `NoMemory` load (session lost) on the worst case.

- [ ] **Step 3: Record and decide**

Add a "Measured" subsection to the spec's section 6 with the numbers and the firmware flash delta against `feat/stats-remove-book`. Settle the open question: keep `kMaxBooks = 100`, or raise it to the largest value whose worst-case reader exit stays under the budget (0.5 s unless decided otherwise); if raising needs the room, file the 30-day per-book bucket change as its own follow-up.

- [ ] **Step 4: Commit**

```bash
git add scripts/gen_reading_stats_history.py docs/superpowers/specs/2026-09-28-reading-stats-streamed-store-design.md
git commit -m "docs(stats): measured the streamed store on the device

Synthetic 100-book histories (typical and worst case): reader exit,
Home, the stats screens and the web, against the base branch."
```
