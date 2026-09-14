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

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input) {
  TouchRoute result;
  if (!uiReady) return result;

  const auto snapshot = touchSnapshotFrom(input);
  if (!snapshot.touchPressed && !snapshot.touchReleased) return result;

  result.routed = true;
  result.event = app.route(snapshot);
  return result;
}