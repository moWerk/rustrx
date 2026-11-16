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
#include "UnifiedPush.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QUuid>

const char *const UnifiedPush::kConnectorName = "org.unifiedpush.Connector.rustrx";

static const char kDistributorPrefix[] = "org.unifiedpush.Distributor.";
// the UnifiedPush project's common gateway, when the push server has none
static const char kCommonGateway[] = "https://matrix.gateway.unifiedpush.org/_matrix/push/v1/notify";

static QVariantMap readState(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QVariantMap();
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

static QString statePath(const QString &dataDir)
{
    return dataDir + QStringLiteral("/push.json");
}

bool UnifiedPush::enabledOnDisk(const QString &dataDir)
{
    return readState(statePath(dataDir)).value("enabled", true).toBool();
}

bool UnifiedPush::activeOnDisk(const QString &dataDir)
{
    const QVariantMap s = readState(statePath(dataDir));
    return s.value("enabled", true).toBool() && s.value("pusherSet").toBool()
            && !s.value("endpoint").toString().isEmpty();
}

UnifiedPush::UnifiedPush(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_path(statePath(dataDir))
{
    load();
    findDistributor();
}

void UnifiedPush::load()
{
    m_state = readState(m_path);
}

void UnifiedPush::save()
{
    QFile f(m_path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_state)).toJson(QJsonDocument::Compact));
    }
    emit changed();
}

void UnifiedPush::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    qInfo().noquote() << "push:" << s;
    emit changed();
}

bool UnifiedPush::active() const
{
    return enabled() && m_state.value("pusherSet").toBool() && !m_state.value("endpoint").toString().isEmpty();
}

QString UnifiedPush::gatewayHost() const
{
    return QUrl(m_state.value("gateway").toString()).host();
}

// a distributor running or startable (Foghorn's when there are several)
void UnifiedPush::findDistributor()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QStringList names = bus.interface()->registeredServiceNames().value();
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("ListActivatableNames"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        names += reply.arguments().first().toStringList();
    // the sandbox may hide activatable names: the service files tell too
    const QStringList files = QDir(QStringLiteral("/usr/share/dbus-1/services"))
            .entryList({ QString::fromLatin1(kDistributorPrefix) + QStringLiteral("*.service") }, QDir::Files);
    for (const QString &f : files)
        names << f.left(f.size() - 8);
    QString found;
    for (const QString &n : names) {
        if (!n.startsWith(QLatin1String(kDistributorPrefix)))
            continue;
        if (found.isEmpty() || n.endsWith(QLatin1String("Foghorn")))
            found = n;
    }
    if (found != m_distributor) {
        m_distributor = found;
        qInfo().noquote() << "push: distributor" << (found.isEmpty() ? QStringLiteral("none") : found);
        emit changed();
    }
}

bool UnifiedPush::exportConnector()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *object = new QObject(this);
    new UnifiedPushConnector(object, this);
    if (!bus.registerObject(QStringLiteral("/org/unifiedpush/Connector"), object)) {
        qWarning() << "push: connector object not registered";
        return false;
    }
    if (!bus.registerService(QString::fromLatin1(kConnectorName))) {
        qInfo() << "push: connector name not taken" << bus.lastError().message();
        return false;
    }
    return true;
}

void UnifiedPush::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_state.insert("enabled", on);
    save();
    if (on) {
        loggedIn();
    } else {
        // off: the homeserver stops pushing, the distributor forgets us
        const QString endpoint = m_state.value("endpoint").toString();
        if (!endpoint.isEmpty() && m_loggedIn)
            emit command({ { "cmd", "push_remove" }, { "endpoint", endpoint } });
        if (!m_distributor.isEmpty() && !m_state.value("token").toString().isEmpty()) {
            QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QStringLiteral("/org/unifiedpush/Distributor"),
                    QStringLiteral("org.unifiedpush.Distributor2"), QStringLiteral("Unregister"));
            call << QVariantMap{ { "token", m_state.value("token") } };
            QDBusConnection::sessionBus().asyncCall(call);
        }
        m_state.remove("endpoint");
        m_state.remove("pusherSet");
        save();
        setStatus(QStringLiteral("off"));
    }
}

void UnifiedPush::loggedIn()
{
    m_loggedIn = true;
    findDistributor();
    if (!enabled() || m_distributor.isEmpty())
        return;
    registerApp();
    setPusherIfNeeded();
}

void UnifiedPush::loggedOut()
{
    m_loggedIn = false;
    // the homeserver drops the pusher with the session; the distributor too
    if (!m_distributor.isEmpty() && !m_state.value("token").toString().isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QStringLiteral("/org/unifiedpush/Distributor"),
                QStringLiteral("org.unifiedpush.Distributor2"), QStringLiteral("Unregister"));
        call << QVariantMap{ { "token", m_state.value("token") } };
        QDBusConnection::sessionBus().asyncCall(call);
    }
    const bool on = enabled();
    m_state.clear();
    m_state.insert("enabled", on);
    save();
}

