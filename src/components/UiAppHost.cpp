#include "UiAppHost.h"

#include "UiAppHelpers.h"

UiAppHost::UiAppHost(const GfxRenderer& renderer)
    : uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

void UiAppHost::resetUi() {
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
}

void UiAppHost::renderUi() {
  app.setDevice(uiTarget.deviceContext());
  app.render();
  uiReady = true;
}

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input, const bool routeHeld) {
  TouchRoute result;
  if (!uiReady) return result;

  result.snap = touchSnapshotFrom(input);
  if (!result.snap.touchPressed && !result.snap.touchReleased && !(routeHeld && result.snap.touchHeld)) {
    return result;
  }

  result.routed = true;
  result.event = app.route(result.snap);
  return result;
}