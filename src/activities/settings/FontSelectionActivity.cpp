#include "FontSelectionActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void FontSelectionActivity::onEnter() {
  const int fontCount = static_cast<int>(fontFamilyOptionCount()) + (overrideSetting ? 1 : 0);
  int selectedIndex = overrideSetting ? static_cast<int>(overrideSetting->getEnumSelectedIndex())
                                      : static_cast<int>(target == Target::TXT ? txtFontFamilyDynamicGetter(nullptr)
                                                                               : fontFamilyDynamicGetter(nullptr));
  if (selectedIndex >= fontCount) selectedIndex = 0;

  rowLabels.clear();
  rowItems.clear();
  rowLabels.reserve(fontCount);
  rowItems.reserve(fontCount);
  for (int index = 0; index < fontCount; ++index) {
    rowLabels.push_back(overrideSetting ? overrideSetting->getEnumOptionLabel(static_cast<uint8_t>(index))
                                        : fontFamilyOptionLabel(static_cast<uint8_t>(index)));
  }
  for (int index = 0; index < fontCount; ++index) {
    fui::ListItem item;
    item.label = rowLabels[index].c_str();
    if (index == selectedIndex) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(index);
    rowItems.push_back(item);
  }

  RenderLock lock(*this);
  UiListActivity::onEnter();
  nav.selected = selectedIndex;
  updatePreviewFontLocked(selectedIndex);
}

void FontSelectionActivity::onExit() {
  sdFontSystem.unload(renderer);
  UiListActivity::onExit();
}

const char* FontSelectionActivity::headerTitle() const {
  if (target == Target::TXT) return tr(STR_TXT_FONT_FAMILY);
  return tr(STR_FONT_FAMILY);
}

uint8_t FontSelectionActivity::selectedFontSize() const {
  return target == Target::TXT ? SETTINGS.txtFontSize : SETTINGS.fontSize;
}

int FontSelectionActivity::previewOptionIndex(const int index) const {
  if (!overrideSetting) return index;
  if (index == 0) return static_cast<int>(fontFamilyDynamicGetter(nullptr));
  return index - 1;
}

void FontSelectionActivity::updatePreviewFont(const int index) {
  if (index < 0 || index >= listCount()) return;

  RenderLock lock(*this);
  updatePreviewFontLocked(index);
}

void FontSelectionActivity::updatePreviewFontLocked(const int index) {
  const int optionIndex = previewOptionIndex(index);
  const uint32_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  const uint32_t largestBefore = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  int previewFontId = 0;
  if (optionIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    sdFontSystem.unload(renderer);
    previewFontId = CrossPointSettings::getBuiltinReaderFontId(static_cast<uint8_t>(optionIndex), selectedFontSize());
  } else {
    const auto& families = sdFontSystem.registry().getFamilies();
    const size_t familyIndex = static_cast<size_t>(optionIndex - CrossPointSettings::BUILTIN_FONT_COUNT);
    if (familyIndex < families.size()) {
      const char* familyName = families[familyIndex].name.c_str();
      sdFontSystem.ensureLoaded(renderer, familyName, selectedFontSize());
      previewFontId = sdFontSystem.resolveFontId(familyName, selectedFontSize());
    }
  }
  if (previewFontId == 0) {
    previewFontId = CrossPointSettings::getBuiltinReaderFontId(CrossPointSettings::NOTOSANS, selectedFontSize());
  }
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, previewFontId);

  LOG_DBG("FPRV", "font=%d free=%lu->%lu largest=%lu->%lu", index, static_cast<unsigned long>(freeBefore),
          static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT)),
          static_cast<unsigned long>(largestBefore),
          static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT)));
}

void FontSelectionActivity::onSelectionChanged(const int index) { updatePreviewFont(index); }

void FontSelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  if (overrideSetting) {
    overrideSetting->setEnumSelectedIndex(static_cast<uint8_t>(index));
  } else if (target == Target::TXT) {
    txtFontFamilyDynamicSetter(nullptr, static_cast<uint8_t>(index));
  } else {
    fontFamilyDynamicSetter(nullptr, static_cast<uint8_t>(index));
  }
  finish();
}

void FontSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::TextAreaProps preview;
  preview.text = tr(STR_FONT_PREVIEW_SAMPLE);
  preview.style = screen.theme().titleText;
  preview.style.font = fui::GfxRendererTarget::FONT_TITLE;
  preview.showCaret = false;
  screen.textArea(preview, static_cast<int16_t>(screen.body().height * 30 / 100));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
