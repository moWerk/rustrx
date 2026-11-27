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
#include "Keys.h"
#include "NameProof.h"
#include "Perf.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSharedPointer>
#include <QUrl>
#include <QUuid>

const char *const UnifiedPush::kConnectorName = "org.unifiedpush.Connector.rustrx";


static QVariantMap readState(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QVariantMap();
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

static QString statePath(const QString &dataDir)
{
    return dataDir + QLatin1Char('/') + QLatin1String(Keys::kPushStateFile);
}

// every instance, for the connector to find the one a call is for
static QList<UnifiedPush *> s_instances;

// the connector's name in this process (IPC2): not looked at yet, ours,
// its holder being asked, held by another Rustrx (one on its way out), or
// by another app that could not prove it is a Rustrx
enum { ConnectorUnknown, ConnectorOurs, ConnectorChecking, ConnectorRustrx, ConnectorBlocked };
static int s_connector = ConnectorUnknown;
// the distributor waiting for the user's yes (IPC3), once per process
static QString s_offered;
static bool s_offerMade = false;

QStringList UnifiedPush::accountDirs(const QString &dataDir)
{
    QStringList dirs{ dataDir };
    QDir more(dataDir + QLatin1String("/accounts"));
    for (const QString &name : more.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (name.toInt() > 0 && QFile::exists(more.filePath(name + QLatin1String("/session.json"))))
            dirs << more.filePath(name);
    return dirs;
}

bool UnifiedPush::enabledOnDisk(const QString &dataDir)
{
    return readState(statePath(dataDir)).value("enabled", true).toBool();
}

bool UnifiedPush::blockedOnDisk(const QString &dataDir)
{
    return readState(statePath(dataDir)).value("blocked").toBool();
}

bool UnifiedPush::activeOnDisk(const QString &dataDir)
{
    // another app holds the connector's name: no push reaches Rustrx
    if (blockedOnDisk(dataDir))
        return false;
    for (const QString &dir : accountDirs(dataDir)) {
        const QVariantMap s = readState(statePath(dir));
        if (!s.value("enabled", true).toBool() || !s.value("pusherSet").toBool()
                || s.value("endpoint").toString().isEmpty())
            return false;
    }
    return true;
}

UnifiedPush::UnifiedPush(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_path(statePath(dataDir))
{
    s_instances << this;
    load();
    findDistributor();
}

UnifiedPush::~UnifiedPush()
{
    s_instances.removeAll(this);
}

UnifiedPush *UnifiedPush::home()
{
    return s_instances.isEmpty() ? nullptr : s_instances.first();
}

bool UnifiedPush::blocked() const
{
    return s_connector == ConnectorBlocked;
}

QString UnifiedPush::offered() const
{
    return s_offered;
}

QString UnifiedPush::pinned() const
{
    const UnifiedPush *h = home();
    return h ? h->m_state.value("distributor").toString() : QString();
}

UnifiedPush *UnifiedPush::instanceFor(const QVariantMap &args)
{
    for (UnifiedPush *p : s_instances)
        if (p->fromDistributor(args))
            return p;
    return nullptr;
}

QVariantMap UnifiedPush::dispatchMessage(const QVariantMap &args)
{
    if (UnifiedPush *p = instanceFor(args))
        return p->message(args);
    // the distributor wants its id back even when the push is not ours
    QVariantMap answer;
    if (args.contains("id"))
        answer.insert("id", args.value("id"));
    return answer;
}

QVariantMap UnifiedPush::dispatchNewEndpoint(const QVariantMap &args)
{
    UnifiedPush *p = instanceFor(args);
    return p ? p->newEndpoint(args) : QVariantMap();
}

QVariantMap UnifiedPush::dispatchUnregistered(const QVariantMap &args)
{
    UnifiedPush *p = instanceFor(args);
    return p ? p->unregistered(args) : QVariantMap();
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
    return enabled() && !blocked() && m_state.value("pusherSet").toBool() && !m_state.value("endpoint").toString().isEmpty();
}

QString UnifiedPush::gatewayHost() const
{
    return QUrl(m_state.value("gateway").toString()).host();
}

// a distributor running or startable (chooseDistributor), asked without waiting: two blocking bus calls held up the window at
// every start, once per account (0.8.2 audit, ST14). A login waits for
// the answer before it registers.
void UnifiedPush::findDistributor()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const int lookup = ++m_lookup;
    struct Answers {
        int waiting = 2;
        QStringList names;
    };
    QSharedPointer<Answers> answers(new Answers);
    auto ask = [this, &bus, lookup, answers](const QString &method, int timeoutMs) {
        const QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(Keys::kDbusService),
                QString::fromLatin1(Keys::kDbusPath), QString::fromLatin1(Keys::kDbusService), method);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, timeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, lookup, answers](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QStringList> reply = *w;
            w->deleteLater();
            if (!reply.isError())
                answers->names += reply.value();
            if (--answers->waiting == 0 && lookup == m_lookup)
                distributorFound(answers->names);
        });
    };
    ask(QStringLiteral("ListNames"), -1);
    ask(QStringLiteral("ListActivatableNames"), Keys::kListNamesTimeoutMs);
}

