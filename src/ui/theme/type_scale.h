#pragma once

namespace Kites
{
/**
 * @brief The one knob for the interface's text size.
 *
 * Chrome and data tables are sized from this: the menu bar, the ribbon's tab
 * strip, button labels, group captions, the status bar, the register and
 * memory tables, and the pane titles inside the tabs. The stylesheet reads
 * it through the {{fontSize}}, {{fontSizeSmall}} and {{fontSizeTable}}
 * tokens that ThemeManager substitutes, and table_font.cpp reads it
 * directly, so there is a single place to turn.
 *
 * NOT the code editor. That has its own family and size, chosen by the user
 * in Settings and served by FontManager; nothing here touches it.
 *
 * Pixels rather than points because the rest of the stylesheet is in pixels
 * and Qt scales both by the device pixel ratio anyway. 14 px is one point
 * larger than the 13 px this interface used before.
 */
inline constexpr int baseFontPixelSize = 14;

/// Captions, column headers and pane titles: one step down, and never
/// larger than the text of the control they label.
inline constexpr int smallFontPixelSize = baseFontPixelSize - 2;

/// Numeric table cells. A step below body text, so a dense grid of hex
/// digits does not shout over the chrome around it.
inline constexpr int tableFontPixelSize = baseFontPixelSize - 1;
} // namespace Kites
