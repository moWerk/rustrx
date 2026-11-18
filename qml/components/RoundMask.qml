/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
import QtQuick 2.6

// The mask for a room or own picture, or a message's picture or video:
// layer.effect: RoundMask {} (clip: true would cut a square). Rounded
// corners, the radius a share of the shorter side: 0.5 (the default) a
// circle on a square, the setting's value app.avatarRadius elsewhere; or
// radiusPx, in pixels, for a picture of any shape. The edge softens over
// one pixel.
ShaderEffect {
    property real radius: 0.5
    property real radiusPx: -1
    // the corner in pixels, and the size, for the shader
    readonly property real r: radiusPx >= 0 ? radiusPx : radius * Math.min(width, height)
    readonly property size box: Qt.size(width, height)
    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform sampler2D source;
        uniform lowp float qt_Opacity;
        uniform highp float r;
        uniform highp vec2 box;
        void main() {
            // distance outside the rounded rectangle, in pixels (negative inside)
            highp vec2 q = abs((qt_TexCoord0 - vec2(0.5)) * box) - (box * 0.5 - vec2(r));
            highp float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
            lowp float a = 1.0 - smoothstep(-1.0, 0.0, d);
            gl_FragColor = texture2D(source, qt_TexCoord0) * a * qt_Opacity;
        }"
}
