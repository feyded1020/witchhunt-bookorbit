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
  // The layout values this screen's app draws with (row and header heights, padding, the scroll
  // bar), derived from the theme metrics and UI fonts in resetUi(). Each screen holds its own,
  // so they live on the heap only while a list or dialog does: they used to be a static
  // double-buffered pool shared by every screen -- 3.4 KB of RAM for the whole session,
  // reading included, where no FreeInkUI screen is on display. Only this screen's own render
  // reads them, after its onEnter() has filled them in, so one copy needs no second buffer.
  freeink::ui::ThemeTokens themeTokens{};
  std::atomic<const freeink::ui::ThemeTokens*> themeCell{nullptr};
};