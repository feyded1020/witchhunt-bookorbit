#include "ReadingSessionTracker.h"

#include <Arduino.h>  // millis()
#include <HalClock.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>

#include "ReadingStats.h"

ReadingSessionTracker& globalReadingSessionTracker() {
  static ReadingSessionTracker instance;
  return instance;
}

void ReadingSessionTracker::flushIdleSinceLastActivity() {
  if (!active) return;
  const uint32_t now = millis();
  const uint32_t delta = now - lastActivityMs;  // unsigned wrap is fine
  // Cap idle gaps. A user idle for an hour shouldn't get credited for it.
  const uint32_t capped = delta > MAX_IDLE_MS ? MAX_IDLE_MS : delta;
  accumulatedMs += capped;
  lastActivityMs = now;
}

void ReadingSessionTracker::begin(const std::string& docId_, const std::string& title_, const std::string& author_) {
  if (active) {
    // Don't lose data from a previous session that wasn't explicitly closed.
    end();
  }
  active = true;
  docId = docId_;
  title = title_;
  author = author_;
  walltimeStartEpoch = HalClock::isSynced() ? static_cast<int64_t>(HalClock::now()) : 0;
  lastActivityMs = millis();
  accumulatedMs = 0;
  pagesTurnedThisSession = 0;
  lastKnownProgress = 0;
  LOG_DBG("RST", "Session begin doc=%s sync=%d", docId.c_str(), HalClock::isSynced() ? 1 : 0);
}

void ReadingSessionTracker::onPageTurn() {
  if (!active) return;
  if (!bookOrbitStateDir.empty()) {
    captureBookOrbitEvent(millis() - lastActivityMs);
  }
  flushIdleSinceLastActivity();
  pagesTurnedThisSession += 1;
}

void ReadingSessionTracker::enableBookOrbitEvents(const std::string& stateDir) {
  if (!active) return;
  bookOrbitStateDir = stateDir;
  preciseProgress = -1.0f;
  pendingBookOrbitEvents.clear();
}

void ReadingSessionTracker::updatePreciseProgress(const float fraction) {
  if (!active) return;
  preciseProgress = std::min(1.0f, std::max(0.0f, fraction));
}

// Ported from CrossInk-Bookorbit's EpubReaderActivity::capturePageStatEvent (MIT).
void ReadingSessionTracker::captureBookOrbitEvent(const uint32_t dwellMs) {
  const uint32_t dwellSeconds = dwellMs / 1000;
  // A page left open past the idle cap was not being read: drop it rather than clamp it,
  // since BookOrbit derives reading pace from these dwell times. Sub-second turns are skims.
  if (dwellSeconds == 0 || dwellMs > MAX_IDLE_MS || preciseProgress < 0.0f) {
    return;
  }
  // Hard bound on session RAM (16 bytes per event); the SD queue applies the same cap.
  if (pendingBookOrbitEvents.size() >= BookOrbitStatsQueue::MAX_QUEUED_EVENTS) {
    return;
  }

  uint32_t nowEpoch = 0;
  bool approximate = true;
  BookOrbitStatsQueue::captureNow(nowEpoch, approximate);
  if (nowEpoch == 0) {
    return;  // no usable clock at all; nothing for the upload-time correction to anchor on
  }

  BookOrbitStatEvent event;
  event.startTime = nowEpoch > dwellSeconds ? nowEpoch - dwellSeconds : nowEpoch;
  event.durationSeconds = dwellSeconds;
  // Position of the page just finished: updatePreciseProgress() is called when a page is
  // saved, so at turn time it still describes the page being left.
  event.page = static_cast<uint16_t>(preciseProgress * BookOrbitStatsQueue::PROGRESS_SCALE + 0.5f);
  event.totalPages = BookOrbitStatsQueue::PROGRESS_SCALE;
  event.era = static_cast<uint16_t>(WallClock::era());
  event.flags = approximate ? BookOrbitStatEvent::FLAG_CLOCK_APPROXIMATE : 0;

  if (pendingBookOrbitEvents.size() == pendingBookOrbitEvents.capacity()) {
    pendingBookOrbitEvents.reserve(pendingBookOrbitEvents.size() + 64);
  }
  pendingBookOrbitEvents.push_back(event);
}

