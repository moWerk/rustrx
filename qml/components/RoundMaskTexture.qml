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

// RoundMask without a layer: laid over a hidden Image, it reads the Image's
// own texture (no offscreen pass per picture). The texture is the whole
// loaded picture, so crop says which share of it PreserveAspectCrop shows
// (cropOf). The same corner and soft edge as RoundMask.
ShaderEffect {
    property Item sourceImage
    property real radius: 0.5
    readonly property variant source: sourceImage
    readonly property size crop: cropOf(sourceImage ? sourceImage.implicitWidth : 0, sourceImage ? sourceImage.implicitHeight : 0, width, height)
    function cropOf(iw, ih, w, h) {
        if (iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return Qt.size(1, 1)
        var k = Math.max(w / iw, h / ih)
        return Qt.size(w / (k * iw), h / (k * ih))
    }
    readonly property real r: radius * Math.min(width, height)
    readonly property size box: Qt.size(width, height)
    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform sampler2D source;
        uniform lowp float qt_Opacity;
        uniform highp float r;
        uniform highp vec2 box;
        uniform highp vec2 crop;
        void main() {
            highp vec2 q = abs((qt_TexCoord0 - vec2(0.5)) * box) - (box * 0.5 - vec2(r));
            highp float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
            lowp float a = 1.0 - smoothstep(-1.0, 0.0, d);
            gl_FragColor = texture2D(source, vec2(0.5) + (qt_TexCoord0 - vec2(0.5)) * crop) * a * qt_Opacity;
        }"
}