// at every start (as the spec has it); the endpoint comes by NewEndpoint
void UnifiedPush::registerApp()
{
    if (m_state.value("token").toString().isEmpty()) {
        m_state.insert("token", QUuid::createUuid().toString().mid(1, 36));
        save();
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QStringLiteral("/org/unifiedpush/Distributor"),
            QStringLiteral("org.unifiedpush.Distributor2"), QStringLiteral("Register"));
    call << QVariantMap{ { "service", QString::fromLatin1(kConnectorName) },
                         { "token", m_state.value("token") },
                         { "description", QStringLiteral("Rustrx") } };
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            setStatus(QStringLiteral("register failed: ") + reply.error().message());
            return;
        }
        const QVariantMap r = reply.value();
        if (r.value("success").toString() == QLatin1String("REGISTRATION_SUCCEEDED"))
            setStatus(m_state.value("endpoint").toString().isEmpty() ? QStringLiteral("registered, waiting for an endpoint")
                                                                     : QStringLiteral("registered"));
        else
            setStatus(QStringLiteral("register failed: ") + r.value("reason").toString());
    });
}

void UnifiedPush::setPusherIfNeeded()
{
    const QString endpoint = m_state.value("endpoint").toString();
    const QString gateway = m_state.value("gateway").toString();
    if (!m_loggedIn || !enabled() || endpoint.isEmpty() || m_state.value("pusherSet").toBool())
        return;
    if (gateway.isEmpty()) {
        discoverGateway();
        return;
    }
    emit command({ { "cmd", "push_set" }, { "endpoint", endpoint }, { "gateway", gateway },
                   { "old", m_state.value("oldEndpoint") }, { "lang", QLocale().name().left(2) } });
}

void UnifiedPush::pusherSet(const QString &endpoint)
{
    if (endpoint != m_state.value("endpoint").toString())
        return;
    m_state.insert("pusherSet", true);
    m_state.remove("oldEndpoint");
    save();
    setStatus(QStringLiteral("active"));
}

// the push server's own Matrix gateway where it has one (ntfy does), else
// the UnifiedPush project's common gateway
void UnifiedPush::discoverGateway()
{
    const QUrl endpoint(m_state.value("endpoint").toString());
    QUrl probe;
    probe.setScheme(endpoint.scheme());
    probe.setHost(endpoint.host());
    probe.setPort(endpoint.port());
    probe.setPath(QStringLiteral("/_matrix/push/v1/notify"));
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_net->get(QNetworkRequest(probe));
    connect(reply, &QNetworkReply::finished, this, [this, reply, probe]() {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const bool own = o.value("unifiedpush").toObject().value("gateway").toString() == QLatin1String("matrix");
        const QString gateway = own ? probe.toString() : QString::fromLatin1(kCommonGateway);
        qInfo().noquote() << "push: gateway" << QUrl(gateway).host() << (own ? "(the push server's)" : "(common)");
        m_state.insert("gateway", gateway);
        save();
        setPusherIfNeeded();
    });
}

QVariantMap UnifiedPush::newEndpoint(const QVariantMap &args)
{
    if (args.value("token").toString() != m_state.value("token").toString())
        return QVariantMap();
    const QString endpoint = args.value("endpoint").toString();
    if (!endpoint.startsWith(QLatin1String("https://"))) {
        setStatus(QStringLiteral("endpoint not https, ignored"));
        return QVariantMap();
    }
    if (endpoint != m_state.value("endpoint").toString()) {
        const QString old = m_state.value("endpoint").toString();
        if (!old.isEmpty() && m_state.value("pusherSet").toBool())
            m_state.insert("oldEndpoint", old);
        m_state.insert("endpoint", endpoint);
        m_state.remove("pusherSet");
        // a new push server may have its own gateway
        if (QUrl(old).host() != QUrl(endpoint).host())
            m_state.remove("gateway");
        save();
        qInfo() << "push: new endpoint," << endpoint.size() << "bytes";
    }
    setPusherIfNeeded();
    return QVariantMap();
}

QVariantMap UnifiedPush::unregistered(const QVariantMap &args)
{
    if (args.value("token").toString() != m_state.value("token").toString())
        return QVariantMap();
    m_state.remove("endpoint");
    m_state.remove("pusherSet");
    m_state.remove("token");
    save();
    setStatus(QStringLiteral("unregistered by the distributor"));
    return QVariantMap();
}

// a push: the gateway's JSON, {"notification": {"room_id", "event_id", ...}}
QVariantMap UnifiedPush::message(const QVariantMap &args)
{
    QVariantMap answer;
    if (args.contains("id"))
        answer.insert("id", args.value("id"));
    if (args.value("token").toString() != m_state.value("token").toString())
        return answer;
    const QJsonObject n = QJsonDocument::fromJson(args.value("message").toByteArray()).object().value("notification").toObject();
    const QString roomId = n.value("room_id").toString();
    const QString eventId = n.value("event_id").toString();
    qInfo() << "push: message" << (eventId.isEmpty() ? "without an event (counts only)" : "for an event");
    if (!roomId.isEmpty() && !eventId.isEmpty())
        emit pushed(roomId, eventId);
    return answer;
}
