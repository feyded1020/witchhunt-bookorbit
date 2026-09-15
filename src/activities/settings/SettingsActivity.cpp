#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "SettingActionDispatch.h"
#include "SettingsList.h"
#include "SettingsSubmenuActivity.h"
#include "SliderSettingPicker.h"
#include "TouchUi.h"
#include "activities/SliderPickerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

bool SettingsActivity::isListItemSelectable(int settingIdx) const {
  return settingIdx >= 0 && settingIdx < settingsCount && !(*currentSettings)[settingIdx].isSeparator;
}

void SettingsActivity::onEnter() {
  Activity::onEnter();
  needsHalfRefresh = true;

  // Build per-category vectors from the shared settings list.
  // addTo tracks the last subcategory per vector and automatically inserts a separator
  // row whenever a setting carries a new subcategory label.
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  submenuData.clear();
  displaySettings.reserve(20);
  readerSettings.reserve(30);
  controlsSettings.reserve(8);
  systemSettings.reserve(20);
  submenuData.reserve(4);

  // Rows go in plain; the subcategory separators are inserted at the END, once prepareSubmenus
  // has moved whatever it is going to move into submenus.
  //
  // Inserting them here instead put headings in front of rows that were about to leave. The
  // gesture rows are the case that showed it: four groups' worth of headings went into the
  // Controls tab, then all twenty rows moved into the Gesture actions submenu, and three of the
  // four headings stayed behind with nothing underneath them. Separators describe a list, so
  // they can only be computed once the list has stopped changing -- which is the order
  // MenuListActivity has always used.
  auto addTo = [](std::vector<SettingInfo>& vec, const SettingInfo& s) { vec.push_back(s); };
  auto addToMoved = [](std::vector<SettingInfo>& vec, SettingInfo s) { vec.push_back(std::move(s)); };

  bool sawReaderFontSection = false;
  bool insertedFontDownload = false;
  bool sawIncludeBetaUpdates = false;
  SettingInfo includeBetaUpdatesSetting{};

  auto insertFontDownloadBelowFontSection = [&]() {
    auto fontDownload = SettingInfo::Action(StrId::STR_FONT_MANAGER, SettingAction::DownloadFonts);
    fontDownload.withSubcategory(StrId::STR_MENU_READER_FONT);
    addToMoved(readerSettings, std::move(fontDownload));
    insertedFontDownload = true;
  };

  for (const auto& setting : getSettingsList()) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_SYSTEM &&
        (setting.nameId == StrId::STR_USE_CLOCK || setting.nameId == StrId::STR_CLOCK_FORMAT ||
         setting.nameId == StrId::STR_TIMEZONE)) {
      continue;
    }
    // Enrich font-family entries with SD card families discovered at boot.
    // The list itself is a namespace-static; we only mutate our local copy here.
    SettingInfo enriched = setting;
    if (setting.key &&
        (std::strcmp(setting.key, "fontFamily") == 0 || std::strcmp(setting.key, "txtFontFamily") == 0)) {
      const uint8_t n = fontFamilyOptionCount();
      enriched.enumLabels.clear();
      enriched.enumLabels.reserve(n);
      for (uint8_t i = 0; i < n; i++) enriched.enumLabels.push_back(fontFamilyOptionLabel(i));
    }
    if (enriched.nameId == StrId::STR_INCLUDE_BETA_UPDATES) {
      includeBetaUpdatesSetting = enriched;
      sawIncludeBetaUpdates = true;
      continue;
    }
    const bool isReaderFontEntry =
        enriched.category == StrId::STR_CAT_READER &&
        (enriched.submenu == StrId::STR_MENU_READER_FONT || enriched.submenu == StrId::STR_MENU_TXT_FONT);

    if (!insertedFontDownload && sawReaderFontSection && !isReaderFontEntry) {
      insertFontDownloadBelowFontSection();
    }

    if (enriched.category == StrId::STR_CAT_DISPLAY) {
      addTo(displaySettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_READER) {
      addTo(readerSettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_CONTROLS) {
      addTo(controlsSettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_SYSTEM) {
      addTo(systemSettings, enriched);
    }
    if (isReaderFontEntry) sawReaderFontSection = true;

    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  }

  if (!insertedFontDownload && sawReaderFontSection) {
    insertFontDownloadBelowFontSection();
  }

  // Device-only ACTION items — subcategory drives separator insertion automatically.
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  controlsSettings.insert(controlsSettings.begin(), SettingInfo::Separator(StrId::STR_MENU_BTN_PHYSICAL));

  // Button Actions overview lives at the end of the Button Actions section (same subcategory as
  // the per-button submenus, so no new separator is inserted).
  addToMoved(controlsSettings,
             std::move(SettingInfo::Action(StrId::STR_BTN_ACTIONS_OVERVIEW, SettingAction::ButtonActionsOverview)
                           .withSubcategory(StrId::STR_MENU_BTN_ACTIONS)));

#if CP_TOUCH_UI
  // The same thing for touch: where the zones are and what each currently does. Sits beside
  // the gesture rows it summarises. Inside the CP_TOUCH_UI bracket with every other touch-UI
  // row, so a non-touch build does not construct a SettingInfo it will only delete again --
  // the runtime requiring() below is for a multi-board binary, not a substitute for the gate.
  addToMoved(controlsSettings,
             std::move(SettingInfo::Action(StrId::STR_GEST_ACTIONS_OVERVIEW, SettingAction::GestureActionsOverview)
                           .withSubcategory(StrId::STR_MENU_GESTURE_ACTIONS)
                           .requiring(SettingRequires::TouchPanel)));
#endif  // CP_TOUCH_UI

  addToMoved(readerSettings, SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  addToMoved(systemSettings, SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_CLOCK_SETTINGS, SettingAction::ClockSettings)
                                           .withSubcategory(StrId::STR_MENU_SYS_TOOLS)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_WEATHER_SETTINGS, SettingAction::Weather)
                                           .withSubcategory(StrId::STR_MENU_SYS_TOOLS)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_SCREEN_REPAIR, SettingAction::ScreenRepair)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));

  addToMoved(systemSettings,
             std::move(SettingInfo::Separator(StrId::STR_SYSTEM_UPDATE_TYPE1).withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates)
                                           .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  if (sawIncludeBetaUpdates) {
    addToMoved(systemSettings, std::move(includeBetaUpdatesSetting.withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  }
  addToMoved(systemSettings,
             std::move(SettingInfo::Separator(StrId::STR_SYSTEM_UPDATE_TYPE2).withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStats)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  addToMoved(systemSettings,
             std::move(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate)
                           .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  // addToMoved(systemSettings,
  //            std::move(SettingInfo::Action(StrId::STR_SWITCH_TO_USB_DRIVE, SettingAction::SwitchToUsbDrive)
  //                          .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_SYSTEM_INFO, SettingAction::SystemInfo)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  // Next to System Information, which is where anyone chasing a "the power button did
  // nothing" report already looks.
  addToMoved(systemSettings, std::move(SettingInfo::Action(StrId::STR_BOOT_DIAGNOSTICS, SettingAction::BootDiagnostics)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));

  SettingInfo::prepareSubmenus(displaySettings, submenuData);
  SettingInfo::prepareSubmenus(readerSettings, submenuData);
  SettingInfo::prepareSubmenus(controlsSettings, submenuData);
  SettingInfo::prepareSubmenus(systemSettings, submenuData);

  // Now that the tabs hold exactly the rows they will show, group them. prepareSubmenus has done
  // the same for each submenu's own rows.
  SettingInfo::insertSubcategorySeparators(displaySettings);
  SettingInfo::insertSubcategorySeparators(readerSettings);
  SettingInfo::insertSubcategorySeparators(controlsSettings);
  SettingInfo::insertSubcategorySeparators(systemSettings);

  resetUi();
  app.on(ACTION_TAB, &SettingsActivity::onTabEvent, this);
  app.on(ACTION_ROW, &SettingsActivity::onRowEvent, this);
  app.setScreen(&SettingsActivity::settingsScreen, this);

  // Reset selection to the first category's tab.
  selectedSettingIndex = 0;
  enterCategory(0);

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  closeRouting();
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      syncListSelection();
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return;
  }

  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? listNav.pageRows() : -listNav.pageRows();
    if (listNav.scrollBy(delta, settingsCount)) requestUpdate();
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1,
                                                      [this](int i) { return i == 0 || isListItemSelectable(i - 1); });
    syncListSelection();
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(
        selectedSettingIndex, settingsCount + 1, [this](int i) { return i == 0 || isListItemSelectable(i - 1); });
    syncListSelection();
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    enterCategory(selectedCategoryIndex);
  }
}

void SettingsActivity::enterCategory(const int categoryIndex) {
  selectedCategoryIndex = categoryIndex;
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  listNav.reset(selectedSettingIndex - 1);
  listTapActivation.reset();
}

void SettingsActivity::syncListSelection() {
  listNav.selected = selectedSettingIndex - 1;
  if (selectedSettingIndex > 0) listNav.follow(settingsCount);
}

void SettingsActivity::settingsScreen(UiScreen& screen, void* user) {
  static_cast<SettingsActivity*>(user)->buildSettingsScreen(screen);
}

void SettingsActivity::onTabEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SettingsActivity*>(user);
  if (event.value < 0 || event.value >= categoryCount) return;
  self->selectedSettingIndex = 0;
  self->enterCategory(event.value);
  self->app.clearTapFlash();
  self->requestUpdate();
}

void SettingsActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SettingsActivity*>(user);
  if (event.value < 0 || event.value >= self->settingsCount) return;
  self->handleRowTouch(event.value);
}

void SettingsActivity::handleRowTouch(const int index) {
  const auto result = listTapActivation.applyPreference(
      index, selectListRow(index), SETTINGS.touchListActivation == CrossPointSettings::TOUCH_LIST_ACTIVATE_IMMEDIATELY);
  if (result == ListRowTap::Result::Rejected) return;
  syncListSelection();
  if (result == ListRowTap::Result::Selected) {
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  toggleCurrentSetting();
  requestUpdate();
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  if (setting.isSeparator) return;

  if (setting.usesSelectorActivity) {
    auto selector = createSelectorActivity(setting, renderer, mappedInput);
    if (selector) {
      startActivityForResult(std::move(selector), [this](const ActivityResult&) {
        CrossPointSettings::normalizeDependentSettings(SETTINGS);
        SETTINGS.saveToFile();
        needsHalfRefresh = true;
      });
    }
    return;
  }

  SliderPickerActivity::Config sliderCfg;
  if (setting.type == SettingType::ACTION && SliderSetting::configFor(setting.action, sliderCfg)) {
    const SettingAction sliderAction = setting.action;
    startActivityForResult(std::make_unique<SliderPickerActivity>(renderer, mappedInput, std::move(sliderCfg)),
                           [this, sliderAction](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               if (const auto* pr = std::get_if<PercentResult>(&result.data)) {
                                 SliderSetting::apply(sliderAction, static_cast<uint8_t>(pr->percent));
                                 SETTINGS.saveToFile();
                               }
                             }
                             needsHalfRefresh = true;
                           });
    return;
  }

  if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult& result) {
      CrossPointSettings::normalizeDependentSettings(SETTINGS);
      SETTINGS.saveToFile();
      needsHalfRefresh = true;
      const auto* menuResult = std::get_if<MenuResult>(&result.data);
      if (menuResult && menuResult->action != -1) {
        auto activity = createActivityForAction(static_cast<SettingAction>(menuResult->action), renderer, mappedInput);
        if (activity) {
          startActivityForResult(std::move(activity), [this](const ActivityResult&) {
            CrossPointSettings::normalizeDependentSettings(SETTINGS);
            SETTINGS.saveToFile();
            needsHalfRefresh = true;
          });
        }
      }
    };

    if (setting.action == SettingAction::Submenu) {
      auto it = std::find_if(submenuData.begin(), submenuData.end(),
                             [&setting](const SettingInfo::SubmenuData& d) { return d.id == setting.nameId; });
      if (it != submenuData.end()) {
        startActivityForResult(
            std::make_unique<SettingsSubmenuActivity>(renderer, mappedInput, setting.nameId, it->items),
            [this](const ActivityResult&) { needsHalfRefresh = true; });
      }
    } else {
      auto activity = createActivityForAction(setting.action, renderer, mappedInput);
      if (activity) startActivityForResult(std::move(activity), resultHandler);
    }
    return;
  }

  setting.toggleValue();
  CrossPointSettings::normalizeDependentSettings(SETTINGS);
  SETTINGS.saveToFile();
}

