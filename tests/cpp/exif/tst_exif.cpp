// SPDX-License-Identifier: GPL-3.0-or-later
// src/Exif.h: the place a photo was taken goes, the rest stays.
// Planted-bug: skip zeroing the GPS values that live outside the entries
// (the latitude's rationals): the test that looks for them fails.
#include <QtTest>
#include "Exif.h"

class TestExif : public QObject
{
    Q_OBJECT

    static void put16(QByteArray &b, int v) { b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF)); }
    static void put32(QByteArray &b, quint32 v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF)); }
    static void entry(QByteArray &b, int tag, int type, quint32 count, quint32 value)
    {
        put16(b, tag); put16(b, type); put32(b, count); put32(b, value);
    }

    // a little-endian TIFF: IFD0 with Orientation and the GPS pointer, the
    // GPS block with the latitude (3 rationals elsewhere) and "N"
    static QByteArray tiff()
    {
        QByteArray t("II*\0", 4);
        put32(t, 8);
        // IFD0 at 8: 2 entries
        put16(t, 2);
        entry(t, 0x0112, 3, 1, 6);          // Orientation: rotate
        entry(t, 0x8825, 4, 1, 38);         // GPS IFD at 38
        put32(t, 0);
        // GPS IFD at 38: 2 entries, the values at 68
        put16(t, 2);
        entry(t, 0x0001, 2, 2, 'N');        // GPSLatitudeRef "N\0", inline
        entry(t, 0x0002, 5, 3, 68);         // GPSLatitude, 24 bytes at 68
        put32(t, 0);
        // the latitude: 52/1 31/1 1234/100
        for (quint32 v : { 52u, 1u, 31u, 1u, 1234u, 100u })
            put32(t, v);
        return t;
    }

    static QByteArray segment(int marker, const QByteArray &body)
    {
        QByteArray s;
        s.append(char(0xFF)); s.append(char(marker));
        const int len = body.size() + 2;
        s.append(char(len >> 8)); s.append(char(len & 0xFF));
        return s + body;
    }

    static QByteArray jpeg()
    {
        QByteArray j("\xFF\xD8", 2);
        j += segment(0xE1, QByteArray("Exif\0\0", 6) + tiff());
        j += segment(0xE1, QByteArray("http://ns.adobe.com/xap/1.0/\0", 29) + "<x:xmpmeta exif:GPSLatitude=\"52,31N\"/>");
        j += QByteArray("\xFF\xDA\x00\x02picture\xFF\xD9", 13);
        return j;
    }

private slots:
    void theLatitudeIsGone()
    {
        QByteArray j = jpeg();
        QVERIFY(Exif::stripLocation(j));
        QByteArray rationals;
        for (quint32 v : { 52u, 1u, 31u })
            put32(rationals, v);
        QVERIFY(!j.contains(rationals));
        QVERIFY(!j.contains("GPSLatitude"));
    }

    void theRestStays()
    {
        QByteArray j = jpeg();
        QVERIFY(Exif::stripLocation(j));
        // the orientation entry, unchanged
        QByteArray orientation;
        entry(orientation, 0x0112, 3, 1, 6);
        QVERIFY(j.contains(orientation));
        QVERIFY(j.endsWith(QByteArray("\xFF\xDA\x00\x02picture\xFF\xD9", 13)));
    }

    void notAJpegIsRefused()
    {
        QByteArray png("\x89PNG\r\n\x1a\n", 8);
        QVERIFY(!Exif::stripLocation(png));
    }

    void aBrokenExifIsRefused()
    {
        QByteArray j = jpeg();
        // the GPS pointer far outside the block
        const int at = j.indexOf(QByteArray("\x25\x88\x04\x00", 4));
        QVERIFY(at > 0);
        j[at + 8] = char(0xFF); j[at + 9] = char(0xFF);
        QVERIFY(!Exif::stripLocation(j));
    }
};

QTEST_APPLESS_MAIN(TestExif)
#include "tst_exif.moc"
