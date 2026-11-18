// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
// Stand-in for Silica's ProgressCircleBase: one arc from 12 o'clock,
// clockwise, over value (0 to 1) of the circle, borderWidth thick.
Canvas {
    id: c
    property real value
    property real borderWidth: Theme.paddingSmall
    property color progressColor: Theme.highlightColor
    property color backgroundColor: Theme.rgba(Theme.primaryColor, Theme.opacityFaint)
    onValueChanged: requestPaint()
    onProgressColorChanged: requestPaint()
    onBackgroundColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onPaint: {
        var g = getContext("2d"); g.reset()
        var r = Math.min(width, height) / 2 - borderWidth / 2
        g.lineWidth = borderWidth
        g.strokeStyle = backgroundColor
        g.beginPath(); g.arc(width / 2, height / 2, r, 0, 2 * Math.PI); g.stroke()
        g.strokeStyle = progressColor
        g.beginPath(); g.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * value); g.stroke()
    }
}
