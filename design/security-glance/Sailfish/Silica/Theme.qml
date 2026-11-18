// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-in for Silica's Theme: the Jolla Phone 2026's values (pixel ratio
// 1.5, Ruska ambience: dark, highlight #fe9a80). The mockup's controls
// change the ambience and the scale; Silica's real Theme takes over on a phone.
pragma Singleton
import QtQuick 2.6

QtObject {
    // 1.5 on the Jolla Phone 2026; 1.0 fits a desktop screen
    property real pixelRatio: 1.0
    // the font size category ("huge" on the author's phone) as a factor
    property real fontScale: 1.0
    property bool lightOnDark: true
    property color highlightColor: "#fe9a80"

    readonly property int colorScheme: lightOnDark ? 0 : 1

    readonly property color primaryColor: lightOnDark ? "#ffffff" : "#000000"
    readonly property color secondaryColor: lightOnDark ? "#bababa" : "#4d4d4d"
    readonly property color secondaryHighlightColor: Qt.darker(highlightColor, 1.3)
    readonly property color highlightDimmerColor: Qt.darker(highlightColor, 2.5)
    readonly property color highlightBackgroundColor: highlightColor
    readonly property color errorColor: "#ff4d4d"
    // not in Silica: the page ground under the ambience picture, for the mockup
    readonly property color backgroundColor: lightOnDark ? "#1c1416" : "#f4efed"

    readonly property real opacityFaint: 0.2
    readonly property real opacityLow: 0.4
    readonly property real opacityHigh: 0.6
    readonly property real opacityOverlay: 0.8
    readonly property real highlightBackgroundOpacity: 0.3

    function px(v) { return Math.round(v * pixelRatio) }
    function fs(v) { return Math.round(v * pixelRatio * fontScale) }

    readonly property int paddingSmall: px(6)
    readonly property int paddingMedium: px(12)
    readonly property int paddingLarge: px(24)
    readonly property int horizontalPageMargin: px(24)

    readonly property int fontSizeTiny: fs(20)
    readonly property int fontSizeExtraSmall: fs(24)
    readonly property int fontSizeSmall: fs(28)
    readonly property int fontSizeMedium: fs(32)
    readonly property int fontSizeLarge: fs(40)
    readonly property int fontSizeExtraLarge: fs(50)
    readonly property int fontSizeHuge: fs(64)
    readonly property string fontFamily: "Sans"

    readonly property int itemSizeExtraSmall: px(70)
    readonly property int itemSizeSmall: px(80)
    readonly property int itemSizeMedium: px(100)
    readonly property int itemSizeLarge: px(110)
    readonly property int itemSizeExtraLarge: px(135)
    readonly property int itemSizeHuge: px(180)

    readonly property int iconSizeExtraSmall: px(24)
    readonly property int iconSizeSmall: px(32)
    readonly property int iconSizeSmallPlus: px(48)
    readonly property int iconSizeMedium: px(64)
    readonly property int iconSizeLarge: px(96)

    function rgba(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
