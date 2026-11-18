// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_EXIF_H
#define RUSTRX_EXIF_H

#include <QByteArray>

// The place a photo was taken, out of a JPEG before it is sent: the GPS
// part of its EXIF block is cleared in place (its entries and the values
// they point to zeroed, the block left empty), and an XMP block, which can
// hold the place too, is dropped. Everything else stays as the camera
// wrote it (orientation, time, the picture itself, not re-encoded).
// Returns false when the file is not a JPEG whose EXIF could be read to
// the end; the caller re-encodes then, which drops all of it.
namespace Exif {

inline bool stripLocation(QByteArray &jpeg)
{
    const int n = jpeg.size();
    auto byte = [&](int i) { return quint8(jpeg.at(i)); };
    if (n < 4 || byte(0) != 0xFF || byte(1) != 0xD8)
        return false;

    int pos = 2;
    while (pos + 4 <= n) {
        if (byte(pos) != 0xFF)
            return false;
        const quint8 marker = byte(pos + 1);
        // the picture data begins: no metadata after it
        if (marker == 0xDA || marker == 0xD9)
            return true;
        const int len = (byte(pos + 2) << 8) | byte(pos + 3);
        const int seg = pos + 4;
        const int end = pos + 2 + len;
        if (len < 2 || end > n)
            return false;
        if (marker == 0xE1 && len >= 2 + 29
                && jpeg.mid(seg, 29) == QByteArray("http://ns.adobe.com/xap/1.0/\0", 29)) {
            // XMP: metadata only, the whole block goes
            jpeg.remove(pos, end - pos);
            continue;
        }
        if (marker == 0xE1 && len >= 2 + 6 + 8 && jpeg.mid(seg, 6) == QByteArray("Exif\0\0", 6)) {
            const int tiff = seg + 6;
            const int tiffEnd = end;
            const bool le = jpeg.mid(tiff, 2) == "II";
            if (!le && jpeg.mid(tiff, 2) != "MM")
                return false;
            auto u16 = [&](int at) -> int {
                return le ? byte(at) | (byte(at + 1) << 8) : (byte(at) << 8) | byte(at + 1);
            };
            auto u32 = [&](int at) -> qint64 {
                return le ? qint64(byte(at)) | (qint64(byte(at + 1)) << 8) | (qint64(byte(at + 2)) << 16) | (qint64(byte(at + 3)) << 24)
                          : (qint64(byte(at)) << 24) | (qint64(byte(at + 1)) << 16) | (qint64(byte(at + 2)) << 8) | qint64(byte(at + 3));
            };
            auto inside = [&](qint64 at, qint64 size) { return at >= tiff && size >= 0 && at + size <= tiffEnd; };
            auto zero = [&](qint64 at, qint64 size) { for (qint64 i = 0; i < size; ++i) jpeg[int(at + i)] = '\0'; };

            const qint64 ifd0 = tiff + u32(tiff + 4);
            if (!inside(ifd0, 2))
                return false;
            const int count0 = u16(int(ifd0));
            if (!inside(ifd0 + 2, qint64(count0) * 12))
                return false;
            for (int e = 0; e < count0; ++e) {
                const int entry = int(ifd0 + 2 + e * 12);
                if (u16(entry) != 0x8825)
                    continue;
                // the GPS block: its values, then its entries, then empty
                const qint64 gps = tiff + u32(entry + 8);
                if (!inside(gps, 2))
                    return false;
                const int count = u16(int(gps));
                if (!inside(gps + 2, qint64(count) * 12 + 4))
                    return false;
                for (int g = 0; g < count; ++g) {
                    const int ge = int(gps + 2 + g * 12);
                    static const int sizes[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
                    const int type = u16(ge + 2);
                    const qint64 size = (type > 0 && type < 13 ? sizes[type] : 1) * u32(ge + 4);
                    if (size > 4) {
                        const qint64 at = tiff + u32(ge + 8);
                        if (!inside(at, size))
                            return false;
                        zero(at, size);
                    }
                }
                zero(gps, 2 + qint64(count) * 12 + 4);
            }
        }
        pos = end;
    }
    return true;
}

}

#endif
