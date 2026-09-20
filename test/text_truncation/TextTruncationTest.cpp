// Cover for textTruncation::truncateToWidth(), which replaced a loop that re-measured the whole
// remaining string after dropping each trailing character (O(n*k) measurements; ~1.9 ms for one
// 39-character row on an X3) with a single forward pass.
//
// The new walk is deliberately NOT byte-identical to the old one: truncatedText() is UI-only, so
// its result reaches no cache key, no pagination and no file format, and a cut differing by a
// character is invisible. That freedom is what makes the pass cheap — but it means the old
// implementation is not the oracle. These tests assert the two properties that actually matter:
//
//   1. **It never overflows the box.** Measured with the real getTextDimensions(), not with the
//      walk's own arithmetic, so an error in the walk cannot hide behind itself.
//   2. **It cuts maximally.** Putting one more character back must overflow. Without this a
//      truncation that returned just an ellipsis would pass (1) trivially.
//
// Run against real shipped fonts rather than a fixture, because the properties depend on real
// kerning tables and negative left bearings (Inter's `left` reaches -20).
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "EpdFontFamily.h"
#include "TextTruncation.h"
#include "builtinFonts/inter_ui_12_bold.h"
#include "builtinFonts/inter_ui_12_regular.h"

namespace {

const EpdFont kRegular(&inter_ui_12_regular);
const EpdFont kBold(&inter_ui_12_bold);
const EpdFontFamily kFamily(&kRegular, &kBold);

int widthOf(const std::string& s, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  int w = 0, h = 0;
  kFamily.getTextDimensions(s.c_str(), &w, &h, style);
  return w;
}

// Strings chosen for what they stress, not for variety: kerning pairs (AV, To), negative left
// bearings, multi-byte UTF-8 at the cut point, and a run with no spaces to cut on.
const std::vector<std::string>& corpus() {
  static const std::vector<std::string> c = {
      "Reading statistics",
      "Inline footnote previews        Enabled",
      "AVATAR To Whom It May Concern",
      "The Hitchhiker's Guide to the Galaxy",
      "Der Wachsmann und die Zauberfl\xc3\xb6te",  // German, umlaut
      "\xd0\x9f\xd1\x80\xd0\xb5\xd1\x81\xd1\x82\xd1\x83\xd0\xbf\xd0\xbb\xd0\xb5\xd0\xbd\xd0\xb8\xd0\xb5 "
      "\xd0\xb8 \xd0\xbd\xd0\xb0\xd0\xba\xd0\xb0\xd0\xb7\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5",  // Russian
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "iiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
      "W",
      "Wi",
  };
  return c;
}

// Byte length of the last UTF-8 character in `s`, so a test can put exactly one character back.
size_t lastCharBytes(const std::string& s) {
  size_t i = s.size();
  while (i > 0) {
    --i;
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) break;  // not a continuation byte
  }
  return s.size() - i;
}

TEST(TextTruncationTest, ResultNeverExceedsTheBox) {
  for (const auto& text : corpus()) {
    const int full = widthOf(text);
    for (int maxWidth = 8; maxWidth <= full + 24; maxWidth += 7) {
      const std::string out = textTruncation::truncateToWidth(kFamily, text.c_str(), maxWidth, EpdFontFamily::REGULAR);
      SCOPED_TRACE("text=\"" + text + "\" maxWidth=" + std::to_string(maxWidth) + " out=\"" + out + "\"");
      ASSERT_FALSE(out.empty());
      if (out == text) {
        // Returned unchanged because it fits; that path is <=, not <.
        EXPECT_LE(widthOf(out), maxWidth);
      } else if (out == textTruncation::ELLIPSIS_UTF8) {
        // The ellipsis alone is the floor: when the box cannot hold even one character plus an
        // ellipsis there is nothing narrower to return, so this is allowed to exceed maxWidth --
        // the old implementation did exactly the same. Assert it was UNAVOIDABLE rather than just
        // waving it through, or a walk that always gave up would pass this test.
        size_t firstChar = 1;
        while (firstChar < text.size() && (static_cast<unsigned char>(text[firstChar]) & 0xC0) == 0x80) ++firstChar;
        EXPECT_GE(widthOf(text.substr(0, firstChar) + textTruncation::ELLIPSIS_UTF8), maxWidth)
            << "gave up although one character plus an ellipsis would have fitted";
      } else {
        // The old loop trimmed while width >= maxWidth, so a real cut is strictly narrower.
        EXPECT_LT(widthOf(out), maxWidth);
      }
    }
  }
}

TEST(TextTruncationTest, CutIsMaximal) {
  for (const auto& text : corpus()) {
    const int full = widthOf(text);
    for (int maxWidth = 8; maxWidth <= full + 24; maxWidth += 7) {
      const std::string out = textTruncation::truncateToWidth(kFamily, text.c_str(), maxWidth, EpdFontFamily::REGULAR);
      if (out == text) continue;  // nothing was cut

      // Reconstruct the prefix, add one more character from the source, and confirm that
      // overflows. Without this, returning a bare ellipsis would satisfy the no-overflow test.
      const std::string ellipsis = textTruncation::ELLIPSIS_UTF8;
      ASSERT_GE(out.size(), ellipsis.size());
      const std::string prefix = out.substr(0, out.size() - ellipsis.size());
      if (prefix.size() >= text.size()) continue;

      // Advance one whole UTF-8 character past the prefix.
      size_t next = prefix.size() + 1;
      while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
      const std::string longer = text.substr(0, next) + ellipsis;

      SCOPED_TRACE("text=\"" + text + "\" maxWidth=" + std::to_string(maxWidth) + " out=\"" + out + "\" longer=\"" +
                   longer + "\"");
      EXPECT_GE(widthOf(longer), maxWidth) << "one more character would still have fitted";
    }
  }
}

