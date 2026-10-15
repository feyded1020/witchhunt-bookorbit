#pragma once

#include "activities/UiListActivity.h"

// Reader status bar configuration activity.
//
// Adapted from upstream crosspoint-reader's StatusBarSettingsActivity (develop @ cdac66ffe,
// src/activities/settings/StatusBarSettingsActivity.cpp): the FreeInkUI row list built from a fixed
// ListItem array, with on/off items as switches. The items themselves (statusBarItems[] in the
// .cpp), cycling the other items on Confirm, and the boxed preview are ours: upstream has a
// smaller item set, edits values in a popup and previews with the real status bar, which cannot
// show our upper/lower bars, items position, printed page or clock position.
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

  // Every entry of statusBarItems[] (the .cpp static_asserts the match). The clock rows are the
  // only ones that can drop out.
  static constexpr int MAX_STATUS_BAR_ITEMS = 12;

  void onEnter() override;

 private:
  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  [[nodiscard]] const char* footerConfirmLabel() const override;
  // Draws the preview into the band buildScreen() keeps free under the list.
  void afterUiRender() override;

  // The rows this visit shows. The clock rows drop out while the clock is off, and that cannot
  // change while this screen is open (it is set in Settings > Clock).
  int rowCount = 0;
  // Labels and action values are set once in onEnter(); buildScreen() refreshes each row's value or
  // switch from SETTINGS. Every string is an I18N pointer, so nothing is allocated per row.
  freeink::ui::ListItem rowItems[MAX_STATUS_BAR_ITEMS]{};
};
