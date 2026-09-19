# The one list of built-in font sizes.
#
# Sourced by convert-builtin-fonts.sh, which generates the faces, and by
# build-font-ids.sh, which hashes each face set into the ID the renderer keys on.
# Keeping it here means a size cannot exist in one and not the other -- the ID
# script used to be unrolled per size by hand and silently fell behind.
#
# Changing a size here is half the job: the point size these map to lives in
# CrossPointSettings::FONT_SIZE_RUNGS, and the generated headers are referenced by
# name from builtinFonts/all.h, main.cpp and CrossPointSettings.cpp.

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
BOOKERLY_FONT_SIZES=(10 12 14 16 18 24)
NOTOSANS_FONT_SIZES=(10 12 14 16 18 24)

UI_FONT_STYLES=("Regular" "Bold")
UI_FONT_SIZES=(10 12 14)
# The UI faces that get an ID of their own. 14 pt is deliberately absent: the
# larger UI size does not add a third logical font, it REBINDS UI_10/UI_12 to the
# 14 pt families at startup (see applyUiFontScale in main.cpp), so every call site
# keeps asking for the same two IDs whatever the setting says.
UI_ID_FONT_SIZES=(10 12)
