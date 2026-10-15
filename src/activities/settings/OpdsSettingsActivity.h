#pragma once

#include <string>

#include "OpdsServerStore.h"
#include "activities/UiListActivity.h"

// Adapted from upstream crosspoint-reader's OpdsSettingsActivity (develop @ cdac66ffe,
// src/activities/settings/OpdsSettingsActivity.cpp).
/**
 * Edit screen for a single OPDS server.
 * Shows Name, URL, Username, Password fields and a Delete option.
 * Used for both adding new servers and editing existing ones.
 */
class OpdsSettingsActivity final : public UiListActivity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1);

  void onEnter() override;

 private:
  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  // The hints, then the popup (if one is up) over the finished frame, which the base render ships.
  void drawFooter() override;

  int getMenuItemCount() const;
  void handleSelection(int index);
  // Stores one keyboard result in an editServer field, then saves.
  void commitField(std::string& field, const std::string& text);
  bool saveServer();
  // The message of the popup this frame carries, or nullptr.
  const char* activePopup() const;

  int serverIndex;
  // Read by the render task: editServer's strings through fieldRowItems[].value, isNewServer for
  // the title and the row count, and the two popup flags. The loop task writes them only under
  // RenderLock.
  OpdsServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;
  bool invalidUrlPopup = false;

  // Row storage: at most 5 rows (Name/URL/Username/Password + Delete, see
  // BASE_ITEMS in the .cpp), so a fixed-capacity array avoids any heap
  // allocation for the row list. Labels are set once in the constructor
  // (they never change); buildScreen() only refreshes the value pointers,
  // which point at editServer's own fields (no new strings built).
  static constexpr int MAX_MENU_ITEMS = 5;
  freeink::ui::ListItem fieldRowItems[MAX_MENU_ITEMS]{};
};
