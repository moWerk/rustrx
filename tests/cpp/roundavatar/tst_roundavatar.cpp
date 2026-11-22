// SPDX-License-Identifier: GPL-3.0-or-later
// src/RoundAvatar.h and AvatarProvider.h: a pre-rounded picture looks as
// the layer with RoundMask drew it.
// Planted-bug: centre the coverage on the pixel's corner (x instead of
// x + 0.5): edgeMatchesTheShader fails. Planted-bug: crop from the left:
// cropsTheCentre fails. Planted-bug: drop the prepended slash in parse:
// parsesTheId fails. Planted-bug: take coverSquare's offsets from the
// unscaled picture: coverSquareCutsTheMiddle fails.
#include <QTemporaryDir>
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
