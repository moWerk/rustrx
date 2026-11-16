/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef SHAREADAPTOR_H
#define SHAREADAPTOR_H

#include <QObject>
#include <QVariant>
#include <QVariantMap>

class QDBusUnixFileDescriptor;
class Rustrx;

// org.sailfishos.share on /share/room (see the Rustrx constructor): the system share sheet calls share(a{sv})
// with "resources", a list of file paths or URLs, or of maps that carry the
// data itself ("data", "name", "type"), e.g. a link or a piece of text.
class ShareAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.share")
public:
    explicit ShareAdaptor(Rustrx *owner);
public slots:
    Q_SCRIPTABLE void share(const QVariantMap &config);
private:
    // the shared file, read from its descriptor into this app's cache
    static QString copyFromDescriptor(const QDBusUnixFileDescriptor &fd, const QString &cache,
                                      const QString &name, int n);
    // D-Bus hands nested values over as QDBusVariant / QDBusArgument
    static QVariant unwrap(const QVariant &v);
    static bool mayShare(const QString &path);
    Rustrx *m_owner;
};

#endif
