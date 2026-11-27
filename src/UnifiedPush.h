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
    // the distributor was looked up (asked without waiting, at start and
    // at each login): until then `available` says nothing
    Q_PROPERTY(bool lookedUp READ lookedUp NOTIFY changed)
    // the user's switch (on unless switched off)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // the homeserver pushes to us: push replaces the stand-in
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString distributor READ distributor NOTIFY changed)
    // where the gateway is (its host only, for Settings)
    Q_PROPERTY(QString gatewayHost READ gatewayHost NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // another app holds the connector's name and could not prove it is a
    // Rustrx (audit 0.8.4 IPC2): no registration, no pusher, the stand-in
    // meanwhile; asked again at every start. The same for every account
    Q_PROPERTY(bool blocked READ blocked NOTIFY changed)
    // a distributor other than the one push went through (gone now) is
    // there and waits for the user's yes (IPC3): its name, else ""; and the
    // one before it. Asked once per start, by the app only
    Q_PROPERTY(QString offered READ offered NOTIFY changed)
    Q_PROPERTY(QString pinned READ pinned NOTIFY changed)

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
    bool lookedUp() const { return m_lookedUp; }
    bool enabled() const { return m_state.value("enabled", true).toBool(); }
    void setEnabled(bool on);
    bool active() const;
    QString distributor() const { return m_distributor; }
    QString gatewayHost() const;
    QString status() const { return m_status; }
    bool blocked() const;
    QString offered() const;
    QString pinned() const;
    // the user's yes to the offered distributor, or the "not now"
    Q_INVOKABLE void useDistributor(const QString &name);
    Q_INVOKABLE void declineDistributor();

    // Which distributor of `names` push goes through: the one it went
    // through before (`pinned`) while it is there; with none before,
    // Foghorn by its exact name, else the first by name. With the pinned
    // one gone, none: another (in `offer`) only with the user's yes.
    static QString chooseDistributor(const QStringList &names, const QString &pinned, QString *offer = nullptr);
    // the data folder's push.json says the connector's name was held by
    // another app at the last look
    static bool blockedOnDisk(const QString &dataDir);

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
    void distributorFound(QStringList names);
    void registerApp();
    void discoverGateway();
    void setPusherIfNeeded();
    void setStatus(const QString &s);
    bool fromDistributor(const QVariantMap &args) const;
    static UnifiedPush *instanceFor(const QVariantMap &args);
    // the data folder's instance (the first made): the pin and the block live in its push.json
    static UnifiedPush *home();
    // the connector's name: whether its holder is a Rustrx (NameProof)
    void checkConnector(int tries);
    // the connector's name taken, or its holder asked (push on only)
    bool takeConnector();
    static void setConnector(int state);
    // logged in, switched on, a distributor: registered, unless the
    // connector's name is not ours
    void registerIfFree();
    // the homeserver stops pushing to an endpoint another app receives
    void dropPusher();

    QString m_path;
    QVariantMap m_state;   // enabled, token, endpoint, oldEndpoint, gateway, pusherSet
    QString m_distributor;
    QString m_status;
    bool m_loggedIn = false;
    bool m_lookedUp = false;
    // a login waits for the lookup it started, then registers
    bool m_registerWanted = false;
    // only the newest lookup's answer counts
    int m_lookup = 0;
    // a registration waits for the connector's check
    bool m_waitingForConnector = false;
    // the connector object is on the bus (exportConnector)
    bool m_exported = false;
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
