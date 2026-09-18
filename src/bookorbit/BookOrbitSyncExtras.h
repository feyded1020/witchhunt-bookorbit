#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

/**
 * Everything a BookOrbit sync does besides reading progress, run on the sync's open TLS
 * session: queued reading-session stats, the highlight and bookmark exchanges, and the sweep
 * record that tells the server this device measures its own reading time.
 *
 * Ported from CrossInk-Bookorbit's BookOrbitSyncActivity (MIT) and adapted to Witch Hunt's
 * stores. Best effort throughout: a failed step is logged and retried on the next sync; it
 * never fails the progress sync that follows.
 */
namespace BookOrbitExtras {

struct Summary {
  size_t statsAccepted = 0;
  uint32_t annotationsSent = 0;
  uint32_t annotationsAdded = 0;
  uint32_t annotationsRemoved = 0;
  uint32_t bookmarksSent = 0;
  uint32_t bookmarksAdded = 0;
  uint32_t bookmarksRemoved = 0;
  bool documentUnmatched = false;
};

// `status` is called with a user-facing line before each network phase.
Summary run(const std::string& epubPath, const std::string& documentHash,
            const std::function<void(const char*)>& status);

}  // namespace BookOrbitExtras
