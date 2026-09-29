#pragma once

// Which keyboard layouts the language key cycles through, and which one a
// keyboard opens on.
//
// Ported from crosspoint-reader (PR #2858 by winst0niuss, with Uri Tauber).
//
// The set is SETTINGS.keyboardLayouts, chosen on the Keyboard Layouts screen.
// Until that is configured it is the UI language's layout plus English, so a
// reader whose interface is in Russian gets ЙЦУКЕН and QWERTY without visiting
// the screen, and one reading Russian books under an English interface switches
// ЙЦУКЕН on there.
//
// Only the layouts our UI fonts can draw are listed. The SDK also has Hebrew and
// Arabic, but Inter UI carries neither script and the UI text path does no bidi
// or shaping, so offering them would put boxes on the keys.

#include <I18n.h>
#include <components/keyboard/keyboard.h>

#include <cstdint>

namespace keyboard_layouts {

struct LayoutInfo {
  freeink::ui::KeyboardLayoutId id;
  Language language;
};

// Table position is the bit a layout occupies in the enabled() mask.
inline constexpr LayoutInfo ALL[] = {
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::EN},
    {freeink::ui::KeyboardLayoutId::AzertyFr, Language::FR},
    {freeink::ui::KeyboardLayoutId::QwertzDe, Language::DE},
    {freeink::ui::KeyboardLayoutId::SpanishEs, Language::ES},
    {freeink::ui::KeyboardLayoutId::CyrillicRu, Language::RU},
    {freeink::ui::KeyboardLayoutId::CyrillicUk, Language::UK},
    {freeink::ui::KeyboardLayoutId::CyrillicBe, Language::BE},
    {freeink::ui::KeyboardLayoutId::CyrillicKk, Language::KK},
};
inline constexpr uint8_t COUNT = sizeof(ALL) / sizeof(ALL[0]);
static_assert(COUNT <= 16, "the enabled-layout mask is uint16_t");

inline constexpr uint16_t bitAt(const uint8_t i) { return static_cast<uint16_t>(1u << i); }
// The layouts that can type a URL or a Wi-Fi password. One of them is always enabled.
inline constexpr uint16_t LATIN_BITS = bitAt(0) | bitAt(1) | bitAt(2) | bitAt(3);

// Bit i set: ALL[i] is reachable. Always includes a Latin layout, which is what
// URL and password fields need whatever else is chosen.
uint16_t enabled();

// The UI language's layout (English when the language has none), or the next
// enabled one when that has been switched off.
freeink::ui::KeyboardLayoutId startingLayout();

// The enabled layout after `current`, wrapping; `current` when it is the only one.
freeink::ui::KeyboardLayoutId next(freeink::ui::KeyboardLayoutId current);

}  // namespace keyboard_layouts
