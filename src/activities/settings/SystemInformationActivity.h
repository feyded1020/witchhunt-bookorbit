#pragma once

#include <optional>

#include "SystemStatus.h"
#include "activities/Activity.h"

class SystemInformationActivity final : public Activity {
 public:
  explicit SystemInformationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SystemInformation", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::optional<SystemStatus> status_;
  bool sdStatusReady_ = false;
  bool sdLoadRequested_ = false;
  // The rows no longer fit one screen, so render() flows them onto as many pages as
  // the current orientation needs and records the count here for loop() to page with.
  int page_ = 0;
  int pageCount_ = 1;
};
