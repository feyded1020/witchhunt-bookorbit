#include "BookOrbitSyncExtras.h"

#include <BookOrbitAnnotations.h>
#include <BookOrbitBookmarkStore.h>
#include <BookOrbitBookmarks.h>
#include <BookOrbitStatsQueue.h>
#include <BookOrbitSyncClient.h>
#include <ChapterXPathIndexer.h>
#include <Epub.h>
#include <I18n.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>
#include <ctime>
#include <vector>

#include "BookOrbitBookState.h"
#include "BookmarkStore.h"
#include "GlobalBookmarkIndex.h"


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

// Loaded on first use and shared by the bookmark and highlight phases: both need the book's
// structure to turn positions into KOReader xpointers and back.
class LazyEpub {
 public:
  explicit LazyEpub(const std::string& path) : path_(path) {}
  const std::shared_ptr<Epub>& get() {
    if (!tried_) {
      tried_ = true;
      auto epub = std::make_shared<Epub>(path_, "/.crosspoint");
      if (epub->load(true, true)) {
        epub->setupCacheDir();
        epub_ = std::move(epub);
      } else {
        LOG_ERR("BookOrbit", "Could not load %s for position mapping", path_.c_str());
      }
    }
    return epub_;
  }

 private:
  std::string path_;
  bool tried_ = false;
  std::shared_ptr<Epub> epub_;
};

// Past this many local bookmarks the full key set is not sent, which only means deletions
// do not propagate that sync (additions still do). Keeps the request body small.
constexpr size_t MAX_KEYS_PER_SYNC = 64;

// A 404 from the bookmark route means the server predates bookmark sync; ask once per boot.
bool s_bookmarkRouteUnsupported = false;

bool keyOfRecord(const BookOrbitBookmarkRecord& record, char (&outKey)[BookOrbitAnnotationKey::DIGEST_SIZE],
                 char (&outDatetime)[20]) {
  return bookOrbitFormatDatetime(record.identityEpoch, outDatetime) &&
         bookOrbitAnnotationKey(outDatetime, record.pos.c_str(), outKey);
}

std::string chapterTitleFor(Epub& epub, int spineIndex) {
  const int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
  return tocIndex >= 0 ? epub.getTocItem(tocIndex).title : std::string();
}

