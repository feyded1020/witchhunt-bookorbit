#include <gtest/gtest.h>

#include <string>

#include "../../lib/Epub/Epub/css/CssParser.h"

TEST(CssParserDeclarations, InlineLineThrough) {
  const CssStyle style = CssParser::parseInlineStyle("text-decoration: line-through");
  ASSERT_TRUE(style.hasTextDecoration());
  ASSERT_EQ(style.textDecoration, CssTextDecoration::LineThrough);
}

TEST(CssParserDeclarations, InlineUnderlineLineThrough) {
  const CssStyle style = CssParser::parseInlineStyle("text-decoration: underline line-through");
  ASSERT_TRUE(style.hasTextDecoration());
  ASSERT_EQ(style.textDecoration, CssTextDecoration::UnderlineLineThrough);
}

TEST(CssParserDeclarations, InlineLineThroughUnderlineOrderInsensitive) {
  const CssStyle style = CssParser::parseInlineStyle("text-decoration: line-through underline");
  ASSERT_TRUE(style.hasTextDecoration());
  ASSERT_EQ(style.textDecoration, CssTextDecoration::UnderlineLineThrough);
}

TEST(CssParserDeclarations, InlineTextDecorationNormalization) {
  const CssStyle style = CssParser::parseInlineStyle("TEXT-DECORATION : LINE-THROUGH ;");
  ASSERT_TRUE(style.hasTextDecoration());
  ASSERT_EQ(style.textDecoration, CssTextDecoration::LineThrough);
}

// `img { height: 100%; ...; height: auto; width: 100% }` — the later `auto` has to win. Dropping
// it as unparseable left the 100% standing, which sized the image box to the whole viewport while
// the decoder filled only the aspect-correct top of it; the rest replayed as a black band.
TEST(CssParserDeclarations, ImageHeightAutoClearsEarlierLength) {
  const CssStyle style = CssParser::parseInlineStyle("height: 100%; width: 100%; height: auto");
  ASSERT_TRUE(style.hasImageWidth());
  ASSERT_FALSE(style.hasImageHeight());
}

// Order still matters the other way round: a length after the auto wins.
TEST(CssParserDeclarations, ImageHeightLengthAfterAutoWins) {
  const CssStyle style = CssParser::parseInlineStyle("height: auto; height: 2em");
  ASSERT_TRUE(style.hasImageHeight());
  ASSERT_EQ(style.imageHeight.unit, CssUnit::Em);
}

// The auto marker must survive the cascade merge, so a more specific rule saying `width: auto`
// clears a length a less specific one set.
TEST(CssParserDeclarations, ImageWidthAutoOverridesLowerPriorityRule) {
  CssStyle resolved = CssParser::parseInlineStyle("width: 50%");
  resolved.applyOver(CssParser::parseInlineStyle("width: auto"));
  ASSERT_FALSE(resolved.hasImageWidth());
}

// Unsupported keywords stay ignored — only auto and the CSS-wide keywords that resolve to it
// clear the property.
TEST(CssParserDeclarations, ImageHeightUnknownKeywordIgnored) {
  const CssStyle style = CssParser::parseInlineStyle("height: 100%; height: fit-content");
  ASSERT_TRUE(style.hasImageHeight());
  ASSERT_EQ(style.imageHeight.unit, CssUnit::Percent);
}

TEST(CssParserDeclarations, ImageWidthImportant) {
  const CssStyle style = CssParser::parseInlineStyle("width: 50% !important");
  ASSERT_TRUE(style.hasImageWidth());
  ASSERT_EQ(style.imageWidth.unit, CssUnit::Percent);
  ASSERT_EQ(style.imageWidth.value, 50.0f);
}

// Regression: `!important` used to be stripped only by the dozen properties that called
// stripTrailingImportant themselves. Everything else compared the marker as part of the value
// and silently dropped the declaration. crosspoint-reader PR #3221.
TEST(CssParserDeclarations, ImportantStrippedForEveryProperty) {
  const CssStyle align = CssParser::parseInlineStyle("text-align: center !important");
  ASSERT_TRUE(align.hasTextAlign());
  ASSERT_EQ(static_cast<int>(align.textAlign), static_cast<int>(CssTextAlign::Center));

  const CssStyle weight = CssParser::parseInlineStyle("font-weight: bold !important");
  ASSERT_TRUE(weight.hasFontWeight());
  ASSERT_EQ(static_cast<int>(weight.fontWeight), static_cast<int>(CssFontWeight::Bold));

  const CssStyle decoration = CssParser::parseInlineStyle("text-decoration: underline !important");
  ASSERT_TRUE(decoration.hasTextDecoration());
  ASSERT_EQ(static_cast<int>(decoration.textDecoration), static_cast<int>(CssTextDecoration::Underline));

  const CssStyle indent = CssParser::parseInlineStyle("text-indent: 2em !important");
  ASSERT_TRUE(indent.hasTextIndent());
  ASSERT_EQ(indent.textIndent.value, 2.0f);

  // Shorthand: the marker has to go before the value is split on whitespace, or the last
  // component parses as "0 !important" and the whole declaration is dropped.
  const CssStyle margin = CssParser::parseInlineStyle("margin: 0 !important");
  ASSERT_TRUE(margin.hasMarginTop());
  ASSERT_EQ(margin.marginTop.value, 0.0f);
}

// Font-size absolute units and keywords resolve to body-relative multipliers:
// pt normalises against 12 pt, px against 16 px, keywords use fixed steps.
TEST(CssParserUnits, FontSizeKeywordsAndAbsoluteUnits) {
  struct Case {
    const char* decl;
    float expected;
  };
  const Case cases[] = {
      {"font-size: 9pt", 0.75f},     {"font-size: 12pt", 1.0f},     {"font-size: 24pt", 2.0f},
      {"font-size: 8px", 0.5f},      {"font-size: 16px", 1.0f},     {"font-size: 32px", 2.0f},
      {"font-size: xx-small", 0.6f}, {"font-size: x-small", 0.75f}, {"font-size: small", 0.8f},
      {"font-size: smaller", 0.8f},  {"font-size: medium", 1.0f},   {"font-size: large", 1.2f},
      {"font-size: larger", 1.2f},   {"font-size: x-large", 1.4f},  {"font-size: xx-large", 1.6f},
  };
  for (const auto& c : cases) {
    const CssStyle st = CssParser::parseInlineStyle(c.decl);
    EXPECT_TRUE(st.hasFontSizeMultiplier()) << c.decl;
    EXPECT_FLOAT_EQ(st.fontSizeMultiplier, c.expected) << c.decl;
  }

  // Unknown keyword must leave font-size undefined.
  const CssStyle bogus = CssParser::parseInlineStyle("font-size: enormous");
  EXPECT_FALSE(bogus.hasFontSizeMultiplier());
}
