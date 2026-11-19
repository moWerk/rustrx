// SPDX-License-Identifier: GPL-3.0-or-later
// src/Png.h: what a PNG says about itself goes, the picture stays as it is.
// Planted-bug: drop iTXt at level 1 whatever its keyword:
// levelOneTakesOnlyThePlace fails. Planted-bug: keep tIME at level 2:
// levelTwoTakesAll fails. Planted-bug: stop at IEND without cutting what
// follows: nothingAfterTheEnd fails. Planted-bug: no length check:
// aHugeLengthIsRefused hangs (the length read as a negative int).
#include <QBuffer>
#include <QImage>
#include <QtTest>
#include "Png.h"

class TestPng : public QObject
{
    Q_OBJECT

    static quint32 crc(const QByteArray &b)
    {
        quint32 c = 0xFFFFFFFF;
        for (char ch : b) {
            c ^= quint8(ch);
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        }
        return c ^ 0xFFFFFFFF;
    }
    static void put32(QByteArray &b, quint32 v) { for (int i = 3; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF)); }
    static QByteArray chunk(const char *type, const QByteArray &data)
    {
        QByteArray c;
        put32(c, quint32(data.size()));
        const QByteArray body = QByteArray(type, 4) + data;
        c += body;
        put32(c, crc(body));
        return c;
    }

    // a real 4x4 picture from Qt: its signature and IHDR, the rest
    static QByteArray picture()
    {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    static int afterIhdr() { return 8 + 12 + 13; }
    // a literal with its NULs, without the last one
    template <int N> static QByteArray lit(const char (&s)[N]) { return QByteArray(s, N - 1); }

    static const QByteArray &comment() { static const QByteArray c = chunk("tEXt", lit("Comment\0taken at home")); return c; }
    static const QByteArray &xmp() { static const QByteArray c = chunk("iTXt", lit("XML:com.adobe.xmp\0\0\0\0\0<x:xmpmeta exif:GPSLatitude=\"52,31N\"/>")); return c; }
    static const QByteArray &title() { static const QByteArray c = chunk("iTXt", lit("Title\0\0\0\0\0Garden")); return c; }
    static const QByteArray &time() { static const QByteArray c = chunk("tIME", lit("\x07\xEA\x0B\x13\x0C\x00\x00")); return c; }
    static const QByteArray &exif() { static const QByteArray c = chunk("eXIf", lit("MM\0*\0\0\0\x08GPS")); return c; }
    static const QByteArray &rawProfile() { static const QByteArray c = chunk("zTXt", lit("Raw profile type exif\0\0x")); return c; }

    static QByteArray png()
    {
        const QByteArray p = picture();
        return p.left(afterIhdr()) + comment() + xmp() + title() + time() + exif() + rawProfile() + p.mid(afterIhdr());
    }

private slots:
    void levelTwoTakesAll()
    {
        QByteArray p = png();
        QVERIFY(Png::strip(p, 2));
        // the picture's chunks byte for byte, CRCs with them
        QCOMPARE(p, picture());
        QImage back;
        QVERIFY(back.loadFromData(p, "PNG"));
        QCOMPARE(back.pixel(1, 1), QColor(Qt::red).rgb());
    }

    void levelOneTakesOnlyThePlace()
    {
        QByteArray p = png();
        QVERIFY(Png::strip(p, 1));
        const QByteArray o = picture();
        QCOMPARE(p, o.left(afterIhdr()) + comment() + title() + time() + o.mid(afterIhdr()));
        QVERIFY(QImage::fromData(p, "PNG").width() == 4);
    }

    void levelZeroLeavesIt()
    {
        QByteArray p = png();
        QVERIFY(Png::strip(p, 0));
        QCOMPARE(p, png());
    }

    void nothingAfterTheEnd()
    {
        QByteArray p = png() + "trailing data with the place";
        QVERIFY(Png::strip(p, 1));
        QVERIFY(!p.contains("trailing"));
        QVERIFY(p.endsWith(chunk("IEND", QByteArray())));
    }

    void notAPngIsRefused()
    {
        QByteArray j("\xFF\xD8\xFF\xE0", 4);
        QVERIFY(!Png::strip(j));
    }

    void aCutFileIsRefused()
    {
        QByteArray p = png();
        p.chop(12);   // no IEND
        QVERIFY(!Png::strip(p));
        QByteArray q = png().left(afterIhdr() + 20);   // a chunk longer than the file
        QVERIFY(!Png::strip(q));
    }

    void aHugeLengthIsRefused()
    {
        // 0xFFFFFFF4 as an int is -12: the chunk would end where it starts
        const QByteArray p = picture();
        QByteArray bad;
        put32(bad, 0xFFFFFFF4);
        bad += "abCd";
        QByteArray q = p.left(afterIhdr()) + bad + QByteArray(16, '\0') + p.mid(afterIhdr());
        QVERIFY(!Png::strip(q));
    }
};

QTEST_MAIN(TestPng)
#include "tst_png.moc"
