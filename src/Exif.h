// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_EXIF_H
#define RUSTRX_EXIF_H

#include <QByteArray>

// What a photo says about itself, out of a JPEG before it is sent, without
// re-encoding the picture. At level 2 the EXIF block (place, time, camera,
// its own thumbnail) becomes a new one holding only the orientation, so the
// photo still shows upright; XMP, IPTC (APP13) and comments go. At level 1
// only the place goes: the EXIF block's GPS entries are blanked where they
// are, and XMP goes whole, because it can repeat them. At both, the
// multi-picture index (MPF, APP2) goes, and so does anything after the
// picture's end (the extra images MPF points to, each with its own EXIF).
// The colour profile (ICC, APP2) and the Adobe and JFIF markers stay: they
// say how to draw the picture.
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

// level 1: the GPS IFD of an EXIF block at jpeg[tiff, tiffEnd) blanked in
// place, every entry and the values it points to zeroed and its count 0,
// so the pointer to it stays valid and nothing moves; false when the block
// cannot be read
inline bool blankGps(QByteArray &jpeg, int tiff, int tiffEnd)
{
    auto byte = [&](qint64 i) { return quint8(jpeg.at(int(i))); };
    const bool le = jpeg.mid(tiff, 2) == "II";
    if (!le && jpeg.mid(tiff, 2) != "MM")
        return false;
    auto u16 = [&](qint64 at) -> int {
        return le ? byte(at) | (byte(at + 1) << 8) : (byte(at) << 8) | byte(at + 1);
    };
    auto u32 = [&](qint64 at) -> qint64 {
        return le ? qint64(byte(at)) | (qint64(byte(at + 1)) << 8) | (qint64(byte(at + 2)) << 16) | (qint64(byte(at + 3)) << 24)
                  : (qint64(byte(at)) << 24) | (qint64(byte(at + 1)) << 16) | (qint64(byte(at + 2)) << 8) | qint64(byte(at + 3));
    };
    auto inside = [&](qint64 at, qint64 size) { return at >= tiff && size >= 0 && at + size <= tiffEnd; };
    auto zero = [&](qint64 at, qint64 size) {
        for (qint64 i = at; i < at + size; ++i)
            jpeg[int(i)] = 0;
    };
    if (!inside(tiff, 8))
        return false;
    const qint64 ifd0 = tiff + u32(tiff + 4);
    if (!inside(ifd0, 2) || !inside(ifd0 + 2, qint64(u16(ifd0)) * 12))
        return false;
    qint64 gps = -1;
    for (int e = 0; e < u16(ifd0); ++e) {
        const qint64 entry = ifd0 + 2 + e * 12;
        if (u16(entry) == 0x8825)
            gps = tiff + u32(entry + 8);
    }
    if (gps < 0)
        return true;
    if (!inside(gps, 2))
        return false;
    const int count = u16(gps);
    // the entries and the next-IFD offset after them
    if (!inside(gps + 2, qint64(count) * 12 + 4))
        return false;
    // bytes per value of each TIFF type, 1 to 12
    static const int sizes[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
    for (int e = 0; e < count; ++e) {
        const qint64 entry = gps + 2 + e * 12;
        const int type = u16(entry + 2);
        if (type < 1 || type > 12)
            return false;
        const qint64 size = qint64(sizes[type]) * u32(entry + 4);
        if (size > 4) {
            const qint64 at = tiff + u32(entry + 8);
            if (!inside(at, size))
                return false;
            zero(at, size);
        }
    }
    zero(gps, 2 + qint64(count) * 12 + 4);
    return true;
}

// level 2: all metadata but the orientation, 1: the place; 0 leaves the
// file as it is
inline bool strip(QByteArray &jpeg, int level = 2)
{
    auto byte = [&](int i) { return quint8(jpeg.at(i)); };
    if (jpeg.size() < 4 || byte(0) != 0xFF || byte(1) != 0xD8)
        return false;
    if (level <= 0)
        return true;

    int pos = 2;
    while (pos + 2 <= jpeg.size()) {
        if (byte(pos) != 0xFF)
            return false;
        const quint8 marker = byte(pos + 1);
        // fill bytes: any number of 0xFF may stand before a marker
        if (marker == 0xFF) {
            ++pos;
            continue;
        }
        if (marker == 0xD9) {
            jpeg.truncate(pos + 2);
            return true;
        }
        // markers without a length: restarts and TEM
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            pos += 2;
            continue;
        }
        if (pos + 4 > jpeg.size())
            return false;
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
            if (level == 1) {
                if (!blankGps(jpeg, seg + 6, end))
                    return false;
                pos = end;
                continue;
            }
            const QByteArray kept = [&] {
                const int o = orientation(jpeg, seg + 6, end);
                return o ? orientationOnly(o) : QByteArray();
            }();
            jpeg.replace(pos, end - pos, kept);
            pos += kept.size();
            continue;
        }
        if (marker == 0xE1 || (marker == 0xE2 && starts("MPF\0", 4))
                || (level >= 2 && (marker == 0xED || marker == 0xFE))) {
            // XMP or other APP1, the multi-picture index; at 2 also IPTC
            // and comments
            jpeg.remove(pos, end - pos);
            continue;
        }
        pos = end;
    }
    return false;
}

}

#endif
