#pragma once

#include "activities/MenuListActivity.h"

/**
 * Submenu for BookOrbit Sync settings: server, credentials, conflict policy, auto-sync,
 * catalog download folder, and a login test.
 */
class BookOrbitSettingsActivity final : public MenuListActivity {
 public:
  explicit BookOrbitSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void render(RenderLock&&) override;

 private:
  void buildMenuItems();

  // MenuListActivity overrides
  std::string getItemValueString(int index) const override;
  void onActionSelected(int index) override;
};
