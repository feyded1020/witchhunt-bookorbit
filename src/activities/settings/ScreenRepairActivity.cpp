#include "ScreenRepairActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/ConfirmDialog.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_CANCEL = 1;
constexpr fui::ActionId ACTION_START = 2;
}  // namespace

void ScreenRepairActivity::onEnter() {
  Activity::onEnter();
  state = WARNING;
  resetUi();
  app.on(ACTION_CANCEL, &ScreenRepairActivity::onCancelEvent, this);
  app.on(ACTION_START, &ScreenRepairActivity::onStartEvent, this);
  app.setScreen(&ScreenRepairActivity::warningScreen, this);
  requestUpdate();
}

void ScreenRepairActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void ScreenRepairActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  static_cast<ScreenRepairActivity*>(user)->finish();
}

void ScreenRepairActivity::onStartEvent(const fui::ActionEvent&, void* user) {
  static_cast<ScreenRepairActivity*>(user)->startRepair();
}

// The one place the confirmation turns into work, so the key press and the touch target cannot
// drift apart.
void ScreenRepairActivity::startRepair() {
  {
    RenderLock lock(*this);
    state = REPAIRING;
  }
  // Paint the "this will take a while" screen and WAIT for it, so the reader sees why the panel is
  // about to flash for twenty seconds. The cycle below never yields to a render.
  requestUpdateAndWait();
  runRepairCycle();
}

void ScreenRepairActivity::warningScreen(UiScreen& screen, void* user) {
  static_cast<ScreenRepairActivity*>(user)->buildWarningScreen(screen);
}

void ScreenRepairActivity::buildWarningScreen(UiScreen& screen) {
  ConfirmDialog::Spec spec;
  spec.message = tr(STR_SCREEN_REPAIR_BODY);
  spec.cancelLabel = tr(STR_CANCEL);
  spec.acceptLabel = tr(STR_START);
  spec.cancelAction = ACTION_CANCEL;
  spec.acceptAction = ACTION_START;
  ConfirmDialog::draw(screen, spec);
}

void ScreenRepairActivity::loop() {
  if (state == WARNING) {
    // Touch first: a tap answered by a dialog button must not also reach the key tests below.
    const auto touch = routeTouch(mappedInput);
    if (touch.routed) {
      if (app.invalidated()) requestUpdate();
      if (touch) return;  // a handler ran
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startRepair();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == DONE && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void ScreenRepairActivity::runRepairCycle() {
  LOG_INF("REPAIR", "Screen repair: %d alternations + %d trailing white", kAlternations, kTrailingWhite);

  // Held for the whole cycle: this drives the panel directly, and a render landing in the middle
  // would put a page between two conditioning pushes and undo the point of them.
  RenderLock lock(*this);

  // HALF rather than FAST throughout. FAST is the differential bank, which drives only what
  // changed -- and after the first black push nothing does. HALF is the clean bank, the one
  // waveform on this board that drives every pixel every time, which is the whole exercise.
  const auto drive = [this](const uint8_t colour) {
    renderer.clearScreen(colour);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  };

  constexpr uint8_t kBlack = 0x00;
  constexpr uint8_t kWhite = 0xFF;

  for (int i = 0; i < kAlternations; ++i) {
    drive(kBlack);
    drive(kWhite);
  }
  // Finish on white, and dwell there: white is the panel's rest state, so the last thing the ink
  // is asked to do should be to settle rather than to swing.
  for (int i = 0; i < kTrailingWhite; ++i) {
    drive(kWhite);
  }

  LOG_INF("REPAIR", "Screen repair complete");
  state = DONE;
  requestUpdate();
}

void ScreenRepairActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_SCREEN_REPAIR));

  const int midY = contentRect.y + contentRect.height / 2;

  if (state == WARNING) {
    renderUi();
    // Still drawn alongside the dialog's own buttons: this labels the PHYSICAL keys, and on a
    // board with no digitiser it is the only affordance there is.
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_START), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == REPAIRING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_SCREEN_REPAIR_RUNNING));
    renderer.displayBuffer();
    return;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_SCREEN_REPAIR_DONE), true, EpdFontFamily::BOLD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // FULL, so the screen the reader is left looking at is itself a clean one rather than a
  // differential against the white the cycle ended on.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
