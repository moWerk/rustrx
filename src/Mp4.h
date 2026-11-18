// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_MP4_H
#define RUSTRX_MP4_H

#include <QByteArray>
#include <QIODevice>
#include <QtGlobal>

// A video's length and its size as shown, from an MP4 or QuickTime file's
// header, so the event says them as other clients' videos do. The size is
// the first video track's, turned when the track is rotated (the phone's
// camera stores portrait videos sideways with a 90 degree matrix). Only
// box headers are read until the header ("moov") is found, wherever it
// sits; the picture data is skipped, not read.
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

// the boxes in b[from, to): calls f(type, body start, end); false stops
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
        if (!f(b.mid(pos + 4, 4), pos + head, int(pos + size)))
            return true;
        pos += int(size);
    }
    return true;
}

}

// the header's info from moov's body (without its own box header)
inline Info fromMoov(const QByteArray &b)
{
    using namespace detail;
    Info info;
    boxes(b, 0, b.size(), [&](const QByteArray &type, int s, int e) {
        if (type == "mvhd" && e - s >= 32) {
            const bool v1 = quint8(b.at(s)) == 1;
            const quint32 scale = v1 ? u32(b, s + 20) : u32(b, s + 12);
            const quint64 dur = v1 ? u64(b, s + 24) : u32(b, s + 16);
            if (scale > 0)
                info.durationMs = qint64(dur * 1000 / scale);
        } else if (type == "trak" && info.width == 0) {
            boxes(b, s, e, [&](const QByteArray &t2, int s2, int e2) {
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
    qint64 pos = 0;
    const qint64 end = file.size();
    while (pos + 8 <= end) {
        if (!file.seek(pos))
            break;
        QByteArray head = file.read(16);
        if (head.size() < 8)
            break;
        quint64 size = u32(head, 0);
        int headSize = 8;
        if (size == 1) {
            if (head.size() < 16)
                break;
            size = u64(head, 8);
            headSize = 16;
        } else if (size == 0) {
            size = quint64(end - pos);
        }
        if (size < quint64(headSize) || qint64(size) > end - pos)
            break;
        // the first box of an MP4 is ftyp; anything else is no MP4
        if (pos == 0 && head.mid(4, 4) != "ftyp")
            break;
        if (head.mid(4, 4) == "moov") {
            if (qint64(size) - headSize > maxMoov || !file.seek(pos + headSize))
                break;
            return fromMoov(file.read(qint64(size) - headSize));
        }
        pos += qint64(size);
    }
    return Info();
}

}

#endif
