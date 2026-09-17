#pragma once

#include <cstdint>

#include "I18nKeys.h"
/**
 * Internationalization (i18n) system for CrossPoint Reader
 */

class I18n {
 public:
  static I18n& getInstance();

  // Disable copy
  I18n(const I18n&) = delete;
  I18n& operator=(const I18n&) = delete;

  // Get localized string by ID
  const char* get(StrId id) const;

  const char* operator[](StrId id) const { return get(id); }

  Language getLanguage() const { return _language; }
  void setLanguage(Language lang);
  const char* getLanguageCode(Language lang) const;
  const char* getLanguageName(Language lang) const;

  void saveSettings();
  void loadSettings();

  // Get all unique characters used in a specific language
  // Returns a sorted string of unique characters
  static const char* getCharacterSet(Language lang);

 private:
  I18n() : _language(Language::EN), _active(englishStrings()) {}

  // Read the stored language code. Does not resolve the strings for it.
  void loadLanguageSetting();

  // Point _active at the strings for _language: English straight from rodata,
  // anything else from the flash language pack — building that pack first if the
  // slot does not already hold this language and firmware build. Falls back to
  // English on any failure, so a missing or unwritable pack degrades to readable
  // English rather than to blank rows.
  void refreshActiveStrings();

  Language _language;

  // The resolved (data, offsets) pair for _language.
  //
  // Cached rather than resolved per lookup: for a pack language that means
  // reading a flash header and comparing a stamp, and tr() is called several
  // times for every row drawn. For English it points at rodata exactly as
  // before. Either way get() is two loads and an add.
  LangStrings _active;
};

// Convenience macros
#define tr(id) I18n::getInstance().get(StrId::id)
#define I18N I18n::getInstance()
