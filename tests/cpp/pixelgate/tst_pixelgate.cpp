// SPDX-License-Identifier: GPL-3.0-or-later
// No picture bomb (PixelGate.h, audit 0.8.4 UC3): the header's size decides
// before anything is decoded. Pictures as phones send them pass, in every
// format Rustrx shows inline; one past 40 MP or with an absurd side does not,
// whatever its name says. Nor does any other format (audit UC2).
#include "PixelGate.h"

#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

static quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool made = false;
    if (!made) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    quint32 c = 0xffffffffu;
    for (char b : data)
        c = table[(c ^ quint8(b)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    qToBigEndian(v, reinterpret_cast<uchar *>(b.data()));
    return b;
}

static QByteArray chunk(const char *type, const QByteArray &data)
{
    const QByteArray body = QByteArray(type) + data;
    return be32(quint32(data.size())) + body + be32(crc32(body));
}

// a PNG header of any size, the kind that decodes whole (grey, Adam7 at
// will): a few hundred bytes that would take w x h bytes and more
static QByteArray pngHeader(quint32 w, quint32 h, bool interlaced, char colour = 0)
{
    QByteArray ihdr = be32(w) + be32(h);
    ihdr += char(8);
    ihdr += colour;
    ihdr += char(0);
    ihdr += char(0);
    ihdr += char(interlaced ? 1 : 0);
    return QByteArray("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", QByteArray("\x78\x9c\x03\x00\x00\x00\x00\x01", 8))
            + chunk("IEND", QByteArray());
}

// a GIF's screen of any size, one 1x1 frame
static QByteArray gif(quint16 w, quint16 h)
{
    QByteArray g("GIF89a");
    g += char(w & 0xff); g += char(w >> 8); g += char(h & 0xff); g += char(h >> 8);
    g += QByteArray("\x80\x00\x00", 3);                 // global colour table of 2
    g += QByteArray("\x00\x00\x00\xff\xff\xff", 6);
    g += QByteArray("\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00", 10);
    g += QByteArray("\x02\x02\x44\x01\x00\x3b", 6);
    return g;
}

class TestPixelGate : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &bytes)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size())
            return QString();
        return path;
    }
    QString save(const QString &name, const QImage &img, const char *format)
    {
        const QString path = m_dir.filePath(name);
        QImageWriter w(path, format);
        return w.write(img) ? path : QString();
    }

