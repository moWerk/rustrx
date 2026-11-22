// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROUNDAVATAR_H
#define ROUNDAVATAR_H

#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QString>
#include <QtMath>

// A room or account picture, rounded once on the CPU instead of through a
// layer and RoundMask per row: scaled to cover a square of `size` and cut to
// its centre, as Image's PreserveAspectCrop shows it, then every pixel takes
// RoundMask's alpha (the same distance and the same one-pixel soft edge, at
// the pixel's centre). `radius` is the corner as a share of the side (0.5 a
// circle). A null image when the file cannot be read.
namespace RoundAvatar {

// RoundMask's coverage of the pixel centred at (px, py) in a w x h box with
// the corner r
inline qreal coverage(qreal px, qreal py, qreal w, qreal h, qreal r)
{
    const qreal qx = qAbs(px - w / 2) - (w / 2 - r);
    const qreal qy = qAbs(py - h / 2) - (h / 2 - r);
    const qreal ox = qMax<qreal>(qx, 0), oy = qMax<qreal>(qy, 0);
    const qreal d = qSqrt(ox * ox + oy * oy) + qMin<qreal>(qMax(qx, qy), 0) - r;
    // 1 - smoothstep(-1, 0, d)
    const qreal t = qBound<qreal>(0, d + 1, 1);
    return 1 - t * t * (3 - 2 * t);
}

// scaled to cover size x size, cut to its centre
inline QImage coverSquare(const QImage &img, int size)
{
    const QImage scaled = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy((scaled.width() - size) / 2, (scaled.height() - size) / 2, size, size);
}

inline QImage render(const QString &path, int size, qreal radius)
{
    QImageReader reader(path);
    const QSize full = reader.size();
    if (size > 0 && full.isValid() && !full.isEmpty()) {
        const QSize cover = full.scaled(size, size, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(cover);
        reader.setScaledClipRect(QRect((cover.width() - size) / 2, (cover.height() - size) / 2, size, size));
    }
    QImage img = reader.read();
    if (img.isNull()) return QImage();
    // no size asked for: the picture's own shorter side
    if (size <= 0) size = qMin(img.width(), img.height());
    // a format that does not say its size before reading: cut here
    if (img.size() != QSize(size, size)) img = coverSquare(img, size);
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal r = qBound<qreal>(0, radius, 0.5) * size;
    if (r <= 0) return img;
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const qreal a = coverage(x + 0.5, y + 0.5, img.width(), img.height(), r);
            if (a >= 1) continue;
            const QRgb p = line[x];
            line[x] = qRgba(qRound(qRed(p) * a), qRound(qGreen(p) * a), qRound(qBlue(p) * a), qRound(qAlpha(p) * a));
        }
    }
    return img;
}

}

#endif
