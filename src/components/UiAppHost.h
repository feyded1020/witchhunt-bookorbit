#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

class GfxRenderer;
class MappedInputManager;

class UiAppHost {
 public:
  using UiApp = freeink::ui::FreeInkApp<24, 6>;
  using UiScreen = UiApp::ScreenType;

  explicit UiAppHost(const GfxRenderer& renderer);

  void resetUi();
  void renderUi();

  // What loop-task routing saw this pass. `routed` is true when the gate was open and the
  // snapshot carried touch input the screen cares about; `snap` is that snapshot, so a caller
  // can also ask where the contact was and whether it ended -- which is how a drawer tells a
  // tap on itself from a tap on the page it is covering.
  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    freeink::ui::InputSnapshot snap{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  // `routeHeld` forwards held frames to InputDrag elements (sliders). Without it a contact is
  // only routed on its press and release edges, so a slider knob jumps to where the finger
  // landed and then to where it lifted, with nothing in between.
  //
  // Upstream (crosspoint-reader bbca4886) also carries a `withLongPress` flag here. It is left
  // out rather than accepted-and-ignored: touchSnapshotFrom() in this fork has no long-press
  // path for it to forward, so the parameter would be a promise the code does not keep.
  TouchRoute routeTouch(const MappedInputManager& input, bool routeHeld = false);
  void closeRouting() { uiReady = false; }

  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;

 private:
  std::atomic<bool> uiReady{false};
};