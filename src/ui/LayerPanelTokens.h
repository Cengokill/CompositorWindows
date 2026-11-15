#pragma once

// Shared visual measurements for the Layers panel and its reusable controls.
// One macOS point maps to one Qt logical pixel. Colors sampled from the
// supplied 2x reference screenshots are kept here, not in widget styles.
namespace compositor::ui::layer_panel {
inline constexpr auto background = "#242424";
inline constexpr auto primary = "#e0e0e0";
inline constexpr auto secondary = "#a3a3a3";
inline constexpr auto disabled = "#666666";
inline constexpr auto separator = "#393939";
inline constexpr auto accent = "#067dff";
inline constexpr auto control = "#353535";
inline constexpr auto field = "#22292b";
inline constexpr auto track = "#424242";
inline constexpr auto knob = "#ebebeb";
inline constexpr auto hover = "#353535";
inline constexpr auto pressed = "#444444";
inline constexpr auto popup = "#202020";
inline constexpr auto selectedRow = "#444444";
inline constexpr auto hoveredRow = "#303030";

inline constexpr int minWidth = 261;
inline constexpr int maxWidth = 372;
inline constexpr int headerHeight = 50;
inline constexpr int headerLeading = 18;
inline constexpr int headerTrailing = 10;
inline constexpr int headerVertical = 10;
inline constexpr int headerSpacing = 2;
inline constexpr int headerIcon = 26;
inline constexpr int searchHeight = 30;
inline constexpr int searchRadius = 6;
inline constexpr int searchLeading = 8;
inline constexpr int searchTrailing = 6;
inline constexpr int searchGap = 6;
inline constexpr int searchMagnifier = 13;
inline constexpr int searchIconFrame = 16;
inline constexpr int searchOutsideTrailing = 6;
inline constexpr int appearancePadding = 12;
inline constexpr int appearanceSpacing = 8;
inline constexpr int opacitySpacing = 6;
inline constexpr int percentSpacing = 2;
inline constexpr int percentWidth = 44;
inline constexpr int percentHeight = 26;
inline constexpr int fieldRadius = 6;
inline constexpr int sliderHeight = 22;
inline constexpr int sliderMinimumWidth = 40;
inline constexpr int sliderTrack = 3;
inline constexpr int sliderKnobWidth = 10;
inline constexpr int sliderKnobHeight = 18;
inline constexpr int sliderKnobRadius = 5;
inline constexpr int blendMinimumWidth = 60;
inline constexpr int blendHeight = 24;
inline constexpr int blendLeading = 12;
inline constexpr int blendTrailing = 28;
inline constexpr int blendArrowWidth = 25;
inline constexpr int blendChevronSize = 8;
inline constexpr int blendChevronTrailing = 14;
inline constexpr int blendChevronTop = 3;
inline constexpr int blendChevronBottom = 11;
inline constexpr int blendGroupGap = 8;
inline constexpr int blendMenuCheckInset = 8;
inline constexpr int blendMenuTextInset = 19;
inline constexpr int footerHorizontal = 8;
inline constexpr int footerVertical = 4;
inline constexpr int footerIconHorizontal = 8;
inline constexpr int footerIconVertical = 12;
inline constexpr int footerMenuHorizontal = 4;
inline constexpr int footerMenuVertical = 12;
inline constexpr int footerIconSize = 16;
inline constexpr int footerChevronSize = 7;
inline constexpr int footerChevronGap = 3;
inline constexpr int footerMenuGapAbove = 6;
inline constexpr int emptySpacing = 10;
inline constexpr int emptyPadding = 16;
inline constexpr int emptyIcon = 25;
inline constexpr int emptyTitleHeight = 18;
inline constexpr int emptyDetailHeight = 40;
inline constexpr int titleFont = 12;
inline constexpr int captionFont = 12;
inline constexpr int searchFont = 13;
inline constexpr int calloutFont = 13;
inline constexpr int separatorWidth = 1;
inline constexpr int resizeHitWidth = 8;
}
