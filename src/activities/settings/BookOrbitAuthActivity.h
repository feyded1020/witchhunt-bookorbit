#pragma once

#include <functional>

#include "activities/Activity.h"

/**
 * Tests the BookOrbit sync credentials: connects to WiFi, then calls the server's
 * kosync-compatible /users/auth. Accounts are created on the BookOrbit server itself
 * (Settings -> Devices & Sync -> KOReader), so there is no register mode.
 */
class BookOrbitAuthActivity final : public Activity {
 public:
  explicit BookOrbitAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookOrbitAuth", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
