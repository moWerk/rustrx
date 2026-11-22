// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AVATARPROVIDER_H
#define AVATARPROVIDER_H

#include <QQuickImageProvider>
#include <QUrlQuery>
#include "RoundAvatar.h"

// image://rxavatar/<file path>?v=<version>&r=<corner in thousandths of the
// side>: the picture pre-rounded (RoundAvatar), drawn as a plain Image, so
// the room list needs no layer per row and small pictures share the scene
// graph's atlas. The size is the Image's sourceSize. The version only makes
// a changed picture a new URL for Qt's pixmap cache; r changes with the
// avatar corners setting, so a new setting loads anew.
class AvatarProvider : public QQuickImageProvider
{
public:
    AvatarProvider()
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading) {}

    // the file and the corner from the id (what follows image://rxavatar/)
    static void parse(const QString &id, QString *path, qreal *radius)
    {
        const int q = id.lastIndexOf(QLatin1Char('?'));
        *path = q < 0 ? id : id.left(q);
        // the URL's path keeps one slash of "//home/..." at most
        if (!path->startsWith(QLatin1Char('/'))) path->prepend(QLatin1Char('/'));
        const QUrlQuery query(q < 0 ? QString() : id.mid(q + 1));
        *radius = query.queryItemValue(QStringLiteral("r")).toInt() / 1000.0;
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        QString path;
        qreal radius = 0;
        parse(id, &path, &radius);
        int side = 0;
        if (requestedSize.width() > 0 && requestedSize.height() > 0) side = qMin(requestedSize.width(), requestedSize.height());
        else side = qMax(requestedSize.width(), requestedSize.height());
        const QImage img = RoundAvatar::render(path, side, radius);
        if (size) *size = img.size();
        return img;
    }
};

#endif
