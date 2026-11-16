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
#include "Twemoji.h"

#include <QDir>
#include <QRegularExpression>
#include <QVector>

namespace {

QString s_dir;
QSet<QString> s_names;   // file names without ".png": "1f600", "1f468-200d-1f4bb"

// the longest emoji sequence Twemoji has (family with skin tones)
const int kMaxLength = 10;

// one character on its own, without the emoji variation selector, is an
// emoji only where emoji presentation is its default (Unicode's
// Emoji_Presentation in the BMP); "❤", "©", "1" stay text
bool emojiByDefault(uint cp)
{
    if (cp >= 0x1F000)
        return true;
    static const QSet<uint> bmp = {
        0x231A, 0x231B, 0x23E9, 0x23EA, 0x23EB, 0x23EC, 0x23F0, 0x23F3, 0x25FD, 0x25FE,
        0x2614, 0x2615, 0x2648, 0x2649, 0x264A, 0x264B, 0x264C, 0x264D, 0x264E, 0x264F,
        0x2650, 0x2651, 0x2652, 0x2653, 0x267F, 0x2693, 0x26A1, 0x26AA, 0x26AB, 0x26BD,
        0x26BE, 0x26C4, 0x26C5, 0x26CE, 0x26D4, 0x26EA, 0x26F2, 0x26F3, 0x26F5, 0x26FA,
        0x26FD, 0x2705, 0x270A, 0x270B, 0x2728, 0x274C, 0x274E, 0x2753, 0x2754, 0x2755,
        0x2757, 0x2795, 0x2796, 0x2797, 0x27B0, 0x27BF, 0x2B1B, 0x2B1C, 0x2B50, 0x2B55 };
    return bmp.contains(cp);
}

// where an emoji can start: saves the lookups for ordinary text
bool mayStart(uint cp)
{
    return cp >= 0x203C || cp == 0x23 || cp == 0x2A || (cp >= 0x30 && cp <= 0x39) || cp == 0xA9 || cp == 0xAE;
}

// Twemoji's file name: the code points in hex, joined by "-"; the
// variation selector U+FE0F is left out unless the sequence has a joiner
QString nameOf(const uint *cps, int n)
{
    bool zwj = false;
    for (int i = 0; i < n; ++i)
        if (cps[i] == 0x200D)
            zwj = true;
    QString name;
    for (int i = 0; i < n; ++i) {
        if (!zwj && cps[i] == 0xFE0F)
            continue;
        if (!name.isEmpty())
            name += QLatin1Char('-');
        name += QString::number(cps[i], 16);
    }
    return name;
}

// the longest emoji at cps[at]: its length in code points (0: none) and
// its file name
int match(const QVector<uint> &cps, int at, QString *name)
{
    if (!mayStart(cps[at]))
        return 0;
    for (int n = qMin(kMaxLength, cps.size() - at); n >= 1; --n) {
        const uint *seq = cps.constData() + at;
        if (n == 1 && !emojiByDefault(seq[0]))
            continue;
        QString key = nameOf(seq, n);
        // a joined sequence may be filed without its selectors
        if (!s_names.contains(key) && n > 1) {
            QVector<uint> bare;
            for (int i = 0; i < n; ++i)
                if (seq[i] != 0xFE0F)
                    bare << seq[i];
            key = nameOf(bare.constData(), bare.size());
        }
        if (s_names.contains(key)) {
            *name = key;
            return n;
        }
    }
    return 0;
}

QString imgTag(const QString &name, int height)
{
    return QStringLiteral("<img src=\"file://%1/%2.png\" width=\"%3\" height=\"%3\" align=\"middle\">")
            .arg(s_dir, name).arg(height);
}

// plain text (no tags, no entities) with its emoji as pictures
QString inText(const QString &text, int height)
{
    const QVector<uint> cps = text.toUcs4();
    bool any = false;
    for (uint cp : cps)
        if (mayStart(cp) && cp >= 0x203C) { any = true; break; }
    if (!any && !text.contains(QChar(0xFE0F)))
        return text;
    QString out;
    for (int i = 0; i < cps.size();) {
        QString name;
        const int n = match(cps, i, &name);
        if (n > 0) {
            out += imgTag(name, height);
            i += n;
        } else {
            out += QString::fromUcs4(&cps[i], 1);
            ++i;
        }
    }
    return out;
}

}