void SettingsActivity::materializeListWindow() {
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(listNav.top, settingsCount)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(settingsCount - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));
  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    const auto& setting = (*currentSettings)[index];
    windowLabels[offset] =
        setting.isSeparator && setting.nameId != StrId::STR_NONE_OPT ? I18N.get(setting.nameId) : setting.getTitle();
    windowValues[offset] = setting.getDisplayValue();
    auto& row = windowItems[offset];
    row = {};
    row.label = windowLabels[offset].c_str();
    row.value = windowValues[offset].empty() ? nullptr : windowValues[offset].c_str();
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = !setting.isSeparator;
    row.isHeader = setting.isSeparator;
  }
}

void SettingsActivity::buildSettingsScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  fui::TabItem tabs[categoryCount];
  for (int i = 0; i < categoryCount; ++i) {
    tabs[i].label = I18N.get(categoryNames[i]);
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = selectedCategoryIndex == i;
  }

  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = categoryCount;
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  tabProps.text = screen.theme().bodyText;
  tabProps.tabInset = fui::Insets{};
  tabProps.contentInset = fui::Insets{};

  // Adapted from CrossInk's FreeInkUI SettingsActivity tab composition at
  // commit cd4b122ef (MIT): https://github.com/uxjulia/crossink
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.background = fui::Paint::solid(fui::Color::White);
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  tabStyles.normal.border = fui::Paint::solid(fui::Color::Black);
  tabStyles.normal.borderWidth = 1;
  tabStyles.selected.background =
      selectedSettingIndex == 0 ? fui::Paint::solid(fui::Color::Black) : fui::Paint::dither(fui::Color::LightGray);
  tabStyles.selected.foreground = fui::Paint::solid(selectedSettingIndex == 0 ? fui::Color::White : fui::Color::Black);
  tabStyles.selected.border = fui::Paint::solid(fui::Color::Black);
  tabStyles.selected.borderWidth = 1;
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;

  fui::tabBar(screen.frame(), screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight)), tabProps);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.count = static_cast<uint16_t>(settingsCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    rowHeight = static_cast<int16_t>(metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  listNav.selected = selectedSettingIndex - 1;
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, settingsCount, props);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_SETTINGS_TITLE), CROSSPOINT_VERSION);

  renderUi();
  for (int pass = 0; listNav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                   tr(STR_SETTINGS_TITLE), CROSSPOINT_VERSION);
    renderUi();
  }

  // Draw help text
  const auto confirmLabel = (selectedSettingIndex == 0)
                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
                                : tr(STR_TOGGLE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool halfRefresh = gpio.deviceIsX3() && needsHalfRefresh;
  needsHalfRefresh = false;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

ListRowTap::Result SettingsActivity::selectListRow(const int index) {
  // Compare in the list's zero-based frame; selectedSettingIndex reserves zero for tab focus.
  int selection = selectedSettingIndex - 1;
  const auto result = ListRowTap::apply(index, settingsCount, selection);
  if (result != ListRowTap::Result::Rejected) selectedSettingIndex = selection + 1;
  return result;
}

bool SettingsActivity::pageList(const ListPageDirection direction) {
  if (settingsCount <= 0) return false;

  const int pageSize = listNav.pageRowsFor(settingsCount);
  const int current = std::max(0, selectedSettingIndex - 1);
  int target = direction == ListPageDirection::Forward
                   ? ButtonNavigator::nextPageIndex(current, settingsCount, pageSize)
                   : ButtonNavigator::previousPageIndex(current, settingsCount, pageSize);
  const auto selectable = [this](const int index) { return isListItemSelectable(index); };
  if (!selectable(target)) {
    target = direction == ListPageDirection::Forward
                 ? ButtonNavigator::nextIndex(target, settingsCount, selectable)
                 : ButtonNavigator::previousIndex(target, settingsCount, selectable);
  }

  selectedSettingIndex = target + 1;
  syncListSelection();
  listTapActivation.reset();
  requestUpdate();
  return true;
}
