// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// A Sailfish theme icon by name, tinted. In Rustrx this is
//     Image { source: "image://theme/" + name + "?" + color; sourceSize... }
// Off the phone the theme is not there, so the few icons the glance uses
// are drawn here, close enough to judge a layout: icon-m-right,
// icon-m-device-lock, icon-m-warning, icon-s-filled-warning,
// icon-m-acknowledge (a check in a circle), icon-m-clear (an x in a
// filled circle).
Canvas {
    id: icon
    property string name
    property color color: Theme.primaryColor
    property real size: Theme.iconSizeMedium
    width: size; height: size
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onSizeChanged: requestPaint()
    onPaint: {
        var g = getContext("2d"); g.reset()
        var s = width, lw = Math.max(1.5, s / 22)
        g.strokeStyle = color; g.fillStyle = color; g.lineWidth = lw; g.lineCap = "round"; g.lineJoin = "round"
        function line(pts) { g.beginPath(); g.moveTo(pts[0] * s, pts[1] * s); for (var i = 2; i < pts.length; i += 2) g.lineTo(pts[i] * s, pts[i + 1] * s); g.stroke() }
        if (name === "icon-m-right") {
            line([0.42, 0.28, 0.62, 0.5, 0.42, 0.72])
        } else if (name === "icon-m-acknowledge") {
            g.beginPath(); g.arc(s / 2, s / 2, s * 0.4, 0, 2 * Math.PI); g.stroke()
            line([0.32, 0.52, 0.45, 0.65, 0.69, 0.38])
        } else if (name === "icon-m-clear") {
            g.beginPath(); g.arc(s / 2, s / 2, s * 0.4, 0, 2 * Math.PI); g.fill()
            g.strokeStyle = Theme.backgroundColor
            line([0.38, 0.38, 0.62, 0.62]); line([0.62, 0.38, 0.38, 0.62])
        } else if (name === "icon-m-device-lock") {
            g.beginPath(); g.arc(s / 2, s * 0.42, s * 0.14, Math.PI, 0); g.stroke()
            line([0.36, 0.42, 0.36, 0.48]); line([0.64, 0.42, 0.64, 0.48])
            g.beginPath(); g.rect(s * 0.3, s * 0.48, s * 0.4, s * 0.28); g.stroke()
            g.beginPath(); g.arc(s / 2, s * 0.6, s * 0.03, 0, 2 * Math.PI); g.fill()
        } else if (name === "icon-m-warning" || name === "icon-s-filled-warning") {
            g.beginPath(); g.moveTo(s * 0.5, s * 0.16); g.lineTo(s * 0.86, s * 0.8); g.lineTo(s * 0.14, s * 0.8); g.closePath()
            if (name === "icon-s-filled-warning") { g.fill(); g.strokeStyle = Theme.backgroundColor } else g.stroke()
            line([0.5, 0.38, 0.5, 0.58]); line([0.5, 0.68, 0.5, 0.69])
        }
    }
}
