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

  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  TouchRoute routeTouch(const MappedInputManager& input);
  void closeRouting() { uiReady = false; }

  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;

 private:
  std::atomic<bool> uiReady{false};
};