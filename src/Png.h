// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_PNG_H
#define RUSTRX_PNG_H

#include <QByteArray>

// What a PNG says about itself, out of it before it is sent, without
// re-encoding the picture: the chunks that hold it are dropped, the others
// copied as they are, their CRC with them. At level 2 the texts (tEXt,
// zTXt, iTXt), the time (tIME) and EXIF (eXIf) go; at level 1 only what can
// hold the place: EXIF, and XMP and the raw EXIF or XMP profiles some tools
// keep in text chunks. Anything after the end (IEND) goes at both.
// Returns false when the file is not a PNG whose chunks could be read to
// the end; the caller re-encodes then, which drops all of it.
namespace Png {

inline bool strip(QByteArray &png, int level = 2)
{
    static const char signature[] = "\x89PNG\r\n\x1a\n";
    if (png.size() < 8 || png.left(8) != QByteArray(signature, 8))
        return false;
    if (level <= 0)
        return true;

    auto u32 = [&](int at) -> quint32 {
        return (quint32(quint8(png.at(at))) << 24) | (quint32(quint8(png.at(at + 1))) << 16)
             | (quint32(quint8(png.at(at + 2))) << 8) | quint32(quint8(png.at(at + 3)));
    };
    int pos = 8;
    while (pos + 12 <= png.size()) {
        const quint32 len = u32(pos);
        // length, type, data, CRC
        if (len > quint32(png.size() - pos - 12))
            return false;
        const int end = pos + 12 + int(len);
        const QByteArray type = png.mid(pos + 4, 4);
        if (type == "IEND") {
            png.truncate(end);
            return true;
        }
        const bool text = type == "tEXt" || type == "zTXt" || type == "iTXt";
        bool drop = type == "eXIf";
        if (level >= 2) {
            drop = drop || text || type == "tIME";
        } else if (text) {
            // the keyword: the data up to its first NUL
            const QByteArray data = png.mid(pos + 8, int(len));
            const QByteArray keyword = data.left(data.indexOf('\0'));
            drop = keyword == "XML:com.adobe.xmp" || keyword.startsWith("Raw profile type");
        }
        if (drop)
            png.remove(pos, end - pos);
        else
            pos = end;
    }
    return false;
}

}

#endif
