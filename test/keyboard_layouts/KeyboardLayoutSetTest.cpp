// keyboard_layouts: which layouts the language key cycles through, and which one
// a keyboard opens on, for each UI language.

#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardLayoutSet.h"

CrossPointSettings CrossPointSettings::instance;

// The real lib/I18n/I18n.cpp cannot link on the host -- it reaches into the flash
// language partition -- and KeyboardLayoutSet only ever asks for the language.
I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}
void I18n::setLanguage(const Language lang) { _language = lang; }

namespace {

using freeink::ui::KeyboardLayoutId;

// Restores English and an unconfigured set after each test, so the order they run in cannot matter.
class KeyboardLayoutSetTest : public ::testing::Test {
 protected:
  void TearDown() override {
    I18N.setLanguage(Language::EN);
    SETTINGS.keyboardLayouts = 0;
  }

  static uint16_t bitOf(const KeyboardLayoutId id) {
    for (uint8_t i = 0; i < keyboard_layouts::COUNT; ++i) {
      if (keyboard_layouts::ALL[i].id == id) return keyboard_layouts::bitAt(i);
    }
    return 0;
  }

  static int enabledCount() { return __builtin_popcount(keyboard_layouts::enabled()); }
};

TEST_F(KeyboardLayoutSetTest, EnglishGetsQwertyAloneAndNoLanguageKey) {
  I18N.setLanguage(Language::EN);
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn);
  EXPECT_EQ(enabledCount(), 1);
  // One layout: the key has nowhere to go, so next() stays put.
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::QwertyEn), KeyboardLayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutSetTest, EachLanguageWithALayoutOpensOnItAndReachesEnglish) {
  const struct {
    Language language;
    KeyboardLayoutId layout;
  } cases[] = {
      {Language::FR, KeyboardLayoutId::AzertyFr},   {Language::DE, KeyboardLayoutId::QwertzDe},
      {Language::ES, KeyboardLayoutId::SpanishEs},  {Language::RU, KeyboardLayoutId::CyrillicRu},
      {Language::UK, KeyboardLayoutId::CyrillicUk}, {Language::BE, KeyboardLayoutId::CyrillicBe},
      {Language::KK, KeyboardLayoutId::CyrillicKk},
  };
  for (const auto& c : cases) {
    I18N.setLanguage(c.language);
    EXPECT_EQ(keyboard_layouts::startingLayout(), c.layout) << static_cast<int>(c.language);
    EXPECT_EQ(enabledCount(), 2) << static_cast<int>(c.language);
    // The language key alternates between the two, starting from either.
    EXPECT_EQ(keyboard_layouts::next(c.layout), KeyboardLayoutId::QwertyEn) << static_cast<int>(c.language);
    EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::QwertyEn), c.layout) << static_cast<int>(c.language);
  }
}

TEST_F(KeyboardLayoutSetTest, LanguageWithoutALayoutFallsBackToQwerty) {
  // Polish, Czech, Italian... have no layout of their own: QWERTY alone.
  for (const Language language : {Language::PL, Language::CS, Language::IT, Language::VI}) {
    I18N.setLanguage(language);
    EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn) << static_cast<int>(language);
    EXPECT_EQ(enabledCount(), 1) << static_cast<int>(language);
  }
}

TEST_F(KeyboardLayoutSetTest, NoScriptTheUiFontsCannotDrawIsOffered) {
  // The SDK has Hebrew and Arabic; Inter UI has neither script.
  for (const auto& info : keyboard_layouts::ALL) {
    EXPECT_NE(info.id, KeyboardLayoutId::HebrewIl);
    EXPECT_NE(info.id, KeyboardLayoutId::ArabicAr);
  }
  // And a layout the table does not list still leads somewhere rather than nowhere.
  I18N.setLanguage(Language::RU);
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::HebrewIl), KeyboardLayoutId::CyrillicRu);
}

// ---------------------------------------------------------------- configured on the settings screen

TEST_F(KeyboardLayoutSetTest, EnglishUiWithCyrillicSwitchedOnGetsTheLanguageKey) {
  // The case the default cannot serve: an English interface, Russian books.
  I18N.setLanguage(Language::EN);
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::QwertyEn) | bitOf(KeyboardLayoutId::CyrillicRu);
  EXPECT_EQ(enabledCount(), 2);
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn);
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::QwertyEn), KeyboardLayoutId::CyrillicRu);
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::CyrillicRu), KeyboardLayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutSetTest, AConfiguredSetAlwaysKeepsALatinLayout) {
  // A hand-edited file can switch every Latin layout off; URL and password fields still need one.
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::CyrillicUk);
  EXPECT_TRUE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::QwertyEn));
  EXPECT_TRUE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::CyrillicUk));
  // A Latin layout other than English satisfies it without adding English.
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::QwertzDe) | bitOf(KeyboardLayoutId::CyrillicRu);
  EXPECT_FALSE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::QwertyEn));
}

TEST_F(KeyboardLayoutSetTest, TheUiLanguagesLayoutSwitchedOffIsNotOpenedOn) {
  I18N.setLanguage(Language::RU);
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::QwertyEn) | bitOf(KeyboardLayoutId::CyrillicUk);
  EXPECT_NE(keyboard_layouts::startingLayout(), KeyboardLayoutId::CyrillicRu);
  EXPECT_TRUE(keyboard_layouts::enabled() & bitOf(keyboard_layouts::startingLayout()));
}

TEST_F(KeyboardLayoutSetTest, BitsNamingNoLayoutAreIgnored) {
  // From a file written by a build with more layouts: only unknown bits reads as unconfigured.
  I18N.setLanguage(Language::FR);
  SETTINGS.keyboardLayouts = 0x8000;
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::AzertyFr);
  EXPECT_EQ(enabledCount(), 2);
}

}  // namespace