void UnifiedPush::distributorFound(QStringList names)
{
    // the sandbox may hide activatable names: the service files tell too
    const QStringList files = QDir(QString::fromLatin1(Keys::kDbusServicesDir))
            .entryList({ QString::fromLatin1(Keys::kDistributorPrefix) + QStringLiteral("*.service") }, QDir::Files);
    for (const QString &f : files)
        names << f.left(f.size() - 8);
    QString offer;
    const QString was = pinned();
    const QString found = chooseDistributor(names, was, &offer);
    // the first distributor ever is taken without asking, and kept
    UnifiedPush *h = home();
    if (was.isEmpty() && !found.isEmpty() && h) {
        h->m_state.insert("distributor", found);
        h->save();
    }
    if (!offer.isEmpty() && !s_offerMade) {
        s_offerMade = true;
        s_offered = offer;
        qInfo().noquote() << "push: distributor" << was << "is gone and" << offer << "is there: asking first";
        for (UnifiedPush *p : s_instances)
            if (p != this)
                emit p->changed();
    }
    const bool first = !m_lookedUp;
    m_lookedUp = true;
    if (first)
        Perf::mark("distributor");
    if (found != m_distributor) {
        m_distributor = found;
        qInfo().noquote() << "push: distributor" << (found.isEmpty() ? QStringLiteral("none") : found);
        emit changed();
    } else if (first || !offer.isEmpty()) {
        emit changed();
    }
    if (m_registerWanted) {
        m_registerWanted = false;
        registerIfFree();
    }
}

QString UnifiedPush::chooseDistributor(const QStringList &names, const QString &pinned, QString *offer)
{
    const QString prefix = QString::fromLatin1(Keys::kDistributorPrefix);
    QStringList there;
    for (const QString &n : names)
        if (n.startsWith(prefix) && n.size() > prefix.size() && !there.contains(n))
            there << n;
    there.sort();
    if (offer)
        offer->clear();
    if (!pinned.isEmpty() && there.contains(pinned))
        return pinned;
    const QString foghorn = QString::fromLatin1(Keys::kDistributorFoghorn);
    const QString found = there.contains(foghorn) ? foghorn : there.value(0);
    if (pinned.isEmpty())
        return found;
    if (offer)
        *offer = found;
    return QString();
}

void UnifiedPush::useDistributor(const QString &name)
{
    UnifiedPush *h = home();
    if (name.isEmpty() || name != s_offered || !h)
        return;
    s_offered.clear();
    h->m_state.insert("distributor", name);
    h->save();
    qInfo().noquote() << "push: distributor" << name << "taken with the user's yes";
    for (UnifiedPush *p : s_instances) {
        p->m_distributor = name;
        emit p->changed();
        p->registerIfFree();
    }
}

void UnifiedPush::declineDistributor()
{
    if (s_offered.isEmpty())
        return;
    qInfo() << "push: the new distributor not taken, asked again at the next start";
    s_offered.clear();
    for (UnifiedPush *p : s_instances)
        emit p->changed();
}

void UnifiedPush::registerIfFree()
{
    if (!m_loggedIn || !enabled() || m_distributor.isEmpty())
        return;
    if (s_connector == ConnectorChecking) {
        m_waitingForConnector = true;
        return;
    }
    if (s_connector == ConnectorBlocked) {
        dropPusher();
        return;
    }
    registerApp();
    setPusherIfNeeded();
}

void UnifiedPush::dropPusher()
{
    const QString endpoint = m_state.value("endpoint").toString();
    if (!m_loggedIn || !m_state.value("pusherSet").toBool() || endpoint.isEmpty())
        return;
    emit command({ { "cmd", "push_remove" }, { "endpoint", endpoint } });
    m_state.remove("pusherSet");
    save();
}

