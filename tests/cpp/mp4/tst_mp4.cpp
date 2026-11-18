// SPDX-License-Identifier: GPL-3.0-or-later
// src/Mp4.h: a video's length and its size as shown, from its header.
// Planted-bug: ignore the track's matrix: the portrait test fails.
#include <QBuffer>
#include <QtTest>
#include "Mp4.h"

class TestMp4 : public QObject
{
    Q_OBJECT

    static void put32(QByteArray &b, quint32 v) { for (int i = 3; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF)); }
    static QByteArray box(const char *type, const QByteArray &body)
    {
        QByteArray b;
        put32(b, quint32(body.size() + 8));
        return b + QByteArray(type, 4) + body;
    }
    // mvhd version 0: the time scale and the duration
    static QByteArray mvhd(quint32 scale, quint32 duration)
    {
        QByteArray b(12, '\0');
        put32(b, scale);
        put32(b, duration);
        return box("mvhd", b + QByteArray(80, '\0'));
    }
    // tkhd version 0 with the matrix's first two entries and a size
    static QByteArray tkhd(quint32 a, quint32 bEntry, int w, int h)
    {
        QByteArray b(40, '\0');
        put32(b, a); put32(b, bEntry);
        b += QByteArray(28, '\0');
        put32(b, quint32(w) << 16);
        put32(b, quint32(h) << 16);
        return box("tkhd", b);
    }
    static QByteArray file(const QByteArray &moov, bool moovFirst)
    {
        const QByteArray ftyp = box("ftyp", "isom\0\0\0\0isom");
        const QByteArray mdat = box("mdat", QByteArray(4096, 'x'));
        return moovFirst ? ftyp + moov + mdat : ftyp + mdat + moov;
    }
    static Mp4::Info info(const QByteArray &bytes)
    {
        QByteArray copy = bytes;
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        return Mp4::read(buffer);
    }
    // the camera's: an audio track first (no size), the video turned by 90
    static QByteArray camera()
    {
        return box("moov", mvhd(1000, 49220)
                   + box("trak", tkhd(0x10000, 0, 0, 0))
                   + box("trak", tkhd(0, 0x10000, 2560, 1440)));
    }

private slots:
    void portraitIsTurned()
    {
        const Mp4::Info i = info(file(camera(), false));
        QCOMPARE(i.durationMs, qint64(49220));
        QCOMPARE(i.width, 1440);
        QCOMPARE(i.height, 2560);
    }

    void landscapeStays()
    {
        const QByteArray moov = box("moov", mvhd(600, 1200) + box("trak", tkhd(0x10000, 0, 1920, 1080)));
        const Mp4::Info i = info(file(moov, true));
        QCOMPARE(i.durationMs, qint64(2000));
        QCOMPARE(i.width, 1920);
        QCOMPARE(i.height, 1080);
    }

    void notAnMp4()
    {
        const Mp4::Info i = info(QByteArray("\x1a\x45\xdf\xa3webm", 8) + QByteArray(100, '\0'));
        QCOMPARE(i.durationMs, qint64(-1));
        QCOMPARE(i.width, 0);
    }

    void cutShort()
    {
        QByteArray f = file(camera(), false);
        f.chop(30);
        const Mp4::Info i = info(f);
        QCOMPARE(i.width, 0);
    }
};

QTEST_APPLESS_MAIN(TestMp4)
#include "tst_mp4.moc"
