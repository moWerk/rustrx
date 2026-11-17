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

// The mask for a room or own picture: layer.effect: RoundMask {} (clip:
// true would cut a square). A square with rounded corners, the radius a
// share of the side: 0.5 (the default) a circle, the setting's value
// app.avatarRadius elsewhere. The edge softens over the outer 1 %.
ShaderEffect {
    property real radius: 0.5
    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform sampler2D source;
        uniform lowp float qt_Opacity;
        uniform highp float radius;
        void main() {
            // distance outside the rounded square (negative inside)
            highp vec2 q = abs(qt_TexCoord0 - vec2(0.5)) - vec2(0.5 - radius);
            highp float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
            lowp float a = 1.0 - smoothstep(-0.01, 0.0, d);
            gl_FragColor = texture2D(source, qt_TexCoord0) * a * qt_Opacity;
        }"
}
