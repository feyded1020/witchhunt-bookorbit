#include "BookOrbitSyncExtras.h"

#include <BookOrbitSyncClient.h>
#include <I18n.h>
#include <Logging.h>


namespace BookOrbitExtras {

namespace {
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
  (void)epubPath;
  (void)documentHash;
  (void)status;
  Summary summary;
  recordSweep(summary);
  return summary;
}

}  // namespace BookOrbitExtras
