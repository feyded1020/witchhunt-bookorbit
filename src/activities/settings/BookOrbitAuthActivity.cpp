#include "BookOrbitAuthActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "BookOrbitSyncClient.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#if CROSSPOINT_KOREADER_AUTOSYNC
#include "activities/reader/KOReaderSyncWorker.h"
#endif
#include "components/UITheme.h"
#include "fontIds.h"

void BookOrbitAuthActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  // Same reason as the sync path: modem sleep can stall a request for seconds, which shows up
  // as an HTTP timeout on a handful of small round trips. WiFi is torn down on exit.
  // Ported from crosspoint-reader PR #3233 (Jadehawk / @jadehawk).
  WiFi.setSleep(false);
  LOG_DBG("BookOrbit", "WiFi sleep disabled for authentication");

  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = tr(STR_AUTHENTICATING);
  }
  requestUpdateAndWait();  // show status before blocking TLS call

  // Set the clock before the first TLS connect. This is what was missing: unlike the sync
  // activity (and HttpDownloader) this path went straight from "WiFi up" to a wolfSSL connect,
  // and on these RTC-less boards a cold boot that discarded a stale NVS epoch is sitting at
  // 1970 — which puts every curated root's notBefore in the future and makes the trust store
  // fail to LOAD, long before any certificate is seen. Not fatal if it fails: SecureClient then
  // waives certificate dates only, keeping chain/signature/hostname checks intact.
  HalClock::ensureUsableForTls(SETTINGS.ntpServer);
  // Same escape hatch as the sync path: a self-hosted server with a private CA.
  BookOrbitSyncClient::setSkipTlsValidation(SETTINGS.skipHttpsValidation != 0);

  performAuthentication();
}

void BookOrbitAuthActivity::performAuthentication() {
  const auto result = BookOrbitSyncClient::authenticate();

  {
    RenderLock lock(*this);
    if (result == BookOrbitSyncClient::OK) {
      state = SUCCESS;
      statusMessage = tr(STR_AUTH_SUCCESS);
    } else {
      state = FAILED;
      errorMessage = BookOrbitSyncClient::errorString(result);
      const char* detail = BookOrbitSyncClient::lastFailureDetail();
      if (detail && detail[0]) {
        errorMessage += " — ";
        errorMessage += detail;
      }
    }
  }
  requestUpdate();
}

void BookOrbitAuthActivity::onEnter() {
  Activity::onEnter();

#if CROSSPOINT_KOREADER_AUTOSYNC
  if (KOReaderSyncWorker::isBusy()) {
    {
      RenderLock lock(*this);
      state = AUTHENTICATING;
      statusMessage = tr(STR_KO_BG_SYNC_WAIT);
    }
    requestUpdateAndWait();
    constexpr unsigned long BACKGROUND_DRAIN_TIMEOUT_MS = 10000;
    if (!KOReaderSyncWorker::drain(BACKGROUND_DRAIN_TIMEOUT_MS)) {
      LOG_ERR("KOSync", "Background sync job still running; proceeding anyway");
    }
  }
#endif

  // Free the heap the WiFi stack needs before it is brought up, not after —
  // association itself is the allocation-heavy step, well ahead of TLS.
  trimMemoryForNetworkSession(renderer, "BookOrbit");

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void BookOrbitAuthActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
  }
  // Unconditional: onEnter() released the secondary framebuffer for the network
  // session and nothing reallocates it, so every exit path — including a
  // cancelled WiFi selection that never brought the radio up — must reboot to
  // restore double-buffered rendering. Land back in BookOrbit settings, where the
  // user started, rather than on Home.
  silentRestartToKOReaderSettings();
}

void BookOrbitAuthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  GUI.drawHeader(renderer, UITheme::getHeaderRect(renderer), tr(STR_BOOKORBIT_SYNC));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = contentRect.y + (contentRect.height - height) / 2;

  if (state == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str());
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_SUCCESS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, tr(STR_SYNC_READY));
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_FAILED), true, EpdFontFamily::BOLD);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, errorMessage.c_str(), contentRect.width - 20, 4);
    int y = top + height + 10;
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += height;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void BookOrbitAuthActivity::loop() {
  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}
