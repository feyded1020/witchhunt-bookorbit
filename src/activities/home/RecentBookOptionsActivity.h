#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// What you can do with a book in Recent Books, as a list rather than a set of direction-and-hold
// shortcuts. Those shortcuts still work, but they name keys the X4 Pro does not have, so on a
// touch-only board they were both unreachable and undiscoverable.
//
// Returns the chosen action as a MenuResult; cancelled when the user backs out.
class RecentBookOptionsActivity final : public Activity {
 public:
  enum class Action : int { Open = 0, Info = 1, Remove = 2 };

  explicit RecentBookOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle)
      : Activity("RecentBookOptions", renderer, mappedInput), title(std::move(bookTitle)) {}

  void onEnter() override;
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ACTION_COUNT = 3;
  std::string title;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
};