// Two-way bookmark exchange, adapted from CrossInk-Bookorbit's prepare/upload/applyIncoming
// bookmark steps (MIT) to Witch Hunt's page-based BookmarkStore. Identity is md5(datetime|pos)
// where datetime is the bookmark's creation timestamp and pos an xpointer minted once, from the
// bookmark's layout-independent chapter position, and never recomputed.
void syncBookmarks(const std::string& stateDir, const std::string& documentHash, LazyEpub& lazyEpub,
                   Summary& summary) {
  if (s_bookmarkRouteUnsupported) return;
  const std::shared_ptr<Epub>& epub = lazyEpub.get();
  if (!epub) return;

  BookmarkStore store;
  store.load(epub->getCachePath());

  std::vector<BookOrbitBookmarkRecord> records;
  // Only a readable record file proves this book synced bookmarks before; without one, an empty
  // key set would read to the server as "every bookmark was deleted here".
  const bool syncedHereBefore = BookOrbitBookmarkStore::readAll(stateDir, records);

  // Mint records for bookmarks that have an identity and a position but no record yet. A
  // bookmark whose position was only settled after later uploads moved the watermark past its
  // creation time takes an identity above the watermark, or it would never be offered.
  const uint32_t watermark = BookOrbitBookmarkStore::readWatermark(stateDir);
  uint32_t nextLateIdentity = watermark + 1;
  size_t syncableCount = 0;
  std::vector<uint32_t> liveTimestamps;
  liveTimestamps.reserve(store.getAll().size());
  for (const Bookmark& bm : store.getAll()) {
    if (bm.timestamp == 0 || bm.progressQ == Bookmark::PROGRESS_UNKNOWN) continue;
    syncableCount++;
    liveTimestamps.push_back(bm.timestamp);
    const bool known = std::any_of(records.begin(), records.end(), [&](const BookOrbitBookmarkRecord& r) {
      return r.timestamp == bm.timestamp;
    });
    if (known) continue;
    BookOrbitBookmarkRecord record;
    record.timestamp = bm.timestamp;
    record.identityEpoch = bm.timestamp > watermark ? bm.timestamp : nextLateIdentity++;
    record.spineIndex = bm.spineIndex;
    record.pos = ChapterXPathIndexer::findXPathForProgress(epub, bm.spineIndex, bm.progressQ / 10000.0f);
    if (record.pos.empty()) continue;  // retried next sync
    if (BookOrbitBookmarkStore::put(stateDir, record)) records.push_back(std::move(record));
  }
  // Drop records whose bookmark was deleted here: the key set then tells the server.
  if (BookOrbitBookmarkStore::retain(stateDir, liveTimestamps)) {
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [&](const BookOrbitBookmarkRecord& r) {
                                   return std::find(liveTimestamps.begin(), liveTimestamps.end(), r.timestamp) ==
                                          liveTimestamps.end();
                                 }),
                  records.end());
  }

  // Outgoing: records newer than the upload watermark.
  std::vector<BookOrbitBookmark> outgoing;
  uint32_t outgoingWatermark = 0;
  for (const BookOrbitBookmarkRecord& record : records) {
    if (outgoing.size() >= BOOKORBIT_BOOKMARK_BATCH) break;
    if (record.identityEpoch <= watermark) continue;
    BookOrbitBookmark entry;
    if (!bookOrbitFormatDatetime(record.identityEpoch, entry.datetime)) continue;
    entry.pos = record.pos;
    entry.chapter = chapterTitleFor(*epub, record.spineIndex);
    outgoing.push_back(std::move(entry));
    outgoingWatermark = std::max(outgoingWatermark, record.identityEpoch);
  }

  // The complete key set (deletion propagation), only once this book has sync history and
  // every bookmark carries a position.
  std::vector<BookOrbitAnnotationKey> keys;
  bool keysComplete = false;
  if (!syncedHereBefore) {
    LOG_INF("BookOrbit", "No bookmark sync history for this book; deletions will not propagate this sync");
  } else if (records.size() <= MAX_KEYS_PER_SYNC && records.size() >= syncableCount) {
    keys.reserve(records.size());
    keysComplete = true;
    for (const BookOrbitBookmarkRecord& record : records) {
      BookOrbitAnnotationKey key;
      if (!bookOrbitFormatDatetime(record.identityEpoch, key.dt) ||
          !bookOrbitAnnotationKey(key.dt, record.pos.c_str(), key.k)) {
        keysComplete = false;
        break;
      }
      keys.push_back(key);
    }
    if (!keysComplete) keys.clear();
  }

  // Exchange. Never skipped when there is nothing to send: this is what brings the web's
  // bookmark changes down.
  std::vector<BookOrbitIncomingBookmark> incoming;
  bool unmatched = false;
  bool morePending = false;
  const BookOrbitAnnotationKeys keySet{keys.empty() ? nullptr : keys.data(), keys.size(), keysComplete};
  const auto result =
      BookOrbitSyncClient::exchangeBookmarks(documentHash, BookOrbitSyncClient::DEVICE_MODEL, keySet, outgoing.data(),
                                             outgoing.size(), unmatched, &incoming, &morePending);
  LOG_INF("BookOrbit", "Bookmark exchange result=%d (http=%d, unmatched=%d)", static_cast<int>(result),
          BookOrbitSyncClient::lastHttpCode, unmatched ? 1 : 0);
  if (result == BookOrbitSyncClient::SERVER_ERROR && BookOrbitSyncClient::lastHttpCode == 404) {
    LOG_INF("BookOrbit", "Server predates bookmark sync; not asking again until reboot");
    s_bookmarkRouteUnsupported = true;
    return;
  }
  if (result != BookOrbitSyncClient::OK) return;  // retried next sync
  if (unmatched) {
    summary.documentUnmatched = true;
    return;
  }
  // A web bookmark converted during this request only ships on the next one, so the first
  // extra round is unconditional.
  const BookOrbitAnnotationKeys noKeys{nullptr, 0, false};
  for (int round = 0; round < 2 && incoming.size() < BOOKORBIT_BOOKMARK_BATCH && (round == 0 || morePending);
       round++) {
    morePending = false;
    bool roundUnmatched = false;
    if (BookOrbitSyncClient::exchangeBookmarks(documentHash, BookOrbitSyncClient::DEVICE_MODEL, noKeys, nullptr, 0,
                                               roundUnmatched, &incoming, &morePending) != BookOrbitSyncClient::OK ||
        roundUnmatched) {
      break;
    }
  }
  if (outgoingWatermark > 0 && !BookOrbitBookmarkStore::advanceWatermark(stateDir, outgoingWatermark)) {
    LOG_ERR("BookOrbit", "Bookmarks uploaded but the watermark did not advance; they will be re-sent");
  }
  summary.bookmarksSent = static_cast<uint32_t>(outgoing.size());
  if (incoming.empty()) return;

  // Apply the server's changes.
  std::vector<BookOrbitBookmarkAck> appliedAcks;
  std::vector<uint32_t> deletedIds;
  uint32_t newestMinted = 0;
  std::vector<uint32_t> mintedHere;  // received this sync: already on the server, never re-offered
  for (const BookOrbitIncomingBookmark& change : incoming) {
    if (change.deleted) {
      for (const BookOrbitBookmarkRecord& record : records) {
        char key[BookOrbitAnnotationKey::DIGEST_SIZE] = {};
        char datetime[20] = {};
        if (!keyOfRecord(record, key, datetime) || change.key != key) continue;
        if (store.removeByTimestamp(record.timestamp)) summary.bookmarksRemoved++;
        break;
      }
      // Already gone locally is the same outcome; withholding the ack would re-offer it forever.
      deletedIds.push_back(change.serverId);
      continue;
    }

    // Dedupe by position: a record already describing this pos means the bookmark is here.
    bool duplicate = false;
    for (const BookOrbitBookmarkRecord& record : records) {
      if (record.pos != change.pos) continue;
      BookOrbitBookmarkAck ack;
      ack.serverId = change.serverId;
      if (keyOfRecord(record, ack.key, ack.datetime)) {
        ack.pos = record.pos;
        appliedAcks.push_back(std::move(ack));
      }
      duplicate = true;
      break;
    }
    if (duplicate) continue;

    int spineIndex = -1;
    float intra = 0.0f;
    bool exact = false;
    if (!ChapterXPathIndexer::tryExtractSpineIndexFromXPath(change.pos, spineIndex) || spineIndex < 0 ||
        spineIndex >= epub->getSpineItemsCount()) {
      // Permanently unplaceable: park it server-side rather than have it re-offered forever.
      BookOrbitBookmarkAck ack;
      ack.serverId = change.serverId;
      ack.failed = true;
      appliedAcks.push_back(std::move(ack));
      LOG_ERR("BookOrbit", "Cannot place server bookmark %lu at %s", static_cast<unsigned long>(change.serverId),
              change.pos.c_str());
      continue;
    }
    if (!ChapterXPathIndexer::findProgressForXPath(epub, spineIndex, change.pos, intra, exact)) {
      intra = 0.0f;  // chapter start: right chapter at least, as CrossInk does without a match
    }
    const uint16_t progressQ = static_cast<uint16_t>(std::min(1.0f, std::max(0.0f, intra)) * 10000.0f + 0.5f);
    const uint32_t ts = store.addSynced(static_cast<uint16_t>(spineIndex), progressQ, change.title);
    if (ts == 0) {
      // No plausible clock to mint an identity (or the store is full): leave it unacknowledged,
      // the server offers it again next sync.
      LOG_ERR("BookOrbit", "Could not store server bookmark %lu", static_cast<unsigned long>(change.serverId));
      continue;
    }

    // Bookmarks invert the annotation convention: the DEVICE mints the identity and reports it.
    BookOrbitBookmarkRecord record;
    record.timestamp = ts;
    record.identityEpoch = ts;
    record.spineIndex = static_cast<uint16_t>(spineIndex);
    record.pos = change.pos;  // the server's pos verbatim, so both sides hash the same string
    BookOrbitBookmarkStore::put(stateDir, record);
    newestMinted = std::max(newestMinted, ts);
    mintedHere.push_back(ts);
    summary.bookmarksAdded++;

    BookOrbitBookmarkAck ack;
    ack.serverId = change.serverId;
    if (keyOfRecord(record, ack.key, ack.datetime)) {
      ack.pos = record.pos;
      appliedAcks.push_back(std::move(ack));
    }
    records.push_back(std::move(record));
  }

  store.save();
  GLOBAL_BOOKMARKS.load();
  GLOBAL_BOOKMARKS.syncFromStore(store, epub->getPath(), epub->getCachePath(), epub->getTitle(), false);
  GLOBAL_BOOKMARKS.save();

  // Identities minted here would otherwise be re-offered to the server next sync; cover them
  // unless older local records still wait to upload.
  if (newestMinted > 0) {
    const uint32_t mark = BookOrbitBookmarkStore::readWatermark(stateDir);
    const bool localStillPending = std::any_of(records.begin(), records.end(), [&](const BookOrbitBookmarkRecord& r) {
      return r.identityEpoch > mark &&
             std::find(mintedHere.begin(), mintedHere.end(), r.timestamp) == mintedHere.end();
    });
    if (!localStillPending) BookOrbitBookmarkStore::advanceWatermark(stateDir, newestMinted);
  }

  LOG_INF("BookOrbit", "Applied %u server bookmark(s), %u deletion(s)", (unsigned)summary.bookmarksAdded,
          (unsigned)deletedIds.size());
  if (!appliedAcks.empty() || !deletedIds.empty()) {
    BookOrbitSyncClient::ackBookmarks(documentHash, BookOrbitSyncClient::DEVICE_MODEL, appliedAcks, deletedIds);
  }
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

  LazyEpub epub(epubPath);
  status(tr(STR_SYNCING_BOOKMARKS));
  syncBookmarks(stateDir, documentHash, epub, summary);

  recordSweep(summary);
  return summary;
}

}  // namespace BookOrbitExtras