namespace Twemoji {

void init(const QString &dir)
{
    s_dir = dir;
    s_names.clear();
    const QStringList files = QDir(dir).entryList({ QStringLiteral("*.png") }, QDir::Files);
    for (const QString &f : files)
        s_names.insert(f.left(f.size() - 4));
}

QString url(const QString &emoji)
{
    const QVector<uint> cps = emoji.toUcs4();
    if (cps.isEmpty())
        return QString();
    QString name;
    // the whole of it, or nothing (a picker cell holds one emoji)
    if (match(cps, 0, &name) != cps.size()) {
        // one character without the selector, offered by the picker as an emoji
        const QString key = nameOf(cps.constData(), cps.size());
        if (!s_names.contains(key))
            return QString();
        name = key;
    }
    return QStringLiteral("file://%1/%2.png").arg(s_dir, name);
}

QString inHtml(const QString &html, int height)
{
    if (s_names.isEmpty() || html.isEmpty())
        return html;
    // tags and entities pass, the text between them is looked at
    static const QRegularExpression markup(QStringLiteral("<[^>]*>|&[#a-zA-Z0-9]{1,10};"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = markup.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += inText(html.mid(last, m.capturedStart() - last), height);
        out += m.captured();
        last = m.capturedEnd();
    }
    out += inText(html.mid(last), height);
    return out;
}

}

namespace StyledText {

namespace {

// JavaScript's \s (the YARR table Qt's engine uses)
bool isSpace(ushort c)
{
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680
            || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F
            || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

// the end of an address starting at "at", or -1: the scheme, then at
// least one character up to white space or "<"
int linkEnd(const QString &s, int at)
{
    int from = -1;
    if (s.midRef(at, 4) == QLatin1String("http")) {
        if (s.midRef(at + 4, 4) == QLatin1String("s://"))
            from = at + 8;
        else if (s.midRef(at + 4, 3) == QLatin1String("://"))
            from = at + 7;
    } else if (s.midRef(at, 7) == QLatin1String("matrix:")) {
        from = at + 7;
    }
    if (from < 0)
        return -1;
    int end = from;
    while (end < s.size() && s.at(end) != QLatin1Char('<') && !isSpace(s.at(end).unicode()))
        ++end;
    return end > from ? end : -1;
}

}

QString escaped(const QString &text, bool greater)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) out += QLatin1String("&lt;");
        else if (greater && c == QLatin1Char('>')) out += QLatin1String("&gt;");
        else out += c;
    }
    return out;
}

// An address for inside href="...": a quote in it would end the value
// and start a second href, and StyledText takes the last one.
QString hrefSafe(const QString &url)
{
    QString out = url;
    out.replace(QLatin1Char('"'), QLatin1String("%22"));
    out.replace(QLatin1Char('\''), QLatin1String("%27"));
    return out;
}

QString linkified(const QString &text)
{
    const QString s = escaped(text);
    QString out;
    out.reserve(s.size());
    int last = 0;
    for (int i = 0; i < s.size();) {
        const int end = linkEnd(s, i);
        if (end < 0) {
            ++i;
            continue;
        }
        const QString url = s.mid(i, end - i);
        out += s.midRef(last, i - last);
        out += QLatin1String("<a href=\"") + hrefSafe(url) + QLatin1String("\">") + url + QLatin1String("</a>");
        i = last = end;
    }
    out += s.midRef(last);
    out.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    return out;
}

QString oneLine(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool inSpace = false;
    for (const QChar c : text) {
        if (isSpace(c.unicode())) {
            if (!inSpace)
                out += QLatin1Char(' ');
            inSpace = true;
        } else {
            out += c;
            inSpace = false;
        }
    }
    return out;
}

}