private slots:
    void bounds()
    {
        QVERIFY(PixelGate::fits(QSize(4000, 3000)));
        QVERIFY(PixelGate::fits(QSize(8000, 5000)));     // 40 MP
        QVERIFY(!PixelGate::fits(QSize(8001, 5000)));
        QVERIFY(PixelGate::fits(QSize(30000, 1000)));    // a panorama
        QVERIFY(!PixelGate::fits(QSize(40000, 10)));     // an absurd side
        QVERIFY(!PixelGate::fits(QSize(10, 40000)));
        QVERIFY(!PixelGate::fits(QSize()));
        QVERIFY(!PixelGate::fits(QSize(0, 10)));
    }

    // what phones send and Rustrx shows inline: let through, and the size
    // is read from the header in each of these formats
    void ordinaryPicturesPass_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QSize>("size");
        QTest::newRow("jpeg 12 MP") << "jpeg" << QSize(4000, 3000);
        QTest::newRow("png") << "png" << QSize(1920, 1080);
        QTest::newRow("webp") << "webp" << QSize(1600, 1200);
    }
    void ordinaryPicturesPass()
    {
        QFETCH(QString, format);
        QFETCH(QSize, size);
        if (!QImageWriter::supportedImageFormats().contains(format.toLatin1()))
            QSKIP("no writer for this format here");
        QImage img(size, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        // named as the core names media: no suffix to go by
        const QString path = save(QStringLiteral("pic-") + format, img, format.toLatin1().constData());
        QVERIFY(!path.isEmpty());
        QCOMPARE(PixelGate::headerSize(path), size);
        QVERIFY(PixelGate::ok(path));
    }
    // Planted-bug: headerSize without the pictureFormat check (the reader
    // probing every plugin by content, or picking one by the name): the
    // BMP, TIFF and SVG pass and reach their decoders.
    void onlyFourFormatsAreDecoded()
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        const QByteArray svg("<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"48\"/>");
        for (const char *name : { "a.svg", "a.png", "a.jpg", "a.bin", "a.img" })
            QVERIFY2(!PixelGate::ok(write(QString::fromLatin1(name), svg)), name);
        for (const char *format : { "bmp", "tiff", "ppm", "xpm", "ico" }) {
            if (!QImageWriter::supportedImageFormats().contains(format))
                continue;
            const QString path = save(QStringLiteral("other-") + QLatin1String(format), img, format);
            QVERIFY(!path.isEmpty());
            QVERIFY2(!PixelGate::ok(path), format);
        }
        // the bytes decide, not the name: a PNG named .svg is read as a PNG
        const QString png = save(QStringLiteral("p.png"), img, "png");
        QFile::rename(png, m_dir.filePath(QStringLiteral("p.svg")));
        QCOMPARE(PixelGate::headerSize(m_dir.filePath(QStringLiteral("p.svg"))), QSize(64, 48));
        QCOMPARE(PixelGate::pictureFormat(QByteArray("\xFF\xD8\xFF\xE0", 4)), QByteArray("jpeg"));
        QCOMPARE(PixelGate::pictureFormat(QByteArray("RIFF\0\0\0\0WEBP", 12)), QByteArray("webp"));
        QCOMPARE(PixelGate::pictureFormat(QByteArray("RIFF\0\0\0\0WAVE", 12)), QByteArray());
        QCOMPARE(PixelGate::pictureFormat(QByteArray("BM")), QByteArray());
    }
    void ordinaryHeadersPass()
    {
        QVERIFY(PixelGate::ok(write(QStringLiteral("adam7.img"), pngHeader(1200, 900, true))));
        QVERIFY(PixelGate::ok(write(QStringLiteral("grey.img"), pngHeader(3000, 2000, false))));
        QVERIFY(PixelGate::ok(write(QStringLiteral("small.gif"), gif(480, 270))));
    }

    void bombsAreRefused_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("png Adam7 8000x8000") << pngHeader(8000, 8000, true);
        QTest::newRow("png grey 20000x20000") << pngHeader(20000, 20000, false);
        QTest::newRow("png palette 9000x9000") << pngHeader(9000, 9000, false, 3);
        QTest::newRow("png absurd side") << pngHeader(100000, 2, false);
        QTest::newRow("gif 65535x65535") << gif(65535, 65535);
        QTest::newRow("gif 7000x7000") << gif(7000, 7000);
        QTest::newRow("not a picture") << QByteArray("#EXTM3U\n");
        QTest::newRow("empty") << QByteArray();
    }
    void bombsAreRefused()
    {
        QFETCH(QByteArray, bytes);
        // whatever the name says
        for (const char *name : { "bomb.img", "bomb.jpg", "bomb.gif" }) {
            const QString path = write(QLatin1String(name), bytes);
            QVERIFY(!path.isEmpty());
            QVERIFY(!PixelGate::ok(path));
        }
    }
    void jpegPast40MpIsRefused()
    {
        QImage img(6400, 6400, QImage::Format_Grayscale8);
        img.fill(128);
        const QString path = save(QStringLiteral("huge-jpeg"), img, "jpeg");
        QVERIFY(!path.isEmpty());
        QCOMPARE(PixelGate::headerSize(path), QSize(6400, 6400));
        QVERIFY(!PixelGate::ok(path));
    }
    void missingFile() { QVERIFY(!PixelGate::ok(m_dir.filePath(QStringLiteral("none")))); }

    // the same name with other bytes (downloaded again) is looked at again
    void cacheFollowsTheFile()
    {
        PixelGate::Cache cache;
        const QString path = write(QStringLiteral("again.img"), pngHeader(800, 600, false));
        QVERIFY(cache.ok(path));
        QVERIFY(write(QStringLiteral("again.img"), pngHeader(9000, 9000, true) + QByteArray(64, 0)) == path);
        QVERIFY(!cache.ok(path));
        QVERIFY(!cache.ok(path));
    }
};

QTEST_GUILESS_MAIN(TestPixelGate)
#include "tst_pixelgate.moc"
