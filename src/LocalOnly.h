// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_LOCALONLY_H
#define RUSTRX_LOCALONLY_H

#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>

// The QML engines' network access, switched off: every picture QML shows
// is a file the core or Media has cached (avatars, media, custom emoji,
// Twemoji), so a remote address in a Label or Image can only come from
// someone else's text, and fetching it would tell them who looked, when
// and from where. file: and qrc: still load. The call page's WebView has
// an engine of its own and is not affected.
class LocalOnly : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        QNetworkAccessManager *nam = new QNetworkAccessManager(parent);
        nam->setNetworkAccessible(QNetworkAccessManager::NotAccessible);
        return nam;
    }
};

#endif
