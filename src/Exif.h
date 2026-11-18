// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_EXIF_H
#define RUSTRX_EXIF_H

#include <QByteArray>

// What a photo says about itself, out of a JPEG before it is sent, without
// re-encoding the picture. The EXIF block (place, time, camera, its own
// thumbnail) becomes a new one holding only the orientation, so the photo
// still shows upright; XMP, IPTC (APP13), comments and the multi-picture
// index (MPF, APP2) go, and so does anything after the picture's end (the
// extra images MPF points to). The colour profile (ICC, APP2) and the
// Adobe and JFIF markers stay: they say how to draw the picture.
// Returns false when the file is not a JPEG whose blocks could be read to
// the end; the caller re-encodes then, which drops all of it.
namespace Exif {

// the orientation (1 to 8) in an EXIF block's IFD0, or 0
inline int orientation(const QByteArray &jpeg, int tiff, int tiffEnd)
{
    auto byte = [&](int i) { return quint8(jpeg.at(i)); };
    const bool le = jpeg.mid(tiff, 2) == "II";
    if (!le && jpeg.mid(tiff, 2) != "MM")
        return 0;
    auto u16 = [&](int at) -> int {
        return le ? byte(at) | (byte(at + 1) << 8) : (byte(at) << 8) | byte(at + 1);
    };
    auto u32 = [&](int at) -> qint64 {
        return le ? qint64(byte(at)) | (qint64(byte(at + 1)) << 8) | (qint64(byte(at + 2)) << 16) | (qint64(byte(at + 3)) << 24)
                  : (qint64(byte(at)) << 24) | (qint64(byte(at + 1)) << 16) | (qint64(byte(at + 2)) << 8) | qint64(byte(at + 3));
    };
    auto inside = [&](qint64 at, qint64 size) { return at >= tiff && size >= 0 && at + size <= tiffEnd; };
    if (!inside(tiff, 8))
        return 0;
    const qint64 ifd0 = tiff + u32(tiff + 4);
    if (!inside(ifd0, 2))
        return 0;
    const int count = u16(int(ifd0));
    if (!inside(ifd0 + 2, qint64(count) * 12))
        return 0;
    for (int e = 0; e < count; ++e) {
        const int entry = int(ifd0 + 2 + e * 12);
        if (u16(entry) == 0x0112 && u16(entry + 2) == 3) {
            const int v = u16(entry + 8);
            return v >= 1 && v <= 8 ? v : 0;
        }
    }
    return 0;
}

// an EXIF block (APP1) with the orientation alone, little-endian
inline QByteArray orientationOnly(int value)
{
    QByteArray body("Exif\0\0II*\0\x08\0\0\0", 14);
    const char ifd[] = { 1, 0,                         // one entry
                         0x12, 0x01, 3, 0, 1, 0, 0, 0, // Orientation, SHORT, 1
                         char(value), 0, 0, 0,
                         0, 0, 0, 0 };                 // no next IFD
    body.append(ifd, sizeof ifd);
    QByteArray seg("\xFF\xE1", 2);
    const int len = body.size() + 2;
    seg.append(char(len >> 8));
    seg.append(char(len & 0xFF));
    return seg + body;
}

inline bool strip(QByteArray &jpeg)
{
    auto byte = [&](int i) { return quint8(jpeg.at(i)); };
    if (jpeg.size() < 4 || byte(0) != 0xFF || byte(1) != 0xD8)
        return false;

    int pos = 2;
    while (pos + 4 <= jpeg.size()) {
        if (byte(pos) != 0xFF)
            return false;
        const quint8 marker = byte(pos + 1);
        if (marker == 0xD9) {
            jpeg.truncate(pos + 2);
            return true;
        }
        const int len = (byte(pos + 2) << 8) | byte(pos + 3);
        const int seg = pos + 4;
        const int end = pos + 2 + len;
        if (len < 2 || end > jpeg.size())
            return false;
        if (marker == 0xDA) {
            // the picture: it ends at the first marker that is neither a
            // stuffed 0xFF00 nor a restart marker; after it, nothing stays
            for (int i = end; i + 1 < jpeg.size(); ++i) {
                if (byte(i) == 0xFF && byte(i + 1) == 0xD9) {
                    jpeg.truncate(i + 2);
                    return true;
                }
            }
            return false;
        }
        auto starts = [&](const char *magic, int size) {
            return len - 2 >= size && jpeg.mid(seg, size) == QByteArray(magic, size);
        };
        if (marker == 0xE1 && starts("Exif\0\0", 6)) {
            const QByteArray kept = [&] {
                const int o = orientation(jpeg, seg + 6, end);
                return o ? orientationOnly(o) : QByteArray();
            }();
            jpeg.replace(pos, end - pos, kept);
            pos += kept.size();
            continue;
        }
        if (marker == 0xE1 || marker == 0xED || marker == 0xFE
                || (marker == 0xE2 && starts("MPF\0", 4))) {
            // XMP or other APP1, IPTC, a comment, the multi-picture index
            jpeg.remove(pos, end - pos);
            continue;
        }
        pos = end;
    }
    return false;
}

}

#endif