void ReadingSessionTracker::updateProgress(uint8_t progress) {
  if (!active) return;
  lastKnownProgress = progress;
}

void ReadingSessionTracker::markFinished() {
  if (!active) return;
  const int64_t walltime = HalClock::isSynced() ? static_cast<int64_t>(HalClock::now()) : 0;
  // Book exit is the only moment the history has to be in RAM; pull it in, merge, write, drop.
  const ReadingStatsStore::ScopedLoad statsLoad;
  READING_STATS.markFinished(docId, title, author, static_cast<time_t>(walltime));
  if (!READING_STATS.saveToFile()) {
    LOG_ERR("RST", "saveToFile failed (markFinished) doc=%s title=%s author=%s wall=%lld", docId.c_str(), title.c_str(),
            author.c_str(), (long long)walltime);
  }
  LOG_DBG("RST", "Marked finished doc=%s wall=%lld", docId.c_str(), (long long)walltime);
}

void ReadingSessionTracker::end() {
  if (!active) return;
  // Final idle flush so we credit the time between the last page turn and now,
  // capped at MAX_IDLE_MS just like all the other gaps.
  flushIdleSinceLastActivity();

  const uint32_t seconds = static_cast<uint32_t>(accumulatedMs / 1000);
  // Prefer the walltime captured at begin(); if HalClock has only just become
  // synced, fall back to "now" as the lastReadEpoch.
  int64_t walltime = walltimeStartEpoch;
  if (walltime == 0 && HalClock::isSynced()) {
    walltime = static_cast<int64_t>(HalClock::now());
  }

  LOG_DBG("RST", "Session end doc=%s secs=%u pages=%u prog=%u wall=%lld", docId.c_str(), seconds,
          pagesTurnedThisSession, lastKnownProgress, (long long)walltime);

  if (!docId.empty()) {
    // See markFinished(): the store is loaded only for this merge-and-save, then released.
    const ReadingStatsStore::ScopedLoad statsLoad;
    READING_STATS.recordSession(docId, title, author, seconds, pagesTurnedThisSession, lastKnownProgress,
                                static_cast<time_t>(walltime));
    if (!READING_STATS.saveToFile()) {
      LOG_ERR("RST", "saveToFile failed (session end) doc=%s title=%s author=%s secs=%u pages=%u wall=%lld",
              docId.c_str(), title.c_str(), author.c_str(), seconds, pagesTurnedThisSession, (long long)walltime);
    }
  }

  // Flush the session's BookOrbit events in one batch (never once per page turn).
  if (!bookOrbitStateDir.empty()) {
    if (!pendingBookOrbitEvents.empty()) {
      BookOrbitStatsQueue::appendBatch(bookOrbitStateDir, pendingBookOrbitEvents);
    }
    // Reading sessions are the natural "last known good time" checkpoints for the
    // boot-time clock restore.
    WallClock::checkpoint();
  }
  bookOrbitStateDir.clear();
  preciseProgress = -1.0f;
  pendingBookOrbitEvents.clear();
  pendingBookOrbitEvents.shrink_to_fit();

  active = false;
  docId.clear();
  title.clear();
  author.clear();
  walltimeStartEpoch = 0;
  lastActivityMs = 0;
  accumulatedMs = 0;
  pagesTurnedThisSession = 0;
  lastKnownProgress = 0;
}

uint32_t ReadingSessionTracker::getLiveSeconds() const {
  if (!active) return 0;
  // Best-effort live readout — does not mutate state, so it does not include
  // the in-flight idle gap. Good enough for "you've been reading for X".
  return static_cast<uint32_t>(accumulatedMs / 1000);
}
