#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// The reading history's plain data, shared by the store (ReadingStats.h), the slot file
// (ReadingStatsSlotFile.h) and the JSON codec (ReadingStatsJson.h) without any of them including
// another's.

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
