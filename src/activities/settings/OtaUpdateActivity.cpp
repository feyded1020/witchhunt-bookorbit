#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/ConfirmDialog.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_CANCEL = 1;
constexpr fui::ActionId ACTION_UPDATE = 2;
}  // namespace

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  // Re-trim: the status screens rendered since onEnter can have repopulated the
  // font cache. Idempotent - the secondary buffer is already gone by now.
  trimMemoryForNetworkSession(renderer, "OTA");

  const auto res = updater.checkForUpdate();
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      failureReason = res;
      state = FAILED;
    }
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available (latest=%s, current=%s)", updater.getLatestVersion().c_str(),
            CROSSPOINT_VERSION);
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
}

void OtaUpdateActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  static_cast<OtaUpdateActivity*>(user)->finish();
}

void OtaUpdateActivity::onUpdateEvent(const fui::ActionEvent&, void* user) {
  static_cast<OtaUpdateActivity*>(user)->startUpdate();
}

void OtaUpdateActivity::confirmScreen(UiScreen& screen, void* user) {
  static_cast<OtaUpdateActivity*>(user)->buildConfirmScreen(screen);
}

namespace {
// Release notes are written for a web page, so they arrive with markdown in them. Strip the
// markup the panel cannot render and keep the words: headings lose their #, bold and code lose
// their markers, links keep their text, and runs of blank lines collapse.
std::string plainNotes(const std::string& markdown) {
  std::string out;
  out.reserve(markdown.size());
  bool lineStart = true;
  for (size_t i = 0; i < markdown.size(); i++) {
    const char c = markdown[i];
    if (c == '\n') {
      // Collapse blank lines: one break between paragraphs is all there is room for.
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      lineStart = true;
      continue;
    }
    if (lineStart && (c == '#' || c == '>' || c == ' ')) continue;  // heading / quote markers
    if (c == '*' || c == '`' || c == '_') continue;                 // bold, italics, code
    if (c == '[') {                                                 // [text](url) -> text
      const size_t close = markdown.find(']', i);
      const size_t open = close == std::string::npos ? std::string::npos : markdown.find('(', close);
      if (close != std::string::npos && open == close + 1) {
        out.append(markdown, i + 1, close - i - 1);
        const size_t end = markdown.find(')', open);
        i = end == std::string::npos ? markdown.size() : end;
        lineStart = false;
        continue;
      }
    }
    out.push_back(c);
    lineStart = false;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}
}  // namespace

void OtaUpdateActivity::buildConfirmScreen(UiScreen& screen) {
  // Two lines in one slot: FUI's layoutText() breaks on \n explicitly
  // (FreeInkUICore.h), so the current and new versions each get their own line without
  // needing a second text slot.
  updateDialogBody = std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION + "\n" +
                     std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion();
  // What changed, so the answer to "Update?" is an informed one rather than a leap.
  const std::string notes = plainNotes(updater.getReleaseNotes());
  if (!notes.empty()) {
    updateDialogBody += "\n\n" + notes;
  }

  ConfirmDialog::Spec spec;
  spec.headline = tr(STR_NEW_UPDATE);
  spec.message = updateDialogBody.c_str();
  // Two version lines, a blank, then as much of the notes as fits above the buttons.
  spec.messageMaxLines = 10;
  spec.cancelLabel = tr(STR_CANCEL);
  spec.acceptLabel = tr(STR_UPDATE);
  spec.cancelAction = ACTION_CANCEL;
  spec.acceptAction = ACTION_UPDATE;
  ConfirmDialog::draw(screen, spec);
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  resetUi();
  app.on(ACTION_CANCEL, &OtaUpdateActivity::onCancelEvent, this);
  app.on(ACTION_UPDATE, &OtaUpdateActivity::onUpdateEvent, this);
  app.setScreen(&OtaUpdateActivity::confirmScreen, this);

  // Free the heap the WiFi stack needs before it is brought up, not after -
  // association itself is the allocation-heavy step, well ahead of TLS. Matters
  // most here because SettingsActivity is still on the stack below us with its
  // per-category SettingInfo vectors resident, fragmenting the heap.
  // WifiSelectionActivity sets WIFI_STA itself, so no radio work happens here.
  trimMemoryForNetworkSession(renderer, "OTA");

  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  closeRouting();
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = contentRect.y + (contentRect.height - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    renderUi();
    // Still drawn alongside the dialog's own buttons: this labels the PHYSICAL keys, and on a
    // board with no digitiser it is the only affordance there is.
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_UPDATE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(renderer,
                        Rect{contentRect.x + metrics.contentSidePadding, y,
                             contentRect.width - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, y,
                              (std::to_string(static_cast<int>(updaterProgress * 100)) + "%").c_str());
    y += height + metrics.verticalSpacing;
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, top + height + metrics.verticalSpacing,
                      (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding,
                      top + height * 2 + metrics.verticalSpacing * 2,
                      (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    const char* reason = "";
    switch (failureReason) {
      case OtaUpdater::HTTP_ERROR:
        reason = "Network error (HTTP request failed)";
        break;
      case OtaUpdater::JSON_PARSE_ERROR:
        reason = "Could not parse release info";
        break;
      case OtaUpdater::UPDATE_OLDER_ERROR:
        reason = "Available version is not newer";
        break;
      case OtaUpdater::OOM_ERROR:
        reason = "Out of memory";
        break;
      case OtaUpdater::METADATA_TOO_LARGE_ERROR:
        reason = tr(STR_RELEASE_METADATA_TOO_LARGE);
        break;
      case OtaUpdater::INTERNAL_UPDATE_ERROR:
        reason = "Internal update error";
        break;
      case OtaUpdater::NO_UPDATE:
        reason = "No firmware asset found";
        break;
      case OtaUpdater::VALIDATE_FAILED:
        reason = "Bootloader incompatible - reflash via USB with PlatformIO";
        break;
      case OtaUpdater::WRONG_DEVICE_ERROR:
        reason = tr(STR_FIRMWARE_WRONG_DEVICE);
        break;
      default:
        break;
    }
    if (reason[0] != '\0') {
      renderer.drawCenteredText(SMALL_FONT_ID, top + height + metrics.verticalSpacing, reason);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    const int hintWidth = contentRect.width - 2 * metrics.contentSidePadding;
    const auto hintLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_POWER_ON_HINT), hintWidth, 4);
    int hintY = top + height + metrics.verticalSpacing;
    for (const auto& line : hintLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, hintY, line.c_str());
      hintY += height;
    }
  }

  renderer.displayBuffer();

  if (state == FINISHED) {
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::startUpdate() {
  LOG_DBG("OTA", "New update available, starting download...");
  // The install now streams in one blocking call; drive the progress bar and
  // Back-to-cancel from inside the download via this callback. Throttle state
  // is a member (the callback outlives this stack frame).
  lastOtaDrawMs = 0;
  updater.setInstallProgressCallback([this](size_t, size_t) -> bool {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      return false;  // abort the install
    }
    const uint32_t now = millis();
    if (now - lastOtaDrawMs >= 1000) {  // throttle e-ink redraws
      lastOtaDrawMs = now;
      requestUpdate(true);
    }
    return true;
  });
  const auto beginResult = updater.beginInstallUpdate();
  if (beginResult != OtaUpdater::UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update begin failed: %d", beginResult);
    {
      RenderLock lock(*this);
      failureReason = beginResult;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdate();
  return;
}

void OtaUpdateActivity::loop() {
  // TODO @ngxson : refactor this logic later
  if (updater.getRender()) {
    requestUpdate();
    updater.clearRender();
  }

  if (state == WAITING_CONFIRMATION) {
    // Touch first: a tap answered by a dialog button must not also reach the key tests below.
    const auto touch = routeTouch(mappedInput);
    if (touch.routed) {
      if (app.invalidated()) requestUpdate();
      if (touch) return;  // a handler ran
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }

    return;
  }

  if (state == UPDATE_IN_PROGRESS) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      updater.cancelUpdate();
      finish();
      return;
    }

    const auto res = updater.performInstallUpdateStep();
    if (res == OtaUpdater::UPDATE_IN_PROGRESS) {
      if (updater.getRender()) {
        requestUpdate();
        updater.clearRender();
      }
      return;
    }

    if (res == OtaUpdater::OK) {
      {
        RenderLock lock(*this);
        state = FINISHED;
      }
      requestUpdate();
      return;
    }

    if (res == OtaUpdater::UPDATE_CANCELLED) {
      finish();
      return;
    }

    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failureReason = res;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == NO_UPDATE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