void UnifiedPush::setConnector(int state)
{
    s_connector = state;
    if (UnifiedPush *h = home()) {
        const bool on = state == ConnectorBlocked;
        if (h->m_state.value("blocked").toBool() != on) {
            if (on)
                h->m_state.insert("blocked", true);
            else
                h->m_state.remove("blocked");
            h->save();
        }
    }
    for (UnifiedPush *p : s_instances) {
        if (state == ConnectorBlocked) {
            p->m_waitingForConnector = false;
            p->dropPusher();
            p->setStatus(QStringLiteral("connector name held by another app"));
        } else if (state != ConnectorChecking && p->m_waitingForConnector) {
            p->m_waitingForConnector = false;
            p->registerIfFree();
        }
        emit p->changed();
    }
}

// The connector's name is held by someone else: a Rustrx on its way out
// (waited for, then the name is taken), or an app that would get the
// pushes, the endpoint and the token (IPC2): then push pauses and the
// stand-in stands in, until a start finds the name free again.
void UnifiedPush::checkConnector(int tries)
{
    s_connector = ConnectorChecking;
    const QString secret = NotifyGate::secret(QFileInfo(m_path).absolutePath() + QLatin1Char('/')
                                              + QLatin1String(NotifyGate::kSecretFile));
    auto *check = new NameProof::Check(QDBusConnection::sessionBus(), QString::fromLatin1(kConnectorName), secret,
                                       Keys::kProofWaitMs, true, this);
    connect(check, &NameProof::Check::done, this, [this, check, tries](NameProof::Holder holder) {
        check->deleteLater();
        if (holder == NameProof::Holder::Free) {
            if (QDBusConnection::sessionBus().registerService(QString::fromLatin1(kConnectorName))) {
                qInfo() << "push: connector name taken once the other Rustrx let go";
                setConnector(ConnectorOurs);
            } else if (tries > 0) {
                // someone may have taken it in between: looked at again
                checkConnector(tries - 1);
            } else {
                // nobody holds it and still it cannot be had: the sandbox
                // does not let Rustrx own it (no UnifiedPush permission
                // without a distributor installed). No squat, nothing blocked;
                // only a name with an owner that cannot prove itself is one
                qInfo() << "push: the connector name is free but cannot be owned here (no distributor's permission?)";
                setConnector(ConnectorUnknown);
            }
            return;
        }
        if (holder == NameProof::Holder::Rustrx) {
            qInfo() << "push: connector name still held by another Rustrx";
            setConnector(ConnectorRustrx);
            return;
        }
        qWarning() << "push: connector name held by another app; push paused, the stand-in stands in";
        setConnector(ConnectorBlocked);
    });
    check->start();
}

bool UnifiedPush::exportConnector()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *object = new QObject(this);
    new UnifiedPushConnector(object);
    if (!bus.registerObject(QString::fromLatin1(Keys::kConnectorPath), object)) {
        qWarning() << "push: connector object not registered";
        return false;
    }
    m_exported = true;
    return takeConnector();
}

bool UnifiedPush::takeConnector()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.registerService(QString::fromLatin1(kConnectorName))) {
        setConnector(ConnectorOurs);
        return true;
    }
    // push off: the name is not looked into (a block left from an earlier
    // look goes); switching push on looks again
    if (!enabled()) {
        qInfo() << "push: connector name not taken, push is off";
        setConnector(ConnectorUnknown);
        return false;
    }
    qInfo() << "push: connector name not taken" << bus.lastError().name() << "- asking its holder";
    checkConnector(1);
    return false;
}

void UnifiedPush::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_state.insert("enabled", on);
    save();
    // the connector's name, not looked into while push was off
    if (on && this == home() && s_connector != ConnectorOurs && s_connector != ConnectorChecking && m_exported)
        takeConnector();
    if (on) {
        // an account not logged in registers at its login
        if (m_loggedIn)
            loggedIn();
    } else {
        // off: the homeserver stops pushing, the distributor forgets us
        const QString endpoint = m_state.value("endpoint").toString();
        if (!endpoint.isEmpty() && m_loggedIn)
            emit command({ { "cmd", "push_remove" }, { "endpoint", endpoint } });
        if (!m_distributor.isEmpty() && !m_state.value("token").toString().isEmpty()) {
            QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QString::fromLatin1(Keys::kDistributorPath),
                    QString::fromLatin1(Keys::kDistributorInterface), QStringLiteral("Unregister"));
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
    // looked up again (one installed since the start counts); registered
    // with the answer
    m_registerWanted = true;
    findDistributor();
}

