#include "MenuListActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "settings/SettingsSubmenuActivity.h"

namespace fui = freeink::ui;

MenuListActivity::MenuListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput), selectedIndex(nav.selected) {}

void MenuListActivity::initMenuList() {
  const int count = static_cast<int>(menuItems.size());
  const auto pred = UITheme::makeSelectablePredicate(count, [this](int i) { return menuItems[i].getTitle(); });
  buttonNavigator.setSelectablePredicate(pred, count);
  if (count > 0 && !pred(selectedIndex)) {
    selectedIndex = buttonNavigator.nextIndex(selectedIndex);
  }
}

void MenuListActivity::onEnter() {
  if (!submenusPrepared) {
    prepareSubmenus();
    SettingInfo::insertSubcategorySeparators(menuItems);
    submenusPrepared = true;
  }
  UiListActivity::onEnter();
  initMenuList();
  requestUpdate();
}

void MenuListActivity::handleNavigation() {
  const int count = static_cast<int>(menuItems.size());
  // Up/Down step, Left/Right page by whatever the last render fit on screen. A menu shorter than
  // one page pages by a single item, so short menus are unchanged.
  const auto moved = [this, count] {
    nav.follow(count);
    requestUpdate();
  };
  buttonNavigator.onNextList(selectedIndex, count, moved, nav.pageRowsFor(count));
  buttonNavigator.onPreviousList(selectedIndex, count, moved, nav.pageRowsFor(count));
}

void MenuListActivity::navigateButtons() { handleNavigation(); }

void MenuListActivity::activateIndex(const int index) {
  selectedIndex = index;
  toggleCurrentItem();
}

void MenuListActivity::toggleCurrentItem() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) return;
  const auto& item = menuItems[selectedIndex];
  if (item.isSeparator) return;

  if (item.type == SettingType::ACTION) {
    if (item.action == SettingAction::Submenu) {
      openSubmenu(item);
      return;
    }
    onActionSelected(selectedIndex);
    return;
  }

  menuItems[selectedIndex].toggleValue();
  onSettingToggled(selectedIndex);
  requestUpdate();
}

std::string MenuListActivity::getItemValueString(int index) const { return menuItems[index].getDisplayValue(); }

void MenuListActivity::drawMenuList(const Rect& rect) {
  listRect = rect;
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    renderUi();
  }
}

void MenuListActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));
  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    const auto& item = menuItems[index];
    windowLabels[offset] =
        item.isSeparator && item.nameId != StrId::STR_NONE_OPT ? I18N.get(item.nameId) : item.getTitle();
    windowValues[offset] = getItemValueString(static_cast<int>(index));
    auto& row = windowItems[offset];
    row = {};
    row.label = windowLabels[offset].c_str();
    row.value = windowValues[offset].empty() ? nullptr : windowValues[offset].c_str();
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = !item.isSeparator;
    row.isHeader = item.isSeparator;
  }
}

void MenuListActivity::buildScreen(UiScreen& screen) {
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(listRect.y), static_cast<int16_t>(renderer.getScreenWidth() - (listRect.x + listRect.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (listRect.y + listRect.height)),
      static_cast<int16_t>(listRect.x)});

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void MenuListActivity::prepareSubmenus() { SettingInfo::prepareSubmenus(menuItems, submenuData); }

void MenuListActivity::openSubmenu(const SettingInfo& submenuEntry) {
  auto it = std::find_if(submenuData.begin(), submenuData.end(),
                         [&submenuEntry](const SettingInfo::SubmenuData& d) { return d.id == submenuEntry.nameId; });
  if (it == submenuData.end()) return;

  startActivityForResult(
      std::make_unique<SettingsSubmenuActivity>(renderer, mappedInput, submenuEntry.nameId, it->items),
      [this](const ActivityResult&) { requestUpdate(); });
}

void MenuListActivity::loop() { UiListActivity::loop(); }

ListRowTap::Result MenuListActivity::selectListRow(const int index) {
  // Separators are already excluded by the band (recorded non-selectable, the same exclusion
  // initMenuList()'s predicate makes for button navigation); declining here as well is belt and
  // braces for a list rebuilt between the render and the tap.
  if (index >= 0 && index < static_cast<int>(menuItems.size()) && menuItems[index].isSeparator) {
    return ListRowTap::Result::Rejected;
  }
  return ListRowTap::apply(index, static_cast<int>(menuItems.size()), selectedIndex);
}
