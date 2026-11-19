// SPDX-License-Identifier: GPL-3.0-or-later
// src/Mp4.h: a video's length and its size as shown, from its header.
// Planted-bug: ignore the track's matrix: the portrait test fails.
// strip(), planted-bug: skip the track's udta at level 1:
// levelOneTakesOnlyThePlace fails; zero the times at level 1 too: the same
// fails; leave mdhd's times (v1, 64 bit) at level 2: levelTwoTakesAll
// fails. B10, planted-bug: compare a 64-bit box size signed again:
// aHugeBoxSizeIsRefused hangs.
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

    static void put64(QByteArray &b, quint64 v) { put32(b, quint32(v >> 32)); put32(b, quint32(v)); }
    // a literal with its NULs, without the last one
    template <int N> static QByteArray lit(const char (&s)[N]) { return QByteArray(s, N - 1); }

    // a phone's video, as small as can be: times in every header, the
    // place in moov's and the track's udta and in an Apple meta, a title,
    // XMP at the top, and stco pointing into mdat's known payload
    static QByteArray phone(quint32 chunkOffset)
    {
        QByteArray mvhdBody;
        put32(mvhdBody, 0);                 // version 0
        put32(mvhdBody, 0x11111111);        // created
        put32(mvhdBody, 0x22222222);        // modified
        put32(mvhdBody, 1000);
        put32(mvhdBody, 3000);
        mvhdBody += QByteArray(80, '\0');
        QByteArray tkhdBody;
        put32(tkhdBody, 0);
        put32(tkhdBody, 0x33333333);
        put32(tkhdBody, 0x44444444);
        tkhdBody += QByteArray(40 - 12, '\0');
        put32(tkhdBody, 0x10000); put32(tkhdBody, 0);
        tkhdBody += QByteArray(28, '\0');
        put32(tkhdBody, 640u << 16);
        put32(tkhdBody, 480u << 16);
        QByteArray mdhdBody;
        put32(mdhdBody, 0x01000000);        // version 1
        put64(mdhdBody, 0x5555555555555555ull);
        put64(mdhdBody, 0x6666666666666666ull);
        put32(mdhdBody, 1000);
        put64(mdhdBody, 3000);
        put32(mdhdBody, 0);
        QByteArray stcoBody;
        put32(stcoBody, 0);
        put32(stcoBody, 1);
        put32(stcoBody, chunkOffset);
        const QByteArray xyz = box("\xA9xyz", lit("\0\x0c\x15\xc7+52.5200+013.4050/"));
        const QByteArray name = box("\xA9nam", lit("\0\x06\0\0Garden"));
        const QByteArray trak = box("trak", box("tkhd", tkhdBody)
                                    + box("mdia", box("mdhd", mdhdBody)
                                          + box("minf", box("stbl", box("stco", stcoBody))))
                                    + box("udta", box("loci", lit("\0\0\0\0place"))));
        const QByteArray keys = box("keys", lit("\0\0\0\0\0\0\0\x01\0\0\0\x2dmdtacom.apple.quicktime.location.ISO6709"));
        const QByteArray moov = box("moov", box("mvhd", mvhdBody) + trak
                                    + box("udta", xyz + name)
                                    + box("meta", box("hdlr", QByteArray(24, '\0')) + keys));
        static const char xmpUuid[] = "\xBE\x7A\xCF\xCB\x97\xA9\x42\xE8\x9C\x71\x99\x94\x91\xE3\xAF\xAC";
        const QByteArray uuid = box("uuid", QByteArray(xmpUuid, 16) + "<x:xmp GPSLatitude/>");
        return box("ftyp", "isom\0\0\0\0isom") + moov + uuid + box("mdat", payload());
    }
    static QByteArray payload() { return QByteArray("frames of the video, untouched") + QByteArray(1000, 'v'); }
    static QByteArray phone()
    {
        // stco points at mdat's payload; its size does not depend on the value
        const QByteArray draft = phone(0);
        return phone(quint32(draft.size() - payload().size()));
    }
    static QByteArray stripped(int level, bool *ok)
    {
        QByteArray bytes = phone();
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadWrite);
        *ok = Mp4::strip(buffer, level);
        buffer.close();
        return bytes;
    }
    // the box type at each place the original had `type`
    static QList<QByteArray> typesWhere(const QByteArray &after, const char *type)
    {
        const QByteArray before = phone();
        QList<QByteArray> found;
        for (int at = before.indexOf(type); at >= 0; at = before.indexOf(type, at + 1))
            found << after.mid(at, 4);
        return found;
    }
    static quint32 at32(const QByteArray &b, int at)
    {
        return (quint32(quint8(b.at(at))) << 24) | (quint32(quint8(b.at(at + 1))) << 16)
             | (quint32(quint8(b.at(at + 2))) << 8) | quint32(quint8(b.at(at + 3)));
    }
    // a header's creation time, v0 or v1 (first 32 bits)
    static quint32 created(const QByteArray &b, const char *type) { return at32(b, b.indexOf(type) + 8); }
    static void sameFrames(const QByteArray &after)
    {
        const QByteArray before = phone();
        QCOMPARE(after.size(), before.size());
        QVERIFY(after.endsWith(payload()));
        const int stco = before.indexOf("stco");
        QCOMPARE(after.mid(stco, 16), before.mid(stco, 16));
        QCOMPARE(int(at32(after, stco + 12)), before.size() - payload().size());
        QCOMPARE(after.mid(before.size() - payload().size(), 7), QByteArray("frames "));
        QBuffer buffer(const_cast<QByteArray *>(&after));
        buffer.open(QIODevice::ReadOnly);
        const Mp4::Info i = Mp4::read(buffer);
        QCOMPARE(i.durationMs, qint64(3000));
        QCOMPARE(i.width, 640);
        QCOMPARE(i.height, 480);
    }

