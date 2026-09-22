#pragma once
#include <Epub.h>

#include <memory>

#include "BookOrbitSyncClient.h"
#include "ChapterXPathIndexer.h"
#include "CrossPointState.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"

/**
 * Activity for syncing with a BookOrbit server: reading progress, reading-session stats,
 * highlights and bookmarks (ported from CrossInk-Bookorbit onto Witch Hunt's sync flow).
 *
 * This activity is launched as a standalone replacement screen, not as a
 * child activity of the reader. The reader persists a compact handoff record,
 * is destroyed to reclaim memory before WiFi/TLS work begins, and a fresh
 * reader instance is reopened after sync completes or is cancelled.
 *
 * Shared pipeline:
 * 1. Connect to WiFi (if not connected)
 * 2. Optionally sync NTP (if stale)
 * 3. Calculate document hash
 *
 * Intent-specific behavior:
 * - COMPARE: fetch remote progress. Under ASK_EVERY_TIME, show the full comparison
 *   screen and let the user choose Apply or Upload; under SMART, resolve it in favour
 *   of whichever side is further and report the outcome.
 * - PULL_REMOTE: fetch and map remote progress, show success feedback, then
 *   persist an applied SyncResult for the reopened reader.
 * - PUSH_LOCAL: compute local mapping, warm session with GET, then upload via
 *   reused connection to avoid a second full TLS handshake.
 */
class BookOrbitSyncActivity final : public Activity {
 public:
  explicit BookOrbitSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& epubPath,
                                 int currentSpineIndex, int currentPage, int totalPagesInSpine,
                                 uint16_t paragraphIndex = 0, bool hasParagraphIndex = false,
                                 uint32_t xhtmlSeekHint = 0,
                                 KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE)
      : Activity("BookOrbitSync", renderer, mappedInput),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine),
        localParagraphIndex(paragraphIndex),
        hasLocalParagraphIndex(hasParagraphIndex),
        localXhtmlSeekHint(xhtmlSeekHint),
        syncIntent(syncIntent),
        remoteProgress{},
        remotePosition{},
        localProgress{} {}

  void onEnter() override;
  void onExit() override;
  // Tap on one of the three conflict options -> select it; ActivityManager synthesizes
  // Confirm. Only accepted in SHOWING_RESULT, the one state that draws them.
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;
  // UPLOADING belongs here too: sleeping mid-PUT drops the connection with the write in
  // flight, and the user is not touching buttons while it runs.
  //
  // The two screens that ask the user a question hold sleep off as well, but only for
  // DECISION_KEEP_AWAKE_MS: sleeping there abandons the sync silently (deep sleep boots fresh
  // into the reader and nothing relaunches this screen), while holding it off forever would let
  // a forgotten prompt flatten the battery. When the window runs out the screen closes itself
  // and the reader reports the unfinished sync -- deliberately NOT by handing the question over
  // to auto-sleep, which only starts its own (10 minute, user-configurable) inactivity timer at
  // that point and leaves the prompt sitting there with nothing visibly happening.
  bool preventAutoSleep() override {
    if (state == CONNECTING || state == SYNCING || state == UPLOADING) return true;
    if (state != SHOWING_RESULT && state != NO_REMOTE_PROGRESS) return false;
    return decisionShownAtMs != 0 && millis() - decisionShownAtMs < DECISION_KEEP_AWAKE_MS;
  }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    APPLY_COMPLETE,
    // Smart mode found the two sides already at the same place: nothing to upload or apply.
    SYNC_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;
  uint16_t localParagraphIndex;
  bool hasLocalParagraphIndex;
  uint32_t localXhtmlSeekHint;
  KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;

  // One TLS connection shared by every request of a sync (progress, stats, highlights,
  // bookmarks). Reset before heavy local work so the inflate buffers get the heap back.
  std::unique_ptr<BookOrbitSyncClient::Session> syncSession;
  void beginSession();
  void endSession();

  // Stats upload, highlight and bookmark exchange, and the sweep record. Runs on the open
  // session right after the progress GET proved the server reachable. Best effort: a failure
  // here is logged and retried next sync, never fails the progress sync itself.
  void runBookOrbitExtras();

  // Remote progress data
  bool hasRemoteProgress = false;
  bool remotePositionMapped = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;

  // Local progress as KOReader format (for display)
  KOReaderPosition localProgress;
  std::string remoteChapterLabel;
  std::string localChapterLabel;
  std::optional<KOReaderMetadata> localDocumentMetadata;

  // Selection in the compare screen: 0=Apply remote, 1=Upload local, 2=conflict policy toggle.
  // The policy row is not an action — confirming it flips the setting and returns the cursor to
  // whichever of the two actions the new policy would pick.
  static constexpr int OPTION_SYNC_BEHAVIOR = 2;
  static constexpr int OPTION_COUNT = 3;
  int selectedOption = 0;

  // How long a pending apply/upload question waits for an answer before closing itself. The
  // prompt appears while the reader is in the user's hands, so this is about walking away
  // mid-decision, not about giving them time to read it.
  static constexpr unsigned long DECISION_KEEP_AWAKE_MS = 3UL * 60UL * 1000UL;
  // millis() when the current question first went up; 0 when no question is pending.
  unsigned long decisionShownAtMs = 0;
  // True once the question has been recorded as abandoned, so it is written once.
  bool decisionAbandoned = false;
  // Tracks the pending question: starts the timer, and records the abandon when it runs out.
  void serviceDecisionTimeout();

  // Timestamp when completion state was entered (for auto-close)
  unsigned long uploadCompleteTime = 0;
  bool closeRequested = false;

  // Tracks whether this session activated WiFi. Set in onEnter past the credentials
  // check; checked in onExit to decide whether to silent-reboot. Can't rely on
  // WiFi.getMode() because intermediate paths call esp_wifi_stop() to drop the
  // radio while user reads the result, which makes WiFi.getMode() return WIFI_MODE_NULL.
  bool wifiActivated = false;

  // Captured from APP_STATE.koReaderSyncSession's postAction/postActionTarget in resumeReader()
  // so onExit() can route the silent reboot even after resumeReader() has cleared the persisted
  // session (Reader/Home/OpenBook all resolve their destination before the reboot happens, so
  // nothing needs to survive it — only OpdsSearch leaves the persisted fields in place, since that
  // one is resolved by HomeActivity after the reboot instead).
  KOReaderSyncPostAction postAction_ = KOReaderSyncPostAction::Reader;
  std::string postActionTarget_;

  void onWifiSelectionComplete(bool success);
  void performSync();
  bool calculateDocumentHash();
  bool smartSyncEnabled() const;
  // -1 remote is further, 0 the two agree, +1 local is further.
  int compareLocalToRemote() const;
  // Persist the mapped remote position and show the apply confirmation (or return straight
  // to the reader for auto-pull). Shared by the pull intent and smart resolution.
  void applyRemoteAndFinish();
  bool handleAutoPushPreflight();
  void performFetchAndCompare();
  void performUpload();
  void closeCancelled();
  void resumeReader(KOReaderSyncOutcomeState outcome, const SyncResult* appliedResult = nullptr);
  bool ensureEpubLoadedForMapping();
  void releaseEpubForMapping();
  bool computeLocalProgressAndChapter();
  void computeRemoteChapter();
  bool ensureRemotePositionMapped(bool closeSessionBeforeMapping = true);
};
