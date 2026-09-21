#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// What you can do with a book in Recent Books, as a list rather than a set of direction-and-hold
// shortcuts. Those shortcuts still work, but they name keys the X4 Pro does not have, so on a
// touch-only board they were both unreachable and undiscoverable.
//
// Returns the chosen action as a MenuResult; cancelled when the user backs out.
class RecentBookOptionsActivity final : public Activity {
 public:
  // Open and sync is offered only with a BookOrbit account configured, so the row order is not
  // fixed: the chosen Action is what comes back, never a row number.
  enum class Action : int { Open = 0, OpenAndSync = 1, Info = 2, Remove = 3 };

  explicit RecentBookOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle,
                                     bool offerSync)
      : Activity("RecentBookOptions", renderer, mappedInput), title(std::move(bookTitle)) {
    actions.push_back(Action::Open);
    if (offerSync) actions.push_back(Action::OpenAndSync);
    actions.push_back(Action::Info);
    actions.push_back(Action::Remove);
  }

  void onEnter() override;
  ListRowTap::Result selectListRow(int index) override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static std::string labelFor(Action action);
  int rowCount() const { return static_cast<int>(actions.size()); }

  std::vector<Action> actions;
  std::string title;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
};
