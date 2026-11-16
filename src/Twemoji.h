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
#ifndef TWEMOJI_H
#define TWEMOJI_H

#include <QSet>
#include <QString>

// Emoji drawn as Twemoji pictures (qml/twemoji, 72 px PNGs, graphics
// CC-BY 4.0) instead of the system's emoji font: in text shown with
// Text.StyledText an emoji becomes an inline <img>, elsewhere a picture's
// address stands in for the character.
namespace Twemoji {

// the folder with the pictures; read once at start
void init(const QString &dir);
// the picture for one emoji (a grapheme cluster), "file://…", or "" when
// there is none
QString url(const QString &emoji);
// HTML (text with tags, entities as they are) with its emoji as pictures
// of the given height; tags and entities are left alone
QString inHtml(const QString &html, int height);

}

// Plain text made ready for Text.StyledText, the way the QML did it in
// JavaScript before, character for character (white space is what
// JavaScript's \s matches), so the output stays the same.
namespace StyledText {

// & and < as entities, and > too unless told not to
QString escaped(const QString &text, bool greater = true);
// escaped, its http(s):// and matrix: addresses as links, \n as <br>
QString linkified(const QString &text);
// every run of white space as one space
QString oneLine(const QString &text);

}

#endif
