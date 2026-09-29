#pragma once

// Settings > System > Keyboard Layouts: which layouts the keyboard's language key
// cycles through. One row per layout, toggled on and off; the last Latin layout
// cannot be switched off, since URL and password fields need one.
//
// Ported from crosspoint-reader (PR #2858 by winst0niuss, with Uri Tauber).

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "activities/util/KeyboardLayoutSet.h"

class MappedInputManager;

class KeyboardLayoutsActivity final : public UiListActivity {
 public:
  explicit KeyboardLayoutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("KeyboardLayouts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return keyboard_layouts::COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // The row is the only Latin layout left on: it stays on, and says so.
  bool isLocked(uint8_t i) const;

  freeink::ui::ListItem rowItems[keyboard_layouts::COUNT]{};
  uint16_t workingMask = 0;
  // Do not persist the derived default merely because the screen was visited: an
  // unconfigured set keeps following the UI language.
  bool edited = false;
};
