#include "BookOrbitSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "BookOrbitAuthActivity.h"
#include "BookOrbitCredentialStore.h"
#include "MappedInputManager.h"
#include "SliderSettingPicker.h"
#include "activities/SliderPickerActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// "" for the SD root, otherwise a leading and no trailing slash, so folder + "/file.epub" is a path.
std::string normalizeDownloadFolder(std::string folder) {
  while (!folder.empty() && (folder.front() == ' ' || folder.front() == '\t')) folder.erase(folder.begin());
  while (!folder.empty() && (folder.back() == ' ' || folder.back() == '\t')) folder.pop_back();
  if (folder.empty() || folder == "/") return "";
  if (folder.front() != '/') folder.insert(folder.begin(), '/');
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  return folder;
}
}  // namespace

BookOrbitSettingsActivity::BookOrbitSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : MenuListActivity("BookOrbitSettings", renderer, mappedInput) {
  buildMenuItems();
}

void BookOrbitSettingsActivity::buildMenuItems() {
  menuItems.reserve(9);
  // Server URL, Username, Password: ACTION items with custom value display
  menuItems.push_back(SettingInfo::Action(StrId::STR_BOOKORBIT_SERVER_URL, SettingAction::None)
                          .withSubcategory(StrId::STR_MENU_KOSYNC_SERVER));
  menuItems.push_back(SettingInfo::Action(StrId::STR_USERNAME, SettingAction::None));
  menuItems.push_back(SettingInfo::Action(StrId::STR_PASSWORD, SettingAction::None));

  // No document-matching option: BookOrbit always matches by the binary partial-MD5 hash.
  menuItems.push_back(SettingInfo::DynamicEnum(
                          StrId::STR_KO_SYNC_CONFLICT, {StrId::STR_KO_ASK_EVERY_TIME, StrId::STR_KO_SMART_SYNC},
                          [](const void*) -> uint8_t { return static_cast<uint8_t>(BOOKORBIT_STORE.getSyncBehavior()); },
                          [](void*, uint8_t v) {
                            BOOKORBIT_STORE.setSyncBehavior(v ? BookOrbitSyncBehavior::SMART
                                                              : BookOrbitSyncBehavior::ASK_EVERY_TIME);
                            BOOKORBIT_STORE.saveToFile();
                          })
                          .withSubcategory(StrId::STR_MENU_KOSYNC_BEHAVIOR));
  menuItems.push_back(SettingInfo::Toggle(StrId::STR_KO_SYNC_ON_BOOK_CLOSE, &CrossPointSettings::koSyncOnBookClose,
                                          "koSyncOnBookClose"));
  menuItems.push_back(SettingInfo::Action(StrId::STR_KO_MIN_SESSION_PAGES, SettingAction::KOSyncMinPagesPicker));

  // Catalog download folder ("" = SD root); created on first download.
  menuItems.push_back(SettingInfo::Action(StrId::STR_OPDS_DOWNLOAD_FOLDER, SettingAction::None)
                          .withSubcategory(StrId::STR_BOOKORBIT_CATALOG));

  menuItems.push_back(
      SettingInfo::Action(StrId::STR_AUTHENTICATE, SettingAction::None).withSubcategory(StrId::STR_MENU_KOSYNC_AUTH));
}

std::string BookOrbitSettingsActivity::getItemValueString(int index) const {
  const auto& item = menuItems[index];

  if (item.nameId == StrId::STR_USERNAME) {
    auto username = BOOKORBIT_STORE.getUsername();
    return username.empty() ? std::string(tr(STR_NOT_SET)) : username;
  }
  if (item.nameId == StrId::STR_PASSWORD) {
    return BOOKORBIT_STORE.getPassword().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
  }
  if (item.nameId == StrId::STR_BOOKORBIT_SERVER_URL) {
    auto serverUrl = BOOKORBIT_STORE.getServerUrl();
    // BookOrbit is self-hosted only: there is no default server to fall back to.
    return serverUrl.empty() ? std::string(tr(STR_NOT_SET)) : serverUrl;
  }
  if (item.nameId == StrId::STR_OPDS_DOWNLOAD_FOLDER) {
    const std::string& folder = BOOKORBIT_STORE.getDownloadFolder();
    return folder.empty() ? std::string(tr(STR_OPDS_SD_ROOT)) : folder;
  }
  if (item.nameId == StrId::STR_KO_MIN_SESSION_PAGES) {
    const uint8_t v = SETTINGS.koSyncMinSessionPages;
    // Zero means no minimum, not "off" — the Auto-Push toggle above is what turns it off.
    if (v == 0) return std::string(tr(STR_ALWAYS));
    return std::to_string(v) + tr(STR_PAGES_SUFFIX);
  }
  if (item.nameId == StrId::STR_AUTHENTICATE) {
    return BOOKORBIT_STORE.hasCredentials() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
  }

  return MenuListActivity::getItemValueString(index);
}

void BookOrbitSettingsActivity::onActionSelected(int index) {
  const auto& item = menuItems[index];

  if (item.nameId == StrId::STR_USERNAME) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKORBIT_USERNAME),
                                                                   BOOKORBIT_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               BOOKORBIT_STORE.setCredentials(kb.text, BOOKORBIT_STORE.getPassword());
                               BOOKORBIT_STORE.saveToFile();
                             }
                           });
  } else if (item.nameId == StrId::STR_PASSWORD) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKORBIT_PASSWORD),
                                                BOOKORBIT_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            BOOKORBIT_STORE.setCredentials(BOOKORBIT_STORE.getUsername(), kb.text);
            BOOKORBIT_STORE.saveToFile();
          }
        });
  } else if (item.nameId == StrId::STR_BOOKORBIT_SERVER_URL) {
    const std::string currentUrl = BOOKORBIT_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOKORBIT_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               BOOKORBIT_STORE.setServerUrl(urlToSave);
                               BOOKORBIT_STORE.saveToFile();
                             }
                           });
  } else if (item.nameId == StrId::STR_KO_MIN_SESSION_PAGES) {
    SliderPickerActivity::Config cfg;
    if (SliderSetting::configFor(SettingAction::KOSyncMinPagesPicker, cfg)) {
      startActivityForResult(std::make_unique<SliderPickerActivity>(renderer, mappedInput, std::move(cfg)),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 if (const auto* pr = std::get_if<PercentResult>(&result.data)) {
                                   SliderSetting::apply(SettingAction::KOSyncMinPagesPicker,
                                                        static_cast<uint8_t>(pr->percent));
                                   SETTINGS.saveToFile();
                                 }
                               }
                               requestUpdate();
                             });
    }
  } else if (item.nameId == StrId::STR_OPDS_DOWNLOAD_FOLDER) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_DOWNLOAD_FOLDER),
                                                BOOKORBIT_STORE.getDownloadFolder(), 64, InputType::Text),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            BOOKORBIT_STORE.setDownloadFolder(normalizeDownloadFolder(kb.text));
            BOOKORBIT_STORE.saveToFile();
          }
        });
  } else if (item.nameId == StrId::STR_AUTHENTICATE) {
    if (!BOOKORBIT_STORE.hasCredentials()) return;
    startActivityForResult(std::make_unique<BookOrbitAuthActivity>(renderer, mappedInput),
                           [](const ActivityResult&) {});
  }
}

void BookOrbitSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_BOOKORBIT_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing * 2;
  drawMenuList(Rect{contentRect.x, contentTop, contentRect.width, contentHeight});

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
