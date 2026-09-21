#include "Activity.h"

#include "ActivityManager.h"
#include "components/themes/ButtonHintStrip.h"
#include "components/themes/ListTouchBand.h"
#include "components/themes/TapTargets.h"

// The recorded button-hint strip, list rows and tap targets belong to the screen that painted
// them. Drop them all on every transition, in both directions: a screen that draws no hints would otherwise
// inherit the previous one's boxes and turn taps near the bottom edge into phantom button
// presses, and one that draws no list would inherit its rows and turn a tap anywhere in the
// content area into a phantom selection. Each screen re-records on its next render, so the
// only gap is between the transition and that render -- during which there is correctly
// neither.
void Activity::onEnter() {
  listTapActivation.reset();
  ButtonHintStrip::invalidate();
  ButtonHintStrip::Side::invalidate();
  ListTouchBand::invalidate();
  TapTargets::homeCovers().invalidate();
  TapTargets::homeMenu().invalidate();
  TapTargets::readerLinks().invalidate();
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
}

void Activity::onExit() {
  ButtonHintStrip::invalidate();
  ButtonHintStrip::Side::invalidate();
  ListTouchBand::invalidate();
  TapTargets::homeCovers().invalidate();
  TapTargets::homeMenu().invalidate();
  TapTargets::readerLinks().invalidate();
  LOG_DBG("ACT", "Exiting activity: %s", name.c_str());
}

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

bool Activity::isUpdateSuperseded() const { return activityManager.isUpdateSuperseded(); }

// "Up and out" — return to whichever parent launched this flow. If no return hint
// is set (typical for activities launched via a plain goTo*()), falls back to Home.
void Activity::onGoHome() { activityManager.returnFromChild(); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

bool Activity::consumeListRowLongPress(int& index) {
  if (!mappedInput.hasTouch()) return false;

  // Live orientation, not Portrait: GUI.drawList() paints in whatever orientation the renderer
  // is in, so that is the frame its rows were recorded in. Only the hint strip forces Portrait.
  int x = 0;
  int y = 0;
  if (!mappedInput.peekScreenLongPressIn(static_cast<touchtransform::Orientation>(renderer.getOrientation()), x, y)) {
    return false;
  }

  const int row = ListTouchBand::hitTest(x, y);
  if (row < 0) return false;

  // peek + suppress, the contract ActivityManager::dispatchHintStripTap() uses: claimed only
  // once the hold is known to be over a row, so a hold anywhere else still reaches the strip.
  // Claiming it also stops the lift that ends the hold reading as a tap on the same row.
  mappedInput.suppressTouchContact();
  index = row;
  return true;
}
