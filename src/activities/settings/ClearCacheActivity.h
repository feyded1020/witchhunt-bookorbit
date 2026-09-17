#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// The WARNING state asks a yes/no question, so it draws the shared ConfirmDialog rather than
// hand-placed centred text plus a hint strip -- the question now has on-screen buttons a finger can
// hit. The other three states are progress/result screens with nothing to answer, so they are
// unchanged.
class ClearCacheActivity final : public Activity, private UiAppHost {
 public:
  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClearCache", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }

  int clearedCount = 0;
  int failedCount = 0;
  // Warnings 3 and 4 joined into one sentence. A member, not a local: OptionDialogProps stores a
  // pointer to it and the draw happens after buildWarningScreen() returns.
  std::string warningBody;
  void clearCache();

  static void warningScreen(UiScreen& screen, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onClearEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildWarningScreen(UiScreen& screen);
  void startClearing();
};
