#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class UiListActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  static constexpr freeink::ui::ActionId ACTION_USER = 2;

  UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  virtual int listCount() const = 0;
  virtual void buildScreen(UiScreen& screen) = 0;
  virtual void activateIndex(int index) = 0;
  virtual freeink::ui::ListNav& activeNav() { return nav; }
  virtual bool handleCustomInput() { return false; }
  virtual bool handleButtons();
  virtual void onBackButton() { finish(); }
  virtual void onSelectionChanged(int /*index*/) {}
  virtual int indexForActionValue(int16_t value) const { return value; }
  virtual const char* headerTitle() const { return nullptr; }
  virtual void drawChrome();
  virtual void drawFooter();
  virtual void navigateButtons();

  void syncListViewport(UiScreen& screen, freeink::ui::ListProps& props, bool hasSubtitle = false);
  void moveSelectionTo(int index);

  freeink::ui::ListNav nav;
  ButtonNavigator buttonNavigator;

 private:
  static void screenTrampoline(UiScreen& screen, void* user);
  static void rowActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void onRowAction(int index);
  bool routeListTouch();
};