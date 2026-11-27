// SPDX-License-Identifier: GPL-3.0-or-later
// src/RoundAvatar.h and AvatarProvider.h: a pre-rounded picture looks as
// the layer with RoundMask drew it.
// Planted-bug: centre the coverage on the pixel's corner (x instead of
// x + 0.5): edgeMatchesTheShader fails. Planted-bug: crop from the left:
// cropsTheCentre fails. Planted-bug: drop the prepended slash in parse:
// parsesTheId fails. Planted-bug: take coverSquare's offsets from the
// unscaled picture: coverSquareCutsTheMiddle fails. Planted-bug: leave the
// radius out of cachedFile's name: cachedFileFollowsVersionAndRadius fails.
// Planted-bug: render again when the file is there: cachedFileIsWrittenOnce
// fails.
#include <QTemporaryDir>
#include <utime.h>
#include <QtTest>
#include "AvatarProvider.h"
#include "RoundAvatar.h"

class TestRoundAvatar : public QObject
{
    Q_OBJECT

    // RoundMask's fragment shader, by hand, at the pixel's centre
    static qreal shader(int x, int y, int w, int h, qreal r)
    {
        const qreal tx = (x + 0.5) / w - 0.5, ty = (y + 0.5) / h - 0.5;
        const qreal qx = qAbs(tx * w) - (w * 0.5 - r), qy = qAbs(ty * h) - (h * 0.5 - r);
        const qreal d = qSqrt(qMax<qreal>(qx, 0) * qMax<qreal>(qx, 0) + qMax<qreal>(qy, 0) * qMax<qreal>(qy, 0))
                        + qMin<qreal>(qMax(qx, qy), 0) - r;
        const qreal t = qBound<qreal>(0, d + 1, 1);
        return 1 - t * t * (3 - 2 * t);
    }

    QTemporaryDir dir;
    QString save(const QImage &img, const char *name)
    {
        const QString p = dir.filePath(QString::fromLatin1(name));
        img.save(p, "PNG");
        return p;
    }

private slots:
    void edgeMatchesTheShader()
    {
        QImage white(64, 64, QImage::Format_RGB32);
        white.fill(Qt::white);
        const QImage out = RoundAvatar::render(save(white, "w.png"), 64, 0.5);
        QCOMPARE(out.size(), QSize(64, 64));
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                QVERIFY2(qAbs(qAlpha(out.pixel(x, y)) - qRound(255 * shader(x, y, 64, 64, 32))) <= 1,
                         qPrintable(QString("%1,%2").arg(x).arg(y)));
        QCOMPARE(qAlpha(out.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(out.pixel(32, 32)), 255);
    }

    void cropsTheCentre()
    {
        // red | green | blue, a wide picture: the square is the green third
        QImage wide(300, 100, QImage::Format_RGB32);
        wide.fill(Qt::green);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x) { wide.setPixel(x, y, qRgb(255, 0, 0)); wide.setPixel(x + 200, y, qRgb(0, 0, 255)); }
        const QImage out = RoundAvatar::render(save(wide, "wide.png"), 50, 0);
        QCOMPARE(out.size(), QSize(50, 50));
        for (int x : {2, 25, 47}) {
            const QRgb p = out.pixel(x, 25);
            QVERIFY2(qGreen(p) > 200 && qRed(p) < 40 && qBlue(p) < 40, qPrintable(QString::number(x)));
        }
    }