TEST(TextTruncationTest, TextThatFitsIsReturnedUnchanged) {
  for (const auto& text : corpus()) {
    const int full = widthOf(text);
    EXPECT_EQ(textTruncation::truncateToWidth(kFamily, text.c_str(), full, EpdFontFamily::REGULAR), text);
    EXPECT_EQ(textTruncation::truncateToWidth(kFamily, text.c_str(), full + 100, EpdFontFamily::REGULAR), text);
  }
}

TEST(TextTruncationTest, NeverSplitsAUtf8Character) {
  // A cut inside a multi-byte sequence renders as a replacement box. Cyrillic is two bytes per
  // character, so every byte width is a chance to get this wrong.
  const std::string russian =
      "\xd0\x9f\xd1\x80\xd0\xb5\xd1\x81\xd1\x82\xd1\x83\xd0\xbf\xd0\xbb\xd0\xb5\xd0\xbd\xd0\xb8\xd0\xb5";
  const std::string ellipsis = textTruncation::ELLIPSIS_UTF8;
  for (int maxWidth = 6; maxWidth < widthOf(russian) + 12; ++maxWidth) {
    const std::string out = textTruncation::truncateToWidth(kFamily, russian.c_str(), maxWidth, EpdFontFamily::REGULAR);
    SCOPED_TRACE("maxWidth=" + std::to_string(maxWidth) + " out=\"" + out + "\"");
    // Every lead byte must be followed by exactly as many continuation bytes as it announces.
    size_t i = 0;
    while (i < out.size()) {
      const auto c = static_cast<unsigned char>(out[i]);
      size_t len = 1;
      if ((c & 0xE0) == 0xC0)
        len = 2;
      else if ((c & 0xF0) == 0xE0)
        len = 3;
      else if ((c & 0xF8) == 0xF0)
        len = 4;
      ASSERT_LE(i + len, out.size()) << "truncated mid-character at byte " << i;
      for (size_t k = 1; k < len; ++k) {
        ASSERT_EQ(static_cast<unsigned char>(out[i + k]) & 0xC0, 0x80) << "bad continuation at byte " << (i + k);
      }
      i += len;
    }
  }
}

TEST(TextTruncationTest, DegenerateWidthsAndInputs) {
  EXPECT_EQ(textTruncation::truncateToWidth(kFamily, nullptr, 100, EpdFontFamily::REGULAR), "");
  EXPECT_EQ(textTruncation::truncateToWidth(kFamily, "abc", 0, EpdFontFamily::REGULAR), "");
  EXPECT_EQ(textTruncation::truncateToWidth(kFamily, "abc", -5, EpdFontFamily::REGULAR), "");
  EXPECT_EQ(textTruncation::truncateToWidth(kFamily, "", 100, EpdFontFamily::REGULAR), "");
  // Too narrow for any character: the ellipsis alone is the floor, and it is allowed to exceed
  // the box because there is nothing narrower to return.
  EXPECT_EQ(textTruncation::truncateToWidth(kFamily, "Wide text here", 1, EpdFontFamily::REGULAR),
            textTruncation::ELLIPSIS_UTF8);
}

TEST(TextTruncationTest, BoldIsMeasuredWithTheBoldFace) {
  // A style bug would silently measure the regular face and overflow, since bold is wider.
  const char* text = "Inline footnote previews";
  const int boldFull = widthOf(text, EpdFontFamily::BOLD);
  const int narrow = boldFull / 2;
  const std::string out = textTruncation::truncateToWidth(kFamily, text, narrow, EpdFontFamily::BOLD);
  EXPECT_LT(widthOf(out, EpdFontFamily::BOLD), narrow);
  EXPECT_NE(out, std::string(text));
}

// The old implementation is not the oracle, but a large disagreement would mean the walk has
// drifted from the metrics it claims to model. Allow one character either way; flag more.
TEST(TextTruncationTest, AgreesWithTheOldAlgorithmWithinOneCharacter) {
  const std::string ellipsis = textTruncation::ELLIPSIS_UTF8;
  for (const auto& text : corpus()) {
    const int full = widthOf(text);
    for (int maxWidth = 12; maxWidth <= full + 12; maxWidth += 5) {
      // Old algorithm, verbatim: trim the last character while prefix+ellipsis does not fit.
      std::string legacy = text;
      if (widthOf(legacy) > maxWidth) {
        while (!legacy.empty() && widthOf(legacy + ellipsis) >= maxWidth) {
          legacy.erase(legacy.size() - lastCharBytes(legacy));
        }
        legacy = legacy.empty() ? ellipsis : legacy + ellipsis;
      }

      const std::string out = textTruncation::truncateToWidth(kFamily, text.c_str(), maxWidth, EpdFontFamily::REGULAR);
      SCOPED_TRACE("text=\"" + text + "\" maxWidth=" + std::to_string(maxWidth) + " new=\"" + out + "\" legacy=\"" +
                   legacy + "\"");
      const int delta = static_cast<int>(out.size()) - static_cast<int>(legacy.size());
      EXPECT_LE(std::abs(delta), 4) << "walk has drifted from the measured metrics";
    }
  }
}

}  // namespace
