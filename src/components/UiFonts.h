#pragma once

class GfxRenderer;

// Maps the menu font ids (UI_10, UI_12) to the family and size chosen in
// Settings -> Display -> Menu Font / Menu Font Size. Every screen draws through those ids,
// so re-running this after a settings change restyles the whole UI; UITheme::reload() then
// grows the row heights to match. Defined in main.cpp, where the built-in font data lives
// (the font headers are file-local, so they must stay in one translation unit).
void applyUiFontSettings(GfxRenderer& renderer);

// Layout multiplier for the chosen menu font size: 1.0, 1.2 or 1.4.
float uiFontScale();