    void coverSquareCutsTheMiddle()
    {
        // a tall picture, red | green | blue from top to bottom, as a
        // format that does not say its size would come
        QImage tall(300, 900, QImage::Format_RGB32);
        tall.fill(Qt::green);
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x) { tall.setPixel(x, y, qRgb(255, 0, 0)); tall.setPixel(x, y + 600, qRgb(0, 0, 255)); }
        const QImage out = RoundAvatar::coverSquare(tall, 60);
        QCOMPARE(out.size(), QSize(60, 60));
        for (int y : {2, 30, 57}) {
            const QRgb p = out.pixel(30, y);
            QVERIFY2(qGreen(p) > 200 && qRed(p) < 40 && qBlue(p) < 40, qPrintable(QString::number(y)));
        }
    }

    void squareWithoutCorner()
    {
        QImage grey(40, 40, QImage::Format_RGB32);
        grey.fill(Qt::gray);
        const QImage out = RoundAvatar::render(save(grey, "g.png"), 40, 0);
        QCOMPARE(qAlpha(out.pixel(0, 0)), 255);
    }

    void ownSizeWhenNoneAsked()
    {
        QImage tall(30, 80, QImage::Format_RGB32);
        tall.fill(Qt::gray);
        QCOMPARE(RoundAvatar::render(save(tall, "t.png"), 0, 0.5).size(), QSize(30, 30));
    }

    void missingFileIsNull()
    {
        QVERIFY(RoundAvatar::render(dir.filePath("none.png"), 50, 0.5).isNull());
    }

    // Planted-bug: render with a reader that probes by content (no
    // onlyItsDecoder): the BMP avatar is decoded (audit UC2); a PNG under
    // the cache's .img name still is
    void onlyPicturesDrawnHereAreDecoded()
    {
        QImage img(40, 40, QImage::Format_RGB32);
        img.fill(Qt::red);
        const QString bmp = dir.filePath("other.img");
        QVERIFY(img.save(bmp, "BMP"));
        QVERIFY(RoundAvatar::render(bmp, 20, 0).isNull());
        const QString png = dir.filePath("room.img");
        QVERIFY(img.save(png, "PNG"));
        QCOMPARE(RoundAvatar::render(png, 20, 0).size(), QSize(20, 20));
    }

    void cachedFileIsWrittenOnce()
    {
        QImage grey(40, 40, QImage::Format_RGB32);
        grey.fill(Qt::gray);
        const QString src = save(grey, "once.png");
        const QString icons = dir.filePath("icons1");
        const QString first = RoundAvatar::cachedFile(src, 0.5, icons, 8);
        QVERIFY(!first.isEmpty());
        QCOMPARE(QFileInfo(first).path(), icons);
        // a reuse leaves the file as it is
        QFile f(first);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("kept");
        f.close();
        QCOMPARE(RoundAvatar::cachedFile(src, 0.5, icons, 8), first);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("kept"));
    }

    void cachedFileFollowsVersionAndRadius()
    {
        QImage grey(40, 40, QImage::Format_RGB32);
        grey.fill(Qt::gray);
        const QString src = save(grey, "versions.png");
        const QString icons = dir.filePath("icons2");
        const QString round = RoundAvatar::cachedFile(src, 0.5, icons, 8);
        const QString square = RoundAvatar::cachedFile(src, 0, icons, 8);
        QVERIFY(!round.isEmpty() && !square.isEmpty() && round != square);
        QCOMPARE(qAlpha(QImage(round).pixel(0, 0)), 0);
        QCOMPARE(qAlpha(QImage(square).pixel(0, 0)), 255);
        QCOMPARE(QImage(round).size(), QSize(40, 40));
        // the picture changed: a new file
        struct utimbuf later = { 1000000000, 1000000000 };
        QCOMPARE(::utime(QFile::encodeName(src).constData(), &later), 0);
        const QString changed = RoundAvatar::cachedFile(src, 0.5, icons, 8);
        QVERIFY(!changed.isEmpty() && changed != round);
    }

    void cachedFileKeepsTheLastUsed()
    {
        const QString icons = dir.filePath("icons3");
        for (int i = 0; i < 5; ++i) {
            QImage img(20, 20, QImage::Format_RGB32);
            img.fill(QColor(i * 40, 0, 0));
            QVERIFY(!RoundAvatar::cachedFile(save(img, qPrintable(QString("k%1.png").arg(i))), 0.5, icons, 3).isEmpty());
        }
        QCOMPARE(QDir(icons).entryList(QDir::Files).size(), 3);
    }

    void cachedFileNothingWithoutSource()
    {
        const QString icons = dir.filePath("icons4");
        QVERIFY(RoundAvatar::cachedFile(dir.filePath("none.png"), 0.5, icons, 8).isEmpty());
        QVERIFY(RoundAvatar::cachedFile(QString(), 0.5, icons, 8).isEmpty());
        QFile junk(dir.filePath("junk.img"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not a picture");
        junk.close();
        QVERIFY(RoundAvatar::cachedFile(junk.fileName(), 0.5, icons, 8).isEmpty());
        QVERIFY(QDir(icons).entryList(QDir::Files).isEmpty());
    }

    void parsesTheId()
    {
        QString path;
        qreal r = -1;
        AvatarProvider::parse(QStringLiteral("/home/u/a b.jpg?v=3&r=500"), &path, &r);
        QCOMPARE(path, QStringLiteral("/home/u/a b.jpg"));
        QCOMPARE(r, 0.5);
        AvatarProvider::parse(QStringLiteral("home/u/x.png?v=1&r=120"), &path, &r);
        QCOMPARE(path, QStringLiteral("/home/u/x.png"));
        QCOMPARE(r, 0.12);
    }

    void providerUsesTheSourceSize()
    {
        QImage grey(100, 60, QImage::Format_RGB32);
        grey.fill(Qt::gray);
        const QString p = save(grey, "p.png");
        AvatarProvider provider;
        QSize size;
        const QImage out = provider.requestImage(p + QStringLiteral("?v=0&r=500"), &size, QSize(48, 48));
        QCOMPARE(size, QSize(48, 48));
        QCOMPARE(qAlpha(out.pixel(0, 0)), 0);
    }
};

QTEST_MAIN(TestRoundAvatar)
#include "tst_roundavatar.moc"
