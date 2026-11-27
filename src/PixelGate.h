// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_PIXELGATE_H
#define RUSTRX_PIXELGATE_H

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QSize>
#include <QString>

// No picture bomb (audit 0.8.4 UC3): a small, highly compressed file (an
// interlaced or grey PNG, a GIF) decodes at its full size whatever
// sourceSize asks, and Qt 5.6 has no allocation limit. So before a
// received picture is decoded its header says how large it is, and one
// past these bounds is not decoded at all: it shows as a picture that
// failed to load. Photos up to 40 MP pass. And only JPEG, PNG, GIF and
// WebP are decoded, by their first bytes, with that decoder alone (audit
// UC2: Qt 5.6 picked any plugin, SVG and TIFF among them, by the name the
// sender's type gave the file, or by probing every plugin).
namespace PixelGate {

static const qint64 kMaxPixels = 40 * 1000 * 1000;
static const int kMaxSide = 32767;
// what QML is given for a refused picture: a file that is no picture, so
// Image and AnimatedImage end in their Error state, as for a broken file
static const char kRefusedUrl[] = "file:///dev/null";

// a header's size within the bounds (an unknown size is not)
inline bool fits(const QSize &s)
{
    return s.isValid() && !s.isEmpty() && s.width() <= kMaxSide && s.height() <= kMaxSide
            && qint64(s.width()) * s.height() <= kMaxPixels;
}

// Qt's format name for a picture's first bytes: one of the four drawn,
// else empty
inline QByteArray pictureFormat(const QByteArray &head)
{
    if (head.startsWith("\xFF\xD8\xFF"))
        return QByteArrayLiteral("jpeg");
    if (head.startsWith("\x89PNG"))
        return QByteArrayLiteral("png");
    if (head.startsWith("GIF8"))
        return QByteArrayLiteral("gif");
    if (head.size() >= 12 && head.startsWith("RIFF") && head.mid(8, 4) == "WEBP")
        return QByteArrayLiteral("webp");
    return QByteArray();
}

// a reader for an open picture file with only the decoder its bytes name
// (none for any other file: it reads nothing)
inline void onlyItsDecoder(QImageReader &reader, QIODevice *device)
{
    reader.setDecideFormatFromContent(false);
    reader.setAutoDetectImageFormat(false);
    const QByteArray format = pictureFormat(device->peek(12));
    reader.setFormat(format.isEmpty() ? QByteArrayLiteral("none") : format);
}

// the picture's size from its header, by the decoder its bytes name (not
// by the name's suffix); none for a file that is no picture drawn here
inline QSize headerSize(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QSize();
    // a GIF decodes onto its logical screen, but Qt tells the first frame's
    // size (a 1x1 frame on a 65535x65535 screen said 1x1)
    const QByteArray head = f.peek(10);
    if (pictureFormat(f.peek(12)).isEmpty())
        return QSize();
    QImageReader reader;
    reader.setDevice(&f);
    onlyItsDecoder(reader, &f);
    QSize s = reader.size();
    if (s.isValid() && head.size() == 10 && head.startsWith("GIF8")) {
        const int w = quint8(head.at(6)) | quint8(head.at(7)) << 8;
        const int h = quint8(head.at(8)) | quint8(head.at(9)) << 8;
        s = s.expandedTo(QSize(w, h));
    }
    return s;
}

inline bool ok(const QString &path)
{
    return fits(headerSize(path));
}

// ok() remembered per file and version (time and size), for QML's
// bindings: a picture downloaded again under the same name is looked at
// again. GUI thread only.
class Cache
{
public:
    bool ok(const QString &path)
    {
        const QFileInfo info(path);
        const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
        const qint64 size = info.size();
        const auto it = m_seen.constFind(path);
        if (it != m_seen.constEnd() && it->mtime == mtime && it->size == size)
            return it->ok;
        const bool fine = PixelGate::ok(path);
        if (!fine)
            qWarning().noquote() << "picture: not shown, its size is past the limit:" << headerSize(path);
        // a few hundred pictures in a session; forgotten all at once past that
        if (m_seen.size() >= kMax)
            m_seen.clear();
        m_seen.insert(path, Seen { mtime, size, fine });
        return fine;
    }

private:
    static const int kMax = 2048;
    struct Seen { qint64 mtime; qint64 size; bool ok; };
    QHash<QString, Seen> m_seen;
};

} // namespace PixelGate

#endif
