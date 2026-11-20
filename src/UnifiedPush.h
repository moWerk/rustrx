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
#ifndef UNIFIEDPUSH_H
#define UNIFIEDPUSH_H

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QVariantMap>

class QNetworkAccessManager;

// Push through UnifiedPush (a distributor such as Foghorn on Sailfish OS):
// Rustrx registers as a connector with its own bus name, the distributor
// hands it an endpoint (a push address), the homeserver is told to push
// there through a Matrix push gateway, and a push wakes Rustrx through
// D-Bus activation of that name even when it is closed. The state lives in
// push.json (mode 0600; the endpoint is a secret, never logged or shown).
// docs/PUSH.md has the design.
class UnifiedPush : public QObject
{
    Q_OBJECT
    // a distributor is installed
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // the user's switch (on unless switched off)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // the homeserver pushes to us: push replaces the stand-in
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString distributor READ distributor NOTIFY changed)
    // where the gateway is (its host only, for Settings)
    Q_PROPERTY(QString gatewayHost READ gatewayHost NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    static const char *const kConnectorName;
    // one per account, each in its account's folder with its own token;
    // the one connector hands each call to the instance it is for
    explicit UnifiedPush(const QString &dataDir, QObject *parent = nullptr);
    ~UnifiedPush();

    // every account's folder with a login: the data folder, accounts/<n>
    static QStringList accountDirs(const QString &dataDir);
    // push.json says push is on and working, for every account: the
    // stand-in is not needed
    static bool activeOnDisk(const QString &dataDir);
    // push is switched on (the same switch for all accounts)
    static bool enabledOnDisk(const QString &dataDir);

    bool available() const { return !m_distributor.isEmpty(); }
    bool enabled() const { return m_state.value("enabled", true).toBool(); }
    void setEnabled(bool on);
    bool active() const;
    QString distributor() const { return m_distributor; }
    QString gatewayHost() const;
    QString status() const { return m_status; }

    // the connector object on the bus and its name (app, stand-in, woken process)
    bool exportConnector();
    // logged in: register with the distributor, set the pusher where needed
    void loggedIn();
    // logged out: unregister, forget
    void loggedOut();
    // the core answered "pusher_set"
    void pusherSet(const QString &endpoint);
    // at login: whether the server still has our pusher (push_check)
    void checkPusher();
    void pusherChecked(const QString &endpoint, bool present);

    // from the D-Bus connector, to the instance whose token the call carries
    static QVariantMap dispatchMessage(const QVariantMap &args);
    static QVariantMap dispatchNewEndpoint(const QVariantMap &args);
    static QVariantMap dispatchUnregistered(const QVariantMap &args);
    QVariantMap message(const QVariantMap &args);
    QVariantMap newEndpoint(const QVariantMap &args);
    QVariantMap unregistered(const QVariantMap &args);

signals:
    void changed();
    // a push for this room and event (the woken process fetches it)
    void pushed(const QString &roomId, const QString &eventId);
    // a core command to send
    void command(const QVariantMap &cmd);

private:
    void load();
    void save();
    void findDistributor();
    void registerApp();
    void discoverGateway();
    void setPusherIfNeeded();
    void setStatus(const QString &s);
    bool fromDistributor(const QVariantMap &args) const;
    static UnifiedPush *instanceFor(const QVariantMap &args);

    QString m_path;
    QVariantMap m_state;   // enabled, token, endpoint, oldEndpoint, gateway, pusherSet
    QString m_distributor;
    QString m_status;
    bool m_loggedIn = false;
    QNetworkAccessManager *m_net = nullptr;
};

// org.unifiedpush.Connector2 at /org/unifiedpush/Connector
class UnifiedPushConnector : public QDBusAbstractAdaptor
{
    Q_OBJECT
    // the connector interface; Q_CLASSINFO takes a literal only
    Q_CLASSINFO("D-Bus Interface", "org.unifiedpush.Connector2")
public:
    explicit UnifiedPushConnector(QObject *object) : QDBusAbstractAdaptor(object) {}
public slots:
    QVariantMap Message(const QVariantMap &args) { return UnifiedPush::dispatchMessage(args); }
    QVariantMap NewEndpoint(const QVariantMap &args) { return UnifiedPush::dispatchNewEndpoint(args); }
    QVariantMap Unregistered(const QVariantMap &args) { return UnifiedPush::dispatchUnregistered(args); }
};

#endif
