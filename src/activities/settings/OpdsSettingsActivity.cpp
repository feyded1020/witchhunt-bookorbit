#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <memory>
#include <optional>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Name, URL, Username, Password.
// Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 4;
constexpr char INVALID_OPDS_URL_MESSAGE[] = "Enter a valid OPDS URL";
}  // namespace

OpdsSettingsActivity::OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const int serverIndex)
    : UiListActivity("OpdsSettings", renderer, mappedInput), serverIndex(serverIndex) {
  static_assert(BASE_ITEMS + 1 == MAX_MENU_ITEMS, "Name, URL, Username, Password and Delete");
  // Labels never change (unlike the values, which track editServer's fields
  // live), so they're set once here rather than every buildScreen() call.
  static constexpr StrId fieldNames[BASE_ITEMS] = {StrId::STR_SERVER_NAME, StrId::STR_OPDS_SERVER_URL,
                                                   StrId::STR_USERNAME, StrId::STR_PASSWORD};
  for (int i = 0; i < BASE_ITEMS; i++) {
    fieldRowItems[i].label = I18N.get(fieldNames[i]);
    fieldRowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fieldRowItems[BASE_ITEMS].label = tr(STR_DELETE_SERVER);
  fieldRowItems[BASE_ITEMS].actionValue = static_cast<int16_t>(BASE_ITEMS);
}

int OpdsSettingsActivity::getMenuItemCount() const {
  return isNewServer ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void OpdsSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  // All of this is read by the render task; set it under the lock so a pass that starts before
  // onEnter() returns never sees a half-copied server.
  RenderLock lock(*this);
  isNewServer = (serverIndex < 0);
  showSaveError = false;
  invalidUrlPopup = false;

  if (!isNewServer) {
    // Edit flow: copy the selected server into local editable state.
    // Changes are persisted field-by-field through saveServer().
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(serverIndex));
    if (server) {
      editServer = *server;
    } else {
      // Server was deleted between navigation and entering this screen — treat as new
      isNewServer = true;
      serverIndex = -1;
    }
  }
}

void OpdsSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens a keyboard or leaves the screen; a lingering flash would
  // gray an unrelated row.
  app.clearTapFlash();
  handleSelection(index);
}

bool OpdsSettingsActivity::saveServer() {
  // The store copies editServer, which only this task writes, so the save itself runs outside the
  // render lock (it is SD I/O). What the render task reads changes under the lock below.
  bool success = false;
  std::optional<size_t> insertedIndex;

  if (isNewServer) {
    // Create flow: first save inserts a new server record into the multi-server store.
    insertedIndex = OPDS_STORE.addServer(editServer);
    success = insertedIndex.has_value();
    if (!success) {
      LOG_ERR("OPS", "Failed to add OPDS server");
    }
  } else {
    // Edit flow: update the same server entry in-place.
    success = OPDS_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
    if (!success) {
      LOG_ERR("OPS", "Failed to update OPDS server at index %d", serverIndex);
    }
  }

  {
    RenderLock lock(*this);
    if (insertedIndex) {
      // After the first successful save, promote to an existing server so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewServer = false;
      serverIndex = static_cast<int>(*insertedIndex);
    }
    showSaveError = !success;
    if (success) invalidUrlPopup = false;
  }
  if (!success) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::commitField(std::string& field, const std::string& text) {
  {
    // The render task reads this string through fieldRowItems[].value, and the assignment may
    // reallocate it.
    RenderLock lock(*this);
    field = text;
  }
  saveServer();
  requestUpdate();
}

void OpdsSettingsActivity::handleSelection(const int index) {
  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (index == 0) {
    // Server Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) commitField(editServer.name, std::get<KeyboardResult>(result.data).text);
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERVER_NAME), editServer.name,
                                                OpdsServerStore::MAX_NAME_LENGTH, InputType::Text),
        handler);
  } else if (index == 1) {
    // Server URL
    const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
    auto handler = [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto normalizedUrl = OpdsServerValidation::normalizeUrl(std::get<KeyboardResult>(result.data).text);
      if (!normalizedUrl) {
        {
          RenderLock lock(*this);
          invalidUrlPopup = true;
        }
        requestUpdate();
        return;
      }
      {
        RenderLock lock(*this);
        invalidUrlPopup = false;
        editServer.url = *normalizedUrl;
      }
      saveServer();
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_SERVER_URL), prefillUrl,
                                                OpdsServerStore::MAX_URL_LENGTH, InputType::Url),
        handler);
  } else if (index == 2) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) commitField(editServer.username, std::get<KeyboardResult>(result.data).text);
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME), editServer.username,
                                                OpdsServerStore::MAX_USERNAME_LENGTH, InputType::Text),
        handler);
  } else if (index == 3) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) commitField(editServer.password, std::get<KeyboardResult>(result.data).text);
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD), editServer.password,
                                                OpdsServerStore::MAX_PASSWORD_LENGTH, InputType::Password),
        handler);
  } else if (index == BASE_ITEMS && !isNewServer) {
    // Delete flow is only available for existing servers. No confirmation.
    if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
      {
        RenderLock lock(*this);
        showSaveError = true;
      }
      requestUpdate();
      return;
    }
    finish();
  }
}

void OpdsSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // layoutListArea() puts its spacer before any band, so the URL hint band below keeps the long
  // form: margins, then the band, then the spacer.
  const Rect contentRect = listContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  // URL hint where the old sub-header band sat.
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), tr(STR_CALIBRE_URL_HINT), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // fieldRowItems' labels/actionValue were set once in the constructor; only
  // the live value pointers (pointing at editServer's own fields, no new
  // strings built) need refreshing here.
  fieldRowItems[0].value = editServer.name.empty() ? tr(STR_NOT_SET) : editServer.name.c_str();
  fieldRowItems[1].value = editServer.url.empty() ? tr(STR_NOT_SET) : editServer.url.c_str();
  fieldRowItems[2].value = editServer.username.empty() ? tr(STR_NOT_SET) : editServer.username.c_str();
  fieldRowItems[3].value = editServer.password.empty() ? tr(STR_NOT_SET) : "******";

  fui::ListProps props = listProps(screen);  // ACTION_ROW, touch input; buttons go through the ListController
  props.items = fieldRowItems;
  props.count = static_cast<uint16_t>(getMenuItemCount());
  props.valueInset = 8;  // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  addList(screen, props);
}

const char* OpdsSettingsActivity::headerTitle() const {
  // Reuse STR_OPDS_BROWSER as the "edit existing server" title.
  // New server creation uses STR_ADD_SERVER.
  return isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
}

const char* OpdsSettingsActivity::activePopup() const {
  if (invalidUrlPopup) return INVALID_OPDS_URL_MESSAGE;
  if (showSaveError) return tr(STR_ERROR_GENERAL_FAILURE);
  return nullptr;
}

void OpdsSettingsActivity::drawFooter() {
  UiListActivity::drawFooter();
  // On top of the finished frame, hints included. This runs inside UiListActivity::render(), whose
  // own displayBuffer() follows, so the popup must neither ship (that would swap the buffers, and
  // the render's ship would then show the stale one) nor re-seed from the displayed frame (that
  // would discard the frame this pass just composed).
  const char* message = activePopup();
  if (message) GUI.drawPopup(renderer, message, /*overlayDisplayedFrame=*/false, PopupShip::Caller);
}