void UnifiedPush::loggedOut()
{
    m_loggedIn = false;
    // the homeserver drops the pusher with the session; the distributor too
    if (!m_distributor.isEmpty() && !m_state.value("token").toString().isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QString::fromLatin1(Keys::kDistributorPath),
                QString::fromLatin1(Keys::kDistributorInterface), QStringLiteral("Unregister"));
        call << QVariantMap{ { "token", m_state.value("token") } };
        QDBusConnection::sessionBus().asyncCall(call);
    }
    // the distributor chosen (IPC3) and the connector's state outlive the login
    const bool on = enabled();
    const QVariant pin = m_state.value("distributor");
    const bool wasBlocked = m_state.value("blocked").toBool();
    m_state.clear();
    m_state.insert("enabled", on);
    if (!pin.toString().isEmpty())
        m_state.insert("distributor", pin);
    if (wasBlocked)
        m_state.insert("blocked", true);
    save();
}

// at every start (as the spec has it); the endpoint comes by NewEndpoint
void UnifiedPush::registerApp()
{
    if (m_state.value("token").toString().isEmpty()) {
        m_state.insert("token", QUuid::createUuid().toString().mid(1, 36));
        save();
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_distributor, QString::fromLatin1(Keys::kDistributorPath),
            QString::fromLatin1(Keys::kDistributorInterface), QStringLiteral("Register"));
    call << QVariantMap{ { "service", QString::fromLatin1(kConnectorName) },
                         { "token", m_state.value("token") },
                         { "description", QStringLiteral("Rustrx") } };
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, Keys::kRegisterTimeoutMs), this);
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
    if (!m_loggedIn || !enabled() || blocked() || endpoint.isEmpty() || m_state.value("pusherSet").toBool())
        return;
    if (gateway.isEmpty()) {
        discoverGateway();
        return;
    }
    emit command({ { "cmd", "push_set" }, { "endpoint", endpoint }, { "gateway", gateway },
                   { "old", m_state.value("oldEndpoint") }, { "lang", QLocale().name().left(2) } });
}

// at login: a pusher we set may be gone from the server (a password
// change, an admin, an expiry); "active" would claim pushes that never come
void UnifiedPush::checkPusher()
{
    if (m_loggedIn && enabled() && m_state.value("pusherSet").toBool())
        emit command({ { "cmd", "push_check" }, { "endpoint", m_state.value("endpoint") } });
}

void UnifiedPush::pusherChecked(const QString &endpoint, bool present)
{
    if (present || endpoint != m_state.value("endpoint").toString())
        return;
    qWarning() << "push: the homeserver had dropped the pusher, setting it again";
    m_state.remove("pusherSet");
    save();
    setPusherIfNeeded();
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
    probe.setPath(QString::fromLatin1(Keys::kPushNotifyPath));
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_net->get(QNetworkRequest(probe));
    connect(reply, &QNetworkReply::finished, this, [this, reply, probe]() {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const bool own = o.value("unifiedpush").toObject().value("gateway").toString() == QLatin1String("matrix");
        const QString gateway = own ? probe.toString() : QString::fromLatin1(Keys::kCommonGateway);
        qInfo().noquote() << "push: gateway" << QUrl(gateway).host() << (own ? "(the push server's)" : "(common)");
        m_state.insert("gateway", gateway);
        save();
        setPusherIfNeeded();
    });
}

// A call on the connector is the distributor's only when it carries the
// token we registered with. With none stored (logged out, or before the
// first registration) no call is: "" == "" would let any bus client set
// the endpoint.
bool UnifiedPush::fromDistributor(const QVariantMap &args) const
{
    const QString token = m_state.value("token").toString();
    return !token.isEmpty() && args.value("token").toString() == token;
}

QVariantMap UnifiedPush::newEndpoint(const QVariantMap &args)
{
    if (!fromDistributor(args))
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
    if (!fromDistributor(args))
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
    if (!fromDistributor(args))
        return answer;
    const QJsonObject n = QJsonDocument::fromJson(args.value("message").toByteArray()).object().value("notification").toObject();
    const QString roomId = n.value("room_id").toString();
    const QString eventId = n.value("event_id").toString();
    qInfo() << "push: message" << (eventId.isEmpty() ? "without an event (counts only)" : "for an event");
    if (!roomId.isEmpty() && !eventId.isEmpty())
        emit pushed(roomId, eventId);
    return answer;
}