private slots:
    void levelOneTakesOnlyThePlace()
    {
        bool ok = false;
        const QByteArray f = stripped(1, &ok);
        QVERIFY(ok);
        sameFrames(f);
        QCOMPARE(typesWhere(f, "\xA9xyz"), QList<QByteArray>() << "free");
        QCOMPARE(typesWhere(f, "loci"), QList<QByteArray>() << "free");
        QCOMPARE(typesWhere(f, "meta"), QList<QByteArray>() << "free");
        QCOMPARE(typesWhere(f, "uuid"), QList<QByteArray>() << "free");
        for (const char *gone : { "+52.5200", "place", "ISO6709", "GPSLatitude" })
            QVERIFY2(!f.contains(gone), gone);
        // the rest stays: the title, the user data around it, every time
        QVERIFY(f.contains("Garden"));
        QCOMPARE(typesWhere(f, "udta"), QList<QByteArray>() << "udta" << "udta");
        QCOMPARE(created(f, "mvhd"), 0x11111111u);
        QCOMPARE(created(f, "tkhd"), 0x33333333u);
        QCOMPARE(created(f, "mdhd"), 0x55555555u);
    }

    void levelTwoTakesAll()
    {
        bool ok = false;
        const QByteArray f = stripped(2, &ok);
        QVERIFY(ok);
        sameFrames(f);
        QCOMPARE(typesWhere(f, "udta"), QList<QByteArray>() << "free" << "free");
        QCOMPARE(typesWhere(f, "meta"), QList<QByteArray>() << "free");
        QCOMPARE(typesWhere(f, "uuid"), QList<QByteArray>() << "free");
        for (const char *gone : { "+52.5200", "place", "ISO6709", "GPSLatitude", "Garden" })
            QVERIFY2(!f.contains(gone), gone);
        // both times of each header, 32 and 64 bit
        QCOMPARE(f.mid(f.indexOf("mvhd") + 8, 8), QByteArray(8, '\0'));
        QCOMPARE(f.mid(f.indexOf("tkhd") + 8, 8), QByteArray(8, '\0'));
        QCOMPARE(f.mid(f.indexOf("mdhd") + 8, 16), QByteArray(16, '\0'));
        // the time scale and length after them stay
        QCOMPARE(at32(f, f.indexOf("mdhd") + 24), 1000u);
    }

    void levelZeroLeavesIt()
    {
        bool ok = false;
        QCOMPARE(stripped(0, &ok), phone());
        QVERIFY(ok);
    }

    void notAnMp4IsRefused()
    {
        QByteArray webm = QByteArray("\x1a\x45\xdf\xa3webm", 8) + QByteArray(100, '\0');
        QBuffer buffer(&webm);
        buffer.open(QIODevice::ReadWrite);
        QVERIFY(!Mp4::strip(buffer, 1));
        // no header at all
        QByteArray bare = box("ftyp", "isom\0\0\0\0isom") + box("mdat", payload());
        QBuffer b2(&bare);
        b2.open(QIODevice::ReadWrite);
        QVERIFY(!Mp4::strip(b2, 1));
        // boxes, but no ftyp first
        QByteArray noFtyp = phone().mid(12);
        QBuffer b3(&noFtyp);
        b3.open(QIODevice::ReadWrite);
        QVERIFY(!Mp4::strip(b3, 1));
    }

    void aCutHeaderIsRefused()
    {
        QByteArray f = phone();
        // moov claims more than the file has
        const int moov = f.indexOf("moov") - 4;
        f[moov] = char(0x7F);
        QBuffer buffer(&f);
        buffer.open(QIODevice::ReadWrite);
        QVERIFY(!Mp4::strip(buffer, 2));
    }

    void aHugeBoxSizeIsRefused()
    {
        // a 64-bit size above qint64's range before the header: read as
        // signed it was -12 and led back to ftyp, round and round
        const QByteArray ftyp = box("ftyp", "isom\0\0\0\0isom");
        QCOMPARE(ftyp.size(), 12);
        QByteArray huge;
        put32(huge, 1);
        huge += "free";
        put64(huge, quint64(0) - 12);
        QByteArray f = ftyp + huge + camera();
        QCOMPARE(info(f).width, 0);
        QBuffer buffer(&f);
        buffer.open(QIODevice::ReadWrite);
        QVERIFY(!Mp4::strip(buffer, 2));
    }
};

QTEST_APPLESS_MAIN(TestMp4)
#include "tst_mp4.moc"
