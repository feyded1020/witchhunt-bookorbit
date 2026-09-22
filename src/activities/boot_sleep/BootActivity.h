#pragma once
#include "../Activity.h"

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;
  // The splash IS the feedback for boot; marking the hand-off to Home paints an
  // indicator over it for a transition nobody is waiting on.
  bool suppressesBusyIndicator() const override { return true; }
};
