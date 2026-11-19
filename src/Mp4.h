// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_MP4_H
#define RUSTRX_MP4_H

#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QtGlobal>

// A video's length and its size as shown, from an MP4 or QuickTime file's
// header, so the event says them as other clients' videos do. The size is
// the first video track's, turned when the track is rotated (the phone's
// camera stores portrait videos sideways with a 90 degree matrix). Only
// box headers are read until the header ("moov") is found, wherever it
// sits; the picture data is skipped, not read.
// strip() takes what a video says about itself out of the same header, in
// place: nothing is moved, because the header points into the file by
// offset; a box that goes becomes a "free" box of zeros of the same size.
namespace Mp4 {

struct Info {
    int width = 0;
    int height = 0;
    qint64 durationMs = -1;
};

namespace detail {

inline quint32 u32(const QByteArray &b, int at)
{
    return (quint32(quint8(b.at(at))) << 24) | (quint32(quint8(b.at(at + 1))) << 16)
         | (quint32(quint8(b.at(at + 2))) << 8) | quint32(quint8(b.at(at + 3)));
}

inline quint64 u64(const QByteArray &b, int at)
{
    return (quint64(u32(b, at)) << 32) | u32(b, at + 4);
}

// the boxes in b[from, to): calls f(type, box start, body start, end);
// false stops. Returns false when a box does not fit.
template <typename F>
inline bool boxes(const QByteArray &b, int from, int to, F f)
{
    int pos = from;
    while (pos + 8 <= to) {
        quint64 size = u32(b, pos);
        int head = 8;
        if (size == 1) {
            if (pos + 16 > to)
                return false;
            size = u64(b, pos + 8);
            head = 16;
        } else if (size == 0) {
            size = quint64(to - pos);
        }
        if (size < quint64(head) || size > quint64(to - pos))
            return false;
        if (!f(b.mid(pos + 4, 4), pos, pos + head, int(pos + size)))
            return true;
        pos += int(size);
    }
    return true;
}

// the file's top-level boxes: calls f(type, box start, header size, box
// size); false stops. Returns false when it is no MP4 (ftyp first) or a box
// does not fit; only the box headers are read.
template <typename F>
inline bool top(QIODevice &file, F f)
{
    qint64 pos = 0;
    const qint64 end = file.size();
    while (pos + 8 <= end) {
        if (!file.seek(pos))
            return false;
        const QByteArray head = file.read(16);
        if (head.size() < 8)
            return false;
        quint64 size = u32(head, 0);
        int headSize = 8;
        if (size == 1) {
            if (head.size() < 16)
                return false;
            size = u64(head, 8);
            headSize = 16;
        } else if (size == 0) {
            size = quint64(end - pos);
        }
        // unsigned: a 64-bit size above qint64's range must not pass as negative
        if (size < quint64(headSize) || size > quint64(end - pos))
            return false;
        if (pos == 0 && head.mid(4, 4) != "ftyp")
            return false;
        if (!f(head.mid(4, 4), pos, headSize, qint64(size)))
            return true;
        pos += qint64(size);
    }
    return true;
}

}

// the header's info from moov's body (without its own box header)
inline Info fromMoov(const QByteArray &b)
{
    using namespace detail;
    Info info;
    boxes(b, 0, b.size(), [&](const QByteArray &type, int, int s, int e) {
        if (type == "mvhd" && e - s >= 32) {
            const bool v1 = quint8(b.at(s)) == 1;
            const quint32 scale = v1 ? u32(b, s + 20) : u32(b, s + 12);
            const quint64 dur = v1 ? u64(b, s + 24) : u32(b, s + 16);
            if (scale > 0)
                info.durationMs = qint64(dur * 1000 / scale);
        } else if (type == "trak" && info.width == 0) {
            boxes(b, s, e, [&](const QByteArray &t2, int, int s2, int e2) {
                if (t2 != "tkhd")
                    return true;
                const bool v1 = quint8(b.at(s2)) == 1;
                // the matrix, then width and height (16.16), at the box's end
                const int matrix = s2 + (v1 ? 52 : 40);
                if (matrix + 44 > e2)
                    return false;
                const int w = int(u32(b, matrix + 36) >> 16);
                const int h = int(u32(b, matrix + 40) >> 16);
                if (w > 0 && h > 0) {
                    // a: the matrix's first entry; 0 means turned by 90 or 270
                    const bool turned = u32(b, matrix) == 0 && u32(b, matrix + 4) != 0;
                    info.width = turned ? h : w;
                    info.height = turned ? w : h;
                }
                return false;
            });
        }
        return true;
    });
    return info;
}

// the file's info; an empty Info (durationMs -1, size 0) when it is no
// MP4 or its header cannot be read
inline Info read(QIODevice &file, qint64 maxMoov = 16 * 1024 * 1024)
{
    using namespace detail;
    Info info;
    top(file, [&](const QByteArray &type, qint64 pos, int head, qint64 size) {
        if (type != "moov")
            return true;
        if (size - head <= maxMoov && file.seek(pos + head))
            info = fromMoov(file.read(size - head));
        return false;
    });
    return info;
}

// What a video says about itself, out of an MP4 or QuickTime file opened
// for reading and writing. Level 2: every udta and meta box (in moov, in
// each track, and at the top) and uuid boxes at the top go, and the
// creation and modification times in mvhd, tkhd and mdhd become zero.
// Level 1: only the place: ©xyz and loci in the user data, meta boxes that
// mention a location (Apple's com.apple.quicktime.location.ISO6709 key),
// and XMP (udta's XMP_, the top-level XMP uuid), which can repeat it.
// Level 0 leaves the file as it is. Only moov is read into memory.
// Returns false when it is no MP4, its header cannot be read, or a write
// fails; the file is then not to be sent as clean.
inline bool strip(QIODevice &file, int level = 2, qint64 maxMoov = 16 * 1024 * 1024)
{
    using namespace detail;
    if (level <= 0)
        return true;
    static const char xmpUuid[] = "\xBE\x7A\xCF\xCB\x97\xA9\x42\xE8\x9C\x71\x99\x94\x91\xE3\xAF\xAC";
    qint64 moovPos = -1;
    int moovHead = 0;
    qint64 moovSize = 0;
    // top-level boxes to free: start, header size, size
    struct Box { qint64 pos; int head; qint64 size; };
    QList<Box> gone;
    bool ok = top(file, [&](const QByteArray &type, qint64 pos, int head, qint64 size) {
        if (type == "moov") {
            moovPos = pos;
            moovHead = head;
            moovSize = size;
        } else if (type == "meta" || type == "uuid") {
            bool drop = level >= 2;
            if (!drop && type == "uuid" && size >= head + 16 && file.seek(pos + head))
                drop = file.read(16) == QByteArray(xmpUuid, 16);
            if (drop)
                gone.append({ pos, head, size });
        }
        return true;
    });
    if (!ok || moovPos < 0 || moovSize - moovHead > maxMoov || !file.seek(moovPos + moovHead))
        return false;
    QByteArray moov = file.read(moovSize - moovHead);
    if (moov.size() != moovSize - moovHead)
        return false;

    auto toFree = [&](int box, int s, int e) {
        moov.replace(box + 4, 4, "free", 4);
        moov.replace(s, e - s, QByteArray(e - s, '\0'));
    };
    auto mentionsPlace = [&](int s, int e) {
        const QByteArray body = QByteArray::fromRawData(moov.constData() + s, e - s);
        return body.contains("location") || body.contains("\xA9xyz");
    };
    // the creation and modification times, 32 bits each in version 0, 64 in 1
    auto zeroTimes = [&](int s, int e) {
        const int n = e - s >= 4 && quint8(moov.at(s)) == 1 ? 8 : 4;
        if (s + 4 + 2 * n > e) {
            ok = false;
            return;
        }
        moov.replace(s + 4, 2 * n, QByteArray(2 * n, '\0'));
    };
    // udta and meta, in moov or in a track
    auto userData = [&](const QByteArray &type, int box, int s, int e) {
        if (type == "meta" && (level >= 2 || mentionsPlace(s, e))) {
            toFree(box, s, e);
        } else if (type == "udta" && level >= 2) {
            toFree(box, s, e);
        } else if (type == "udta") {
            ok = boxes(moov, s, e, [&](const QByteArray &t, int b, int s2, int e2) {
                if (t == "\xA9xyz" || t == "loci" || t == "XMP_" || (t == "meta" && mentionsPlace(s2, e2)))
                    toFree(b, s2, e2);
                return true;
            }) && ok;
        }
    };
    ok = boxes(moov, 0, moov.size(), [&](const QByteArray &type, int box, int s, int e) {
        if (type == "mvhd" && level >= 2) {
            zeroTimes(s, e);
        } else if (type == "trak") {
            ok = boxes(moov, s, e, [&](const QByteArray &t, int b, int s2, int e2) {
                if (t == "tkhd" && level >= 2) {
                    zeroTimes(s2, e2);
                } else if (t == "mdia" && level >= 2) {
                    ok = boxes(moov, s2, e2, [&](const QByteArray &t3, int, int s3, int e3) {
                        if (t3 == "mdhd")
                            zeroTimes(s3, e3);
                        return true;
                    }) && ok;
                } else {
                    userData(t, b, s2, e2);
                }
                return true;
            }) && ok;
        } else {
            userData(type, box, s, e);
        }
        return true;
    }) && ok;
    if (!ok || !file.seek(moovPos + moovHead) || file.write(moov) != moov.size())
        return false;

    const QByteArray zeros(1 << 16, '\0');
    for (const Box &b : gone) {
        if (!file.seek(b.pos + 4) || file.write("free", 4) != 4 || !file.seek(b.pos + b.head))
            return false;
        for (qint64 left = b.size - b.head; left > 0;) {
            const qint64 n = qMin(left, qint64(zeros.size()));
            if (file.write(zeros.constData(), n) != n)
                return false;
            left -= n;
        }
    }
    return true;
}

}

#endif
