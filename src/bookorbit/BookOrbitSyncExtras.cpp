#include "BookOrbitSyncExtras.h"

#include <BookOrbitStatsQueue.h>
#include <BookOrbitSyncClient.h>
#include <I18n.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>
#include <ctime>
#include <vector>

#include "BookOrbitBookState.h"


namespace BookOrbitExtras {

namespace {
// Drains the book's queued reading-session events (see ReadingSessionTracker) to BookOrbit's
// page-stats endpoint. Ported from CrossInk-Bookorbit's uploadQueuedStats (MIT).
size_t uploadQueuedStats(const std::string& stateDir, const std::string& documentHash) {
  // Drained one batch at a time: a full queue is 32 KB, too much to hold beside TLS.
  const size_t total = std::min(BookOrbitStatsQueue::queuedCount(stateDir), BookOrbitStatsQueue::MAX_QUEUED_EVENTS);
  if (total == 0) {
    LOG_INF("BookOrbit", "No queued reading stats for this book");
    return 0;
  }
  LOG_INF("BookOrbit", "Draining %u queued events (era now %u)", (unsigned)total, (unsigned)WallClock::era());

  // System-clock stamps are re-resolved against NTP: each event gets the correction WallClock
  // measured for ITS era (see WallClock::correctionForEvent). Events from eras whose error was
  // never measured keep their approximate stamp unless outright implausible.
  uint32_t syncInstant = 0;
  if (!WallClock::now(syncInstant)) syncInstant = 0;

  // ~2 KB of JSON per request: small enough to build beside the kept TLS session.
  constexpr size_t BATCH_SIZE = 25;
  std::vector<BookOrbitStatEvent> batch;
  uint32_t previousStart = 0;  // carried across batches: the queue is chronological
  size_t dropped = 0;
  size_t uploaded = 0;

  for (size_t offset = 0; offset < total; offset += BATCH_SIZE) {
    if (!BookOrbitStatsQueue::readRange(stateDir, offset, BATCH_SIZE, batch)) {
      LOG_ERR("BookOrbit", "Failed to read queued stats at event %u; keeping the queue", (unsigned)offset);
      return uploaded;
    }
    if (batch.empty()) {
      // Truncated queue: clearing it below is the self-heal.
      LOG_ERR("BookOrbit", "Stats queue ended early at event %u of %u", (unsigned)offset, (unsigned)total);
      break;
    }

    size_t droppedInBatch = 0;
    for (auto& event : batch) {
      if (event.flags & BookOrbitStatEvent::FLAG_CLOCK_APPROXIMATE) {
        int64_t delta = 0;
        if (WallClock::correctionForEvent(event.era, event.startTime, delta)) {
          int64_t corrected = static_cast<int64_t>(event.startTime) + delta;
          // Nothing queued can postdate this sync, and the queue stays chronological.
          if (syncInstant != 0 && corrected > static_cast<int64_t>(syncInstant)) corrected = syncInstant;
          if (corrected < static_cast<int64_t>(previousStart)) corrected = previousStart;
          if (corrected > 0 && corrected < static_cast<int64_t>(WallClock::MAX_PLAUSIBLE_EPOCH)) {
            event.startTime = static_cast<uint32_t>(corrected);
          }
        } else if (event.startTime < WallClock::MIN_PLAUSIBLE_EPOCH) {
          event.durationSeconds = 0;  // unresolvable: dropped below
          droppedInBatch++;
        }
      }
      previousStart = event.startTime;
    }
    if (droppedInBatch > 0) {
      batch.erase(std::remove_if(batch.begin(), batch.end(),
                                 [](const BookOrbitStatEvent& e) { return e.durationSeconds == 0; }),
                  batch.end());
      dropped += droppedInBatch;
    }
    if (batch.empty()) continue;

    const auto result = BookOrbitSyncClient::uploadPageStats(documentHash, BookOrbitSyncClient::DEVICE_MODEL,
                                                             batch.data(), batch.size());
    if (result != BookOrbitSyncClient::OK) {
      const int httpCode = BookOrbitSyncClient::lastHttpCode;
      if (httpCode == 404 || httpCode == 405 || httpCode == 501) {
        // This server predates the page-stats endpoint: drop the queue rather than re-firing a
        // doomed upload every sync. Updating the server starts buffering fresh.
        LOG_INF("BookOrbit", "Server has no page-stats endpoint (http=%d); discarding queued stats", httpCode);
        BookOrbitStatsQueue::clear(stateDir);
        return uploaded;
      }
      // Transient: keep the whole queue. Re-sending an accepted batch is harmless.
      LOG_ERR("BookOrbit", "Stats upload failed after %u/%u events (http=%d)", (unsigned)uploaded, (unsigned)total,
              httpCode);
      return uploaded;
    }
    uploaded += batch.size();
  }

  if (dropped > 0) {
    LOG_ERR("BookOrbit", "Dropped %u stat events with unresolvable timestamps", (unsigned)dropped);
  }
  LOG_INF("BookOrbit", "Uploaded %u reading-session events", (unsigned)uploaded);
  BookOrbitStatsQueue::clear(stateDir);
  return uploaded;
}

// Records the sweep. The server treats a device with no recent sweep as a plain KOReader
// install and fabricates estimated sessions from progress pushes, duplicating the measured
// ones; the sweep suppresses that. Recorded before any progress push for the same reason.
void recordSweep(const Summary& summary) {
  const auto result =
      BookOrbitSyncClient::completeSweep(BookOrbitSyncClient::DEVICE_MODEL, summary.documentUnmatched ? 0 : 1,
                                         static_cast<uint32_t>(summary.statsAccepted), summary.annotationsSent);
  if (result == BookOrbitSyncClient::OK) return;
  const int httpCode = BookOrbitSyncClient::lastHttpCode;
  if (httpCode == 404 || httpCode == 405 || httpCode == 501) {
    LOG_INF("BookOrbit", "Server has no sweeps endpoint (http=%d); skipping sweep record", httpCode);
  } else {
    LOG_ERR("BookOrbit", "Sweep record failed (result=%d, http=%d)", static_cast<int>(result), httpCode);
  }
}
}  // namespace

Summary run(const std::string& epubPath, const std::string& documentHash,
            const std::function<void(const char*)>& status) {
  Summary summary;
  const std::string stateDir = BookOrbitBookState::dirFor(epubPath);
  if (stateDir.empty()) {
    LOG_ERR("BookOrbit", "No state dir for %s; skipping stats/highlights/bookmarks", epubPath.c_str());
    recordSweep(summary);
    return summary;
  }

  status(tr(STR_SYNCING_READING_SESSIONS));
  summary.statsAccepted = uploadQueuedStats(stateDir, documentHash);

  recordSweep(summary);
  return summary;
}

}  // namespace BookOrbitExtras
