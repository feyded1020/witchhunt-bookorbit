#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// What this screen's buttons and gestures do, listed for the screen you came from.
//
// It exists because a hint strip can only name a short press, and on some screens the useful
// half of the controls is a hold -- of a button, or of a row. Which of those a board can even
// make varies: X4 Pro has no Back, Confirm, Left or Right key at all, so a caller filters its
// own list before handing it over rather than this screen guessing.
//
// Deliberately NOT a list of everything. The visible buttons are already labelled on the strip;
// repeating them is noise. A caller lists what a reader could not otherwise discover.
class ControlsActivity final : public Activity {
 public:
  struct Entry {
    std::string action;   // what happens
    std::string gesture;  // how to ask for it
  };

  ControlsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Entry> entries)
      : Activity("Controls", renderer, mappedInput), entries(std::move(entries)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  ListRowTap::Result selectListRow(int index) override;

 private:
  int rowCount() const { return static_cast<int>(entries.size()); }

  std::vector<Entry> entries;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
};
