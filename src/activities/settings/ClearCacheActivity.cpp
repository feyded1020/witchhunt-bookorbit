#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/ConfirmDialog.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_CANCEL = 1;
constexpr fui::ActionId ACTION_CLEAR = 2;
}  // namespace

void ClearCacheActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  resetUi();
  app.on(ACTION_CANCEL, &ClearCacheActivity::onCancelEvent, this);
  app.on(ACTION_CLEAR, &ClearCacheActivity::onClearEvent, this);
  app.setScreen(&ClearCacheActivity::warningScreen, this);
  requestUpdate();
}

void ClearCacheActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void ClearCacheActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ClearCacheActivity*>(user);
  LOG_DBG("CLEAR_CACHE", "User cancelled");
  self->goBack();
}

void ClearCacheActivity::onClearEvent(const fui::ActionEvent&, void* user) {
  static_cast<ClearCacheActivity*>(user)->startClearing();
}

// The one place the confirmation turns into work, so the button and the touch target cannot drift.
void ClearCacheActivity::startClearing() {
  LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
  {
    RenderLock lock(*this);
    state = CLEARING;
  }
  requestUpdateAndWait();
  clearCache();
}

void ClearCacheActivity::warningScreen(UiScreen& screen, void* user) {
  static_cast<ClearCacheActivity*>(user)->buildWarningScreen(screen);
}

void ClearCacheActivity::buildWarningScreen(UiScreen& screen) {
  // Warnings 3 and 4 are one sentence split across two keys; joined here as the body, exactly as
  // the hand-drawn version did. Held in a member-lifetime string because OptionDialogProps stores
  // a pointer and the draw happens inside ConfirmDialog::draw().
  warningBody = std::string(tr(STR_CLEAR_CACHE_WARNING_3)) + " " + tr(STR_CLEAR_CACHE_WARNING_4);

  ConfirmDialog::Spec spec;
  spec.title = tr(STR_CLEAR_CACHE_WARNING_1);
  spec.headline = tr(STR_CLEAR_CACHE_WARNING_2);
  spec.message = warningBody.c_str();
  spec.cancelLabel = tr(STR_CANCEL);
  spec.acceptLabel = tr(STR_CLEAR_BUTTON);
  spec.cancelAction = ACTION_CANCEL;
  spec.acceptAction = ACTION_CLEAR;
  // Warning 1 is a sentence, not a caption, so it may wrap.
  spec.titleMaxLines = 3;
  ConfirmDialog::draw(screen, spec);
}

void ClearCacheActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_CLEAR_READING_CACHE));

  const int midY = contentRect.y + contentRect.height / 2;
  if (state == WARNING) {
    renderUi();
    // Still drawn alongside the dialog's own buttons: this is how the PHYSICAL keys are labelled,
    // and on a board with no digitiser it is the only affordance there is.
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAR_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_CLEARING_CACHE));
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 20, tr(STR_CACHE_CLEARED), true, EpdFontFamily::BOLD);
    std::string resultText = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 20, tr(STR_CLEAR_CACHE_FAILED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    state = FAILED;
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    if (file.isDirectory() && (itemName.startsWith("epub_") || itemName.startsWith("xtc_") ||
                               itemName.startsWith("txt_") || itemName == "fontprev")) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  state = SUCCESS;
  requestUpdate();
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
    // Touch first: a tap that lands on a dialog button is answered by that button, and must not
    // also reach the key tests below.
    const auto touch = routeTouch(mappedInput);
    if (touch.routed) {
      if (app.invalidated()) requestUpdate();
      if (touch) return;  // a handler ran
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startClearing();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG("CLEAR_CACHE", "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
