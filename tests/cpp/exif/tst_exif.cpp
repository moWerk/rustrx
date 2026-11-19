// SPDX-License-Identifier: GPL-3.0-or-later
// src/Exif.h: what a photo says about itself goes, which way up it is stays.
// Planted-bug: keep the old EXIF block (only the orientation should be
// left): theCameraAndPlaceAreGone fails. Planted-bug: stop at the picture
// without cutting what follows its end: nothingAfterThePicture fails.
// Level 1, planted-bug: leave the values a GPS entry points to:
// levelOneTakesOnlyThePlace fails; keep XMP at level 1: the same fails;
// skip a GPS value outside the block instead of refusing it:
// levelOneRefusesAGpsValueOutside fails.
// Planted-bug: no fill-byte skip: fillBytesAreSkipped fails; no standalone
// markers: standaloneMarkersAreSkipped fails.
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

    static int tiffSize() { return tiff().size() + 12; }   // with "Canon EOS 5D"

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
        j += segment(0xE0, QByteArray("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14));
        j += segment(0xE1, QByteArray("Exif\0\0", 6) + tiff() + "Canon EOS 5D");
        j += segment(0xE1, QByteArray("http://ns.adobe.com/xap/1.0/\0", 29) + "<x:xmpmeta exif:GPSLatitude=\"52,31N\"/>");
        j += segment(0xE2, QByteArray("ICC_PROFILE\0", 12) + "colours");
        j += segment(0xE2, QByteArray("MPF\0", 4) + "index");
        j += segment(0xED, QByteArray("Photoshop 3.0\0", 14) + "byline");
        j += segment(0xFE, QByteArray("taken at home"));
        j += picture();
        return j;
    }

    // the scan: a stuffed 0xFF00 and a restart marker inside, then the end
    static QByteArray picture() { return QByteArray("\xFF\xDA\x00\x02pic\xFF\x00ture\xFF\xD0more\xFF\xD9", 21); }

    static int le16(const QByteArray &b, int at) { return quint8(b.at(at)) | (quint8(b.at(at + 1)) << 8); }

private slots:
    void theCameraAndPlaceAreGone()
    {
        QByteArray j = jpeg();
        QVERIFY(Exif::strip(j));
        QByteArray rationals;
        for (quint32 v : { 52u, 1u, 31u })
            put32(rationals, v);
        QVERIFY(!j.contains(rationals));
        for (const char *gone : { "GPSLatitude", "Canon", "MPF", "byline", "taken at home" })
            QVERIFY2(!j.contains(gone), gone);
    }

    void theOrientationAndColoursStay()
    {
        QByteArray j = jpeg();
        QVERIFY(Exif::strip(j));
        QByteArray want("\xFF\xD8", 2);
        want += segment(0xE0, QByteArray("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14));
        want += Exif::orientationOnly(6);
        want += segment(0xE2, QByteArray("ICC_PROFILE\0", 12) + "colours");
        want += picture();
        QCOMPARE(j, want);
        // the new block reads back
        QCOMPARE(Exif::orientation(Exif::orientationOnly(6), 10, Exif::orientationOnly(6).size()), 6);
    }

    void noOrientationNoExif()
    {
        QByteArray t("II*\0", 4);
        put32(t, 8);
        put16(t, 1);
        entry(t, 0x0110, 2, 4, 0x43414E);   // Model, inline
        put32(t, 0);
        QByteArray j("\xFF\xD8", 2);
        j += segment(0xE1, QByteArray("Exif\0\0", 6) + t);
        j += picture();
        QVERIFY(Exif::strip(j));
        QCOMPARE(j, QByteArray("\xFF\xD8", 2) + picture());
    }

    void nothingAfterThePicture()
    {
        QByteArray j = jpeg() + QByteArray("\xFF\xD8\xFF\xE1second image with its own EXIF", 34);
        QVERIFY(Exif::strip(j));
        QVERIFY(j.endsWith(picture()));
        QVERIFY(!j.contains("second image"));
    }

    void levelOneTakesOnlyThePlace()
    {
        const QByteArray before = jpeg();
        QByteArray j = before;
        QVERIFY(Exif::strip(j, 1));
        // the GPS values, inline ("N") and pointed to, are zero
        QByteArray rationals;
        for (quint32 v : { 52u, 1u, 31u })
            put32(rationals, v);
        QVERIFY(!j.contains(rationals));
        // the EXIF block is where it was, as long; the pointer to the GPS
        // IFD stays, the IFD is empty
        const int exif = before.indexOf("Exif");
        QCOMPARE(j.indexOf("Exif"), exif);
        const int tiff = exif + 6;
        QCOMPARE(le16(j, tiff + 8 + 2 + 12), 0x8825);
        QCOMPARE(le16(j, tiff + 38), 0);
        QCOMPARE(j.mid(tiff + 38, 2 + 2 * 12 + 4 + 24), QByteArray(2 + 2 * 12 + 4 + 24, '\0'));
        // the rest stays: the orientation, the camera, IPTC, the comment, ICC
        QCOMPARE(Exif::orientation(j, tiff, tiff + tiffSize()), 6);
        for (const char *kept : { "Canon EOS 5D", "byline", "taken at home", "ICC_PROFILE", "JFIF" })
            QVERIFY2(j.contains(kept), kept);
        // XMP can repeat the place, MPF points to pictures with their own
        for (const char *gone : { "GPSLatitude", "xap", "MPF" })
            QVERIFY2(!j.contains(gone), gone);
        QVERIFY(j.endsWith(picture()));
    }

    void levelOneNothingAfterThePicture()
    {
        QByteArray j = jpeg() + QByteArray("\xFF\xD8\xFF\xE1second image with its own EXIF", 34);
        QVERIFY(Exif::strip(j, 1));
        QVERIFY(!j.contains("second image"));
    }

    void levelOneRefusesAGpsValueOutside()
    {
        QByteArray t = tiff();
        // GPSLatitude's offset (entry 2 of the GPS IFD) past the block
        const int at = 38 + 2 + 12 + 8;
        t[at] = char(0xF0);
        QByteArray j("\xFF\xD8", 2);
        j += segment(0xE1, QByteArray("Exif\0\0", 6) + t);
        j += picture();
        QVERIFY(!Exif::strip(j, 1));
    }

    void levelZeroLeavesIt()
    {
        QByteArray j = jpeg();
        QVERIFY(Exif::strip(j, 0));
        QCOMPARE(j, jpeg());
    }

    void fillBytesAreSkipped()
    {
        QByteArray j("\xFF\xD8\xFF\xFF", 4);
        j += segment(0xE0, QByteArray("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14));
        j += QByteArray("\xFF\xFF\xFF", 3) + segment(0xFE, QByteArray("taken at home"));
        j += picture();
        QVERIFY(Exif::strip(j));
        QVERIFY(!j.contains("taken at home"));
        QVERIFY(j.contains("JFIF"));
        QVERIFY(j.endsWith(picture()));
    }

    void standaloneMarkersAreSkipped()
    {
        QByteArray j("\xFF\xD8", 2);
        j += segment(0xE0, QByteArray("JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14));
        j += QByteArray("\xFF\x01\xFF\xD3", 4);   // TEM, RST3
        j += segment(0xFE, QByteArray("taken at home"));
        j += picture();
        QVERIFY(Exif::strip(j));
        QVERIFY(!j.contains("taken at home"));
        QVERIFY(j.endsWith(picture()));
    }

    void notAJpegIsRefused()
    {
        QByteArray png("\x89PNG\r\n\x1a\n", 8);
        QVERIFY(!Exif::strip(png));
    }

    void aCutFileIsRefused()
    {
        QByteArray j = jpeg();
        j.chop(2);   // no end of the picture
        QVERIFY(!Exif::strip(j));
        QByteArray k = jpeg().left(30);   // a block longer than the file
        QVERIFY(!Exif::strip(k));
    }
};

QTEST_APPLESS_MAIN(TestExif)
#include "tst_exif.moc"
