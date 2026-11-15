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
#include "Twemoji.h"
#include "Rustrx.h"
#include "DebugLog.h"
#include <QLibrary>
#include <sailfishapp.h>
#include "RoomModel.h"
#include "TimelineModel.h"
#include "rustrx_core.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QDBusError>
#include <QMutex>
#include <QElapsedTimer>
#include <algorithm>
#include <QMutexLocker>

// The core's tokio threads call eventCallback until the process ends; the
// lock and the pointer keep them away from a Rustrx that is being destroyed.
// the JSON protocol this UI speaks, see docs/PROTOCOL.md (core: PROTO in core.rs)
static const int kProto = 1;

static QMutex s_lock;

// org.sailfishos.share on /share/room (see the constructor): the system share sheet calls share(a{sv})
// with "resources", a list of file paths or URLs, or of maps that carry the
// data itself ("data", "name", "type"), e.g. a link or a piece of text.
class ShareAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.share")
public:
    explicit ShareAdaptor(Rustrx *owner) : QObject(owner), m_owner(owner) {}
public slots:
    Q_SCRIPTABLE void share(const QVariantMap &config)
    {
        QStringList files;
        QStringList texts;
        const QVariantList resources = unwrap(config.value(QStringLiteral("resources"))).toList();
        qInfo() << "share:" << resources.size() << "resources";
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/shared";
        int n = 0;
        for (const QVariant &raw : resources) {
            const QVariant r = unwrap(raw);
            qInfo() << "share: resource of type" << r.typeName();
            if (r.type() == QVariant::String || r.type() == QVariant::Url) {
                QString path = r.toString();
                if (path.startsWith(QLatin1String("file://")))
                    path = QUrl(path).toLocalFile();
                if (QFileInfo(path).isFile()) {
                    files << path;
                } else {
                    qInfo() << "share: not a readable file, sent as text";
                    texts << path;   // a URL that is not a local file: send it as text
                }
                continue;
            }
            const QVariantMap m = r.toMap();
            const QString type = unwrap(m.value(QStringLiteral("type"))).toString();
            const QVariant rawData = unwrap(m.value(QStringLiteral("data")));
            // a file from a sandboxed app (the gallery, say) comes as an open
            // file descriptor, since this app may not read the other's folders
            if (rawData.userType() == qMetaTypeId<QDBusUnixFileDescriptor>()) {
                const QString path = copyFromDescriptor(rawData.value<QDBusUnixFileDescriptor>(), cache,
                        unwrap(m.value(QStringLiteral("name"))).toString(), ++n);
                if (!path.isEmpty())
                    files << path;
                continue;
            }
            QByteArray data = rawData.type() == QVariant::String ? rawData.toString().toUtf8() : rawData.toByteArray();
            if (type.startsWith(QLatin1String("text/")))
                while (data.endsWith('\0'))   // C strings arrive with their terminator
                    data.chop(1);
            if (type.startsWith(QLatin1String("text/")) || m.contains(QStringLiteral("linkTitle"))) {
                texts << QString::fromUtf8(data);
            } else if (!data.isEmpty()) {
                QDir().mkpath(cache);
                QString name = m.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    name = QStringLiteral("shared-%1").arg(++n);
                const QString path = cache + "/" + QFileInfo(name).fileName();
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    files << path;
                }
            }
        }
        m_owner->receiveShare(files, texts.join(QStringLiteral("\n")));
    }
private:
    // the shared file, read from its descriptor into this app's cache
    static QString copyFromDescriptor(const QDBusUnixFileDescriptor &fd, const QString &cache,
                                      const QString &name, int n)
    {
        if (!fd.isValid())
            return QString();
        QDir().mkpath(cache);
        const QString file = name.isEmpty() ? QStringLiteral("shared-%1").arg(n) : QFileInfo(name).fileName();
        const QString path = cache + "/" + file;
        QFile in;
        QFile out(path);
        // the descriptor stays owned by fd; QFile reads it without closing it
        if (!in.open(fd.fileDescriptor(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
                || !out.open(QIODevice::WriteOnly)) {
            qWarning() << "share: cannot copy" << file;
            return QString();
        }
        in.seek(0);
        while (!in.atEnd()) {
            const QByteArray chunk = in.read(1 << 20);
            if (chunk.isEmpty() || out.write(chunk) != chunk.size())
                break;
        }
        qInfo() << "share: received" << file << out.size() << "bytes";
        return path;
    }
    // D-Bus hands nested values over as QDBusVariant / QDBusArgument
    static QVariant unwrap(const QVariant &v)
    {
        if (v.userType() == qMetaTypeId<QDBusVariant>())
            return unwrap(v.value<QDBusVariant>().variant());
        if (v.userType() == qMetaTypeId<QDBusArgument>()) {
            const QDBusArgument arg = v.value<QDBusArgument>();
            if (arg.currentType() == QDBusArgument::ArrayType) {
                if (arg.currentSignature() == QLatin1String("ay")) {
                    QByteArray bytes;
                    arg.beginArray();
                    while (!arg.atEnd()) {
                        uchar b = 0;
                        arg >> b;
                        bytes.append(char(b));
                    }
                    arg.endArray();
                    return bytes;
                }
                if (arg.currentSignature().startsWith(QLatin1String("a{"))) {
                    QVariantMap map;
                    arg >> map;
                    return map;
                }
                QVariantList list;
                arg.beginArray();
                while (!arg.atEnd()) {
                    QVariant item;
                    arg >> item;
                    list << unwrap(item);
                }
                arg.endArray();
                return list;
            }
            if (arg.currentType() == QDBusArgument::MapType) {
                QVariantMap map;
                arg >> map;
                return map;
            }
        }
        return v;
    }
    Rustrx *m_owner;
};

#include "Rustrx.moc"
static Rustrx *s_instance = nullptr;

Rustrx::Rustrx(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_rooms(new RoomModel(this))
    , m_people(new RoomModel(this))
    , m_favourites(new RoomModel(this))
    , m_all(new RoomModel(this))
    , m_spaces(new RoomModel(this))
    , m_targets(new RoomModel(this))
    , m_timeline(new TimelineModel(this))
    , m_thread(new TimelineModel(this))
{
    s_instance = this;
    connect(m_thread, &TimelineModel::emoteWanted, this, [this](const QString &mxc) {
        command({ { "cmd", "fetch_emote" }, { "mxc", mxc } });
    });
    connect(m_timeline, &TimelineModel::emoteWanted, this, [this](const QString &mxc) {
        command({ { "cmd", "fetch_emote" }, { "mxc", mxc } });
    });
    m_since.start();
    QDir().mkpath(dataDir);
    // the store's key from sailfish-secrets, if it works here; without it the
    // store stays unencrypted (community ports without sailfish-secrets)
    QElapsedTimer secretsTime;
    secretsTime.start();
    // the sailfish-secrets plugin, loaded only where it can be: its library
    // is not a dependency of the package
    QByteArray storeKey;
    QLibrary secrets(SailfishApp::pathTo(QStringLiteral("lib/librustrx-secrets.so")).toLocalFile());
    typedef int (*StoreKeyFn)(char *, int);
    if (StoreKeyFn fn = reinterpret_cast<StoreKeyFn>(secrets.resolve("rustrx_secrets_store_key"))) {
        char buf[128];
        const int n = fn(buf, sizeof buf);
        if (n > 0)
            storeKey = QByteArray(buf, n);
    } else {
        qInfo().noquote() << "startup: no sailfish-secrets plugin:" << secrets.errorString();
    }
    qInfo().noquote() << "startup: store key" << (storeKey.isEmpty() ? "none" : "from sailfish-secrets")
                      << "after" << secretsTime.elapsed() << "ms";
    if (!storeKey.isEmpty())
        rustrx_set_store_key(storeKey.constData());
    const int rc = rustrx_start(dataDir.toUtf8().constData(), &Rustrx::eventCallback, this);
    if (rc != 0)
        qWarning() << "rustrx_start failed:" << rc;
    qInfo().noquote() << "startup: core started after" << m_since.elapsed() << "ms";

    // the sandbox lets the app own its organisation's name on the session bus
    QDBusConnection bus = QDBusConnection::sessionBus();
    // the share sheet calls /share/<method>, the method named in the desktop
    // file's X-Share-Methods ("room"); /share stays for direct callers
    ShareAdaptor *share = new ShareAdaptor(this);
    bus.registerObject(QStringLiteral("/share"), share, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(QStringLiteral("/share/room"), share, QDBusConnection::ExportScriptableSlots);
    if (!bus.registerObject(QStringLiteral("/net/mowerk/harbour_rustrx"), this, QDBusConnection::ExportScriptableSlots)
            || !bus.registerService(QStringLiteral("net.mowerk.harbour-rustrx")))
        qWarning() << "D-Bus service not registered:" << bus.lastError().message();
}

Rustrx::~Rustrx()
{
    QMutexLocker lock(&s_lock);
    rustrx_stop();
    s_instance = nullptr;
}

void Rustrx::receiveShare(const QStringList &files, const QString &text)
{
    m_pendingShare = QVariantMap{{"files", files}, {"text", text}};
    if (handOver())
        return;
    emit activateRequested();
    emit shareRequested(files, text);
}

static QString handoverPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/handover.json");
}

bool Rustrx::startUnit(const QString &unit)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
            QStringLiteral("/org/freedesktop/systemd1"), QStringLiteral("org.freedesktop.systemd1.Manager"),
            QStringLiteral("StartUnit"));
    call << unit << QStringLiteral("replace");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "starting" << unit << "failed:" << reply.errorMessage();
        return false;
    }
    return true;
}

void Rustrx::quit()
{
    emit quitRequested();
    // time for the close to reach the notification server
    QTimer::singleShot(300, qApp, &QCoreApplication::quit);
}

bool Rustrx::activate()
{
    if (!m_standIn) {
        emit activateRequested();
        return false;
    }
    qInfo() << "stand-in: the app is starting, leaving";
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

// The new process opens the store only when this one is gone (it waits for
// the bus name), so what it should show goes through a file.
bool Rustrx::handOver()
{
    if (!m_standIn)
        return false;
    QJsonObject o;
    o.insert("room", m_pendingRoom);
    o.insert("call", m_pendingCall);
    o.insert("knocks", m_pendingKnocks);
    o.insert("uri", m_pendingUri);
    o.insert("share", QJsonObject::fromVariantMap(m_pendingShare));
    QFile f(handoverPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();
    qInfo() << "stand-in: handing over to the app";
    startUnit(QStringLiteral("harbour-rustrx-open.service"));
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

void Rustrx::takeHandover()
{
    QFile f(handoverPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    f.remove();
    m_pendingRoom = o.value("room").toString();
    m_pendingCall = o.value("call").toString();
    m_pendingKnocks = o.value("knocks").toBool();
    m_pendingUri = o.value("uri").toString();
    m_pendingShare = o.value("share").toObject().toVariantMap();
}

QString Rustrx::backgroundFlagPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/background");
}

bool Rustrx::background() const
{
    return QFile::exists(backgroundFlagPath());
}

void Rustrx::setBackground(bool on)
{
    if (on == background())
        return;
    if (on) {
        QDir().mkpath(QFileInfo(backgroundFlagPath()).path());
        QFile f(backgroundFlagPath());
        f.open(QIODevice::WriteOnly);
    } else {
        QFile::remove(backgroundFlagPath());
    }
    emit settingsChanged();
}

bool Rustrx::downscaleImages() const
{
    return QSettings().value("downscaleImages", true).toBool();
}

void Rustrx::setDownscaleImages(bool on)
{
    if (on == downscaleImages())
        return;
    QSettings().setValue("downscaleImages", on);
    emit settingsChanged();
}

QString Rustrx::sortOrder() const
{
    return QSettings().value("sortOrder", QStringLiteral("activity")).toString();
}

void Rustrx::setSortOrder(const QString &order)
{
    if (order == sortOrder())
        return;
    QSettings().setValue("sortOrder", order);
    emit settingsChanged();
    distributeRooms(m_lastRooms);
}

bool Rustrx::favsOnTop() const
{
    return QSettings().value("favsOnTop", false).toBool();
}

void Rustrx::setFavsOnTop(bool on)
{
    if (on == favsOnTop())
        return;
    QSettings().setValue("favsOnTop", on);
    emit settingsChanged();
    distributeRooms(m_lastRooms);
}

void Rustrx::setRoomFilter(const QString &text)
{
    const QString t = text.trimmed();
    if (t == m_roomFilter)
        return;
    m_roomFilter = t;
    emit roomFilterChanged();
    distributeRooms(m_lastRooms);
}

bool Rustrx::debugLog() const
{
    return QSettings().value("debugLog", true).toBool();
}

void Rustrx::setDebugLog(bool on)
{
    if (on == debugLog())
        return;
    QSettings().setValue("debugLog", on);
    DebugLog::setEnabled(on);
    emit settingsChanged();
}

QString Rustrx::shareableLog()
{
    // other apps cannot read the data folder: the copy goes to Downloads
    QString device;
    QFile release(QStringLiteral("/etc/sailfish-release"));
    if (release.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : release.readAll().split('\n'))
            if (line.startsWith("VERSION=")) device = QString::fromUtf8(line.mid(8)).remove('"');
    }
    const QString header = QStringLiteral("Rustrx %1, core %2, Sailfish OS %3\nwritten %4")
            .arg(QStringLiteral(APP_VERSION), coreVersion(), device,
                 QDateTime::currentDateTime().toString(Qt::ISODate));
    return DebugLog::collect(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), header);
}

bool Rustrx::hideMuted() const
{
    return QSettings().value("hideMuted", false).toBool();
}

void Rustrx::setHideMuted(bool on)
{
    if (on == hideMuted())
        return;
    QSettings().setValue("hideMuted", on);
    emit settingsChanged();
    distributeRooms(m_lastRooms);
}

bool Rustrx::lowLast() const
{
    return QSettings().value("lowLast", true).toBool();
}

void Rustrx::setLowLast(bool on)
{
    if (on == lowLast())
        return;
    QSettings().setValue("lowLast", on);
    emit settingsChanged();
    distributeRooms(m_lastRooms);
}

// The core sends the rooms by activity. Invitations stay on top; then, if
// switched on, favourites first and low priority rooms last; within that,
// the most unread first or by name, else the activity order stays (the sort
// is stable).
void Rustrx::sortRooms(QVariantList &rooms) const
{
    const QString order = sortOrder();
    const bool favsFirst = favsOnTop();
    const bool lowAtEnd = lowLast();
    // the number the badge shows: notifications, else unread messages
    const auto unread = [](const QVariantMap &r) {
        const int n = r.value("notifications").toInt();
        return n > 0 ? n : r.value("unread").toInt();
    };
    std::stable_sort(rooms.begin(), rooms.end(), [&](const QVariant &va, const QVariant &vb) {
        const QVariantMap a = va.toMap(), b = vb.toMap();
        const bool ia = a.value("state").toString() == QLatin1String("invited");
        const bool ib = b.value("state").toString() == QLatin1String("invited");
        if (ia != ib)
            return ia;
        if (favsFirst) {
            const bool fa = a.value("is_fav").toBool(), fb = b.value("is_fav").toBool();
            if (fa != fb)
                return fa;
        }
        if (lowAtEnd) {
            const bool la = a.value("is_low").toBool(), lb = b.value("is_low").toBool();
            if (la != lb)
                return lb;
        }
        if (order == QLatin1String("unread")) {
            // the most unread on top; equal counts keep the activity order
            const int ua = unread(a), ub = unread(b);
            if (ua != ub)
                return ua > ub;
        } else if (order == QLatin1String("name")) {
            return QString::localeAwareCompare(a.value("name").toString(), b.value("name").toString()) < 0;
        }
        return false;
    });
}

QString Rustrx::coreVersion() const
{
    return QString::fromUtf8(rustrx_version());
}

// tokio thread: parse here, hand the map to the GUI thread
void Rustrx::eventCallback(void *ctx, const char *json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    if (!doc.isObject())
        return;
    QMutexLocker lock(&s_lock);
    Rustrx *self = static_cast<Rustrx *>(ctx);
    if (!s_instance || self != s_instance)
        return;
    QMetaObject::invokeMethod(self, "handleEvent", Qt::QueuedConnection,
                              Q_ARG(QVariantMap, doc.object().toVariantMap()));
}

QString Rustrx::dev(const QString &json)
{
    const QString flag = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/dev-commands");
    if (!QFile::exists(flag))
        return QStringLiteral("{\"error\":\"dev commands are off\"}");
    const QVariantMap req = QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
    qInfo().noquote() << "dev" << req.value("cmd", req.value("ui", req.keys().value(0))).toString();
    QJsonObject out;
    out["stand_in"] = m_standIn;
    // the stand-in has no window: no pages, no call page
    if (m_standIn && (req.contains("ui") || req.value("cmd").toString().startsWith(QLatin1String("call_")))) {
        out["error"] = QStringLiteral("the stand-in is running, not the app");
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    if (req.contains("cmd")) {
        command(req);
        out["ok"] = true;
    } else if (req.contains("rooms")) {
        QJsonArray rooms;
        for (auto it = m_roomsById.cbegin(); it != m_roomsById.cend(); ++it) {
            QJsonObject r;
            for (const char *key : { "room_id", "name", "is_dm", "encrypted", "unread", "notifications", "state" })
                r[key] = QJsonValue::fromVariant(it.value().value(key));
            rooms.append(r);
        }
        out["rooms"] = rooms;
    } else if (req.contains("state")) {
        out["logged_in"] = m_loggedIn;
        out["sync"] = m_syncState;
        out["open_room"] = m_openRoomId;
        out["active"] = qGuiApp && qGuiApp->applicationState() == Qt::ApplicationActive;
    } else if (req.contains("ui")) {
        emit devUi(req);
        out["ok"] = true;
    } else {
        out["error"] = QStringLiteral("unknown request");
    }
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void Rustrx::command(const QVariantMap &cmd)
{
    // the emote pictures live in the media folder
    if (cmd.value("cmd").toString() == QLatin1String("clear_media")) {
        m_timeline->clearEmotes();
        m_thread->clearEmotes();
        m_emotePaths.clear();
        m_emotesAsked.clear();
    }
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(cmd)).toJson(QJsonDocument::Compact);
    const int rc = rustrx_command(json.constData());
    // one line per command in the journal; the password is not part of the message
    if (rc)
        qWarning().noquote() << "command" << cmd.value("cmd").toString() << "rc" << rc;
    else
        qInfo().noquote() << "command" << cmd.value("cmd").toString();
}

void Rustrx::loginPassword(const QString &user, const QString &password, const QString &homeserver)
{
    m_loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_password"}, {"user", user}, {"password", password}, {"homeserver", homeserver}});
}

void Rustrx::loginSso(const QString &user, const QString &homeserver, const QString &idp)
{
    m_loggingIn = true;
    emit stateChanged();
    QVariantMap cmd{{"cmd", "login_sso"}, {"user", user}, {"homeserver", homeserver}};
    if (!idp.isEmpty())
        cmd.insert("idp", idp);
    command(cmd);
}

void Rustrx::probeServer(const QString &user, const QString &homeserver)
{
    command({{"cmd", "probe_server"}, {"user", user}, {"homeserver", homeserver}});
}

void Rustrx::openRoom(const QString &roomId)
{
    if (m_openRoomId != roomId) {
        m_timeline->clear();
        m_openRoomId = roomId;
        emit openRoomChanged();
    }
    command({{"cmd", "open_room"}, {"room_id", roomId}});
}

void Rustrx::closeRoom()
{
    if (m_openRoomId.isEmpty())
        return;
    command({{"cmd", "close_room"}});
    m_openRoomId.clear();
    m_timeline->clear();
    emit openRoomChanged();
}

void Rustrx::openThread(const QString &roomId, const QString &root)
{
    m_threadRoot = root;
    m_thread->clear();
    command({ { "cmd", "open_thread" }, { "room_id", roomId }, { "root", root } });
}

void Rustrx::closeThread()
{
    m_threadRoot.clear();
    m_thread->clear();
    command({ { "cmd", "close_thread" } });
}

void Rustrx::sendText(const QString &roomId, const QString &body, const QString &replyTo,
                      const QVariantMap &emotes, const QString &threadRoot)
{
    QVariantMap cmd{{"cmd", "send_text"}, {"room_id", roomId}, {"body", body}};
    if (!threadRoot.isEmpty())
        cmd.insert("thread_root", threadRoot);
    if (!replyTo.isEmpty())
        cmd.insert("reply_to", replyTo);
    if (!emotes.isEmpty())
        cmd.insert("emotes", emotes);
    command(cmd);
}

QVariantList Rustrx::knockRequests() const
{
    QVariantList out;
    for (auto it = m_knocks.constBegin(); it != m_knocks.constEnd(); ++it) {
        for (const QVariant &v : it.value().value("requests").toList()) {
            QVariantMap r = v.toMap();
            r.insert("room_id", it.key());
            r.insert("room_name", it.value().value("room_name"));
            r.insert("is_space", it.value().value("is_space"));
            out << r;
        }
    }
    return out;
}

QString Rustrx::emojify(const QString &html, int height) const
{
    return Twemoji::inHtml(html, height);
}

QString Rustrx::emojiUrl(const QString &emoji) const
{
    return Twemoji::url(emoji);
}

QString Rustrx::emotePath(const QString &mxc)
{
    const QString path = m_emotePaths.value(mxc);
    if (!path.isEmpty())
        return QStringLiteral("file://") + path;
    if (mxc.startsWith(QLatin1String("mxc://")) && !m_emotesAsked.contains(mxc)) {
        m_emotesAsked.insert(mxc);
        command({ { "cmd", "fetch_emote" }, { "mxc", mxc } });
    }
    return QString();
}

QString Rustrx::prepareAvatar(const QString &path, const QString &cmd, QSize *size)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    QImageReader reader(p);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        emit errorOccurred(cmd, reader.errorString());
        return QString();
    }
    if (qMax(image.width(), image.height()) > 512)
        image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/outgoing";
    QDir().mkpath(cache);
    // PNG keeps a transparent picture (an icon) as it is; photos go as JPEG
    const bool alpha = image.hasAlphaChannel();
    const QString out = cache + "/avatar-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + (alpha ? ".png" : ".jpg");
    if (!image.save(out, alpha ? "PNG" : "JPG", alpha ? -1 : 90)) {
        emit errorOccurred(cmd, QStringLiteral("cannot write ") + out);
        return QString();
    }
    if (size)
        *size = image.size();
    return out;
}

void Rustrx::setRoomAvatar(const QString &roomId, const QString &path)
{
    QSize size;
    const QString out = prepareAvatar(path, QStringLiteral("set_room_avatar"), &size);
    if (out.isEmpty())
        return;
    command({{"cmd", "set_room_avatar"}, {"room_id", roomId}, {"path", out},
             {"w", size.width()}, {"h", size.height()}});
}

void Rustrx::setOwnAvatar(const QString &path)
{
    const QString out = prepareAvatar(path, QStringLiteral("set_own_avatar"), nullptr);
    if (!out.isEmpty())
        command({{"cmd", "set_own_avatar"}, {"path", out}});
}

void Rustrx::sendFile(const QString &roomId, const QString &path, const QString &caption,
                      const QString &threadRoot)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", p}};
    if (!threadRoot.isEmpty())
        cmd.insert("thread_root", threadRoot);
    if (!caption.trimmed().isEmpty())
        cmd.insert("caption", caption.trimmed());

    // pictures go out the way Element expects them: upright, at most 2048 px
    // on the long side when the setting asks for it, with their size and a
    // thumbnail, so the other side shows a preview instead of a bare file
    QImageReader reader(p);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format();
    if (!format.isEmpty() && format != "gif") {
        QImage image = reader.read();
        if (!image.isNull()) {
            const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/outgoing";
            QDir().mkpath(cache);
            const QString stem = cache + "/" + QString::number(QDateTime::currentMSecsSinceEpoch());
            const bool shrink = QSettings().value("downscaleImages", true).toBool()
                    && qMax(image.width(), image.height()) > 2048;
            if (shrink) {
                image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QString out = stem + ".jpg";
                if (image.save(out, "JPEG", 88))
                    cmd.insert("path", out);
            }
            cmd.insert("w", image.width());
            cmd.insert("h", image.height());
            const QImage thumb = image.scaled(800, 800, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QString thumbPath = stem + "-thumb.jpg";
            if (thumb.save(thumbPath, "JPEG", 80)) {
                cmd.insert("thumb_path", thumbPath);
                cmd.insert("thumb_w", thumb.width());
                cmd.insert("thumb_h", thumb.height());
            }
        }
    }
    command(cmd);
}

void Rustrx::sendVoice(const QString &roomId, const QString &path, qint64 durationMs, const QString &caption)
{
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", path}, {"voice", true}, {"duration_ms", durationMs}};
    if (!caption.isEmpty())
        cmd.insert("caption", caption);
    command(cmd);
}

void Rustrx::getMedia(const QString &roomId, const QString &eventId, bool thumb)
{
    command({{"cmd", "get_media"}, {"room_id", roomId}, {"event_id", eventId}, {"thumb", thumb}});
}

void Rustrx::exportMedia(const QString &roomId, const QString &eventId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Rustrx");
    command({{"cmd", "get_media"}, {"room_id", roomId}, {"event_id", eventId}, {"thumb", false}, {"dest_dir", dir}});
}

QVariantMap Rustrx::roomInfo(const QString &roomId) const
{
    return m_roomsById.value(roomId);
}

QString Rustrx::firstUnreadRoom() const
{
    // the room list is ordered by activity; take the first with something unread
    for (int i = 0; i < m_rooms->rowCount(); ++i) {
        const QVariantMap r = m_rooms->get(i);
        if (r.value("state").toString() != QLatin1String("invited")
                && (r.value("notifications").toInt() > 0 || r.value("unread").toInt() > 0))
            return r.value("room_id").toString();
    }
    return QString();
}

QString Rustrx::formatTime(const QVariant &ms) const
{
    return QDateTime::fromMSecsSinceEpoch(ms.toLongLong()).toString(QStringLiteral("HH:mm"));
}

QString Rustrx::formatDate(const QVariant &ms) const
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms.toLongLong()).date();
    // "Today" and "Yesterday" are QML's (app.dayLabel): qtTrId from here came
    // back as the raw id on the phone while every QML qsTrId resolved.
    return d.toString(Qt::DefaultLocaleLongDate);
}

QString Rustrx::newestPhotoSince(qint64 sinceMs) const
{
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Camera"));
    const QFileInfoList files = dir.entryInfoList({ "*.jpg", "*.jpeg", "*.JPG" }, QDir::Files, QDir::Time);
    if (files.isEmpty() || files.first().lastModified().toMSecsSinceEpoch() < sinceMs)
        return QString();
    return files.first().absoluteFilePath();
}

QString Rustrx::draft(const QString &roomId) const
{
    QSettings settings;
    return settings.value(QStringLiteral("drafts/") + roomId).toString();
}

void Rustrx::setDraft(const QString &roomId, const QString &text)
{
    QSettings settings;
    const QString key = QStringLiteral("drafts/") + roomId;
    if (text.trimmed().isEmpty())
        settings.remove(key);
    else
        settings.setValue(key, text);
}

void Rustrx::distributeRooms(const QVariantList &rooms)
{
    if (&rooms != &m_lastRooms)
        m_lastRooms = rooms;
    QVariantList groups, dms, favs, everything, targets, spaces;
    const bool hideMutedOn = hideMuted();
    int unread = 0, chats = 0;
    m_roomsById.clear();
    for (const QVariant &v : rooms) {
        QVariantMap r = v.toMap();
        const QString id = r.value("room_id").toString();
        const bool invited = r.value("state").toString() == QLatin1String("invited");
        const bool fav = r.value("is_fav").toBool();
        //: list section for pending invitations
        //% "Invitations"
        r.insert("section", invited ? qtTrId("rustrx-se-invitations") : QString());
        m_roomsById.insert(id, r);
        // a joined space goes to the spaces only (an invitation to one stays
        // with the invitations)
        if (r.value("is_space").toBool() && !invited) {
            r.insert("section", QString());
            spaces.append(r);
            continue;
        }
        // the same number the row badge shows: notifications, else unread messages
        const int rowUnread = r.value("notifications").toInt() > 0 ? r.value("notifications").toInt()
                                                                   : r.value("unread").toInt();
        if (!invited && rowUnread > 0) {
            unread += rowUnread;
            ++chats;
        }
        // the search in the filter menu: only rooms whose name or address
        // contains it (the unread counts above still count every room)
        const bool shown = (m_roomFilter.isEmpty()
                || r.value("name").toString().contains(m_roomFilter, Qt::CaseInsensitive)
                || r.value("alias").toString().contains(m_roomFilter, Qt::CaseInsensitive))
                // "Hide muted" in the filter menu (invitations always show)
                && !(hideMutedOn && r.value("muted").toBool() && !invited);
        if (shown) {
            if (r.value("is_dm").toBool())
                dms.append(r);
            else
                groups.append(r);
            // the one list keeps the core's order (by activity) across rooms
            // and people; joining the two lists would put every room first
            everything.append(r);
        }
        if (!invited)
            targets.append(r);
        if (shown && fav && !invited) {
            // the Favs tab mixes rooms and people, by activity, no sections
            QVariantMap f = r;
            f.insert("section", QString());
            favs.append(f);
        }
        // notifications while running: a room other than the open one gained notifications
        const int n = r.value("notifications").toInt();
        const int before = m_notificationCounts.value(id, -1);
        if (before >= 0 && n > before && id != m_openRoomId)
            emit newMessages(id, r.value("name").toString(), n - before);
        m_notificationCounts.insert(id, n);
    }
    // every invitation once, with a banner: the ones already notified are
    // kept in the settings (pruned to the open ones), so a restart does not
    // repeat them and one that came while Rustrx was closed still shows
    {
        QSettings settings;
        const QStringList notified = settings.value("invitesNotified").toStringList();
        QStringList open;
        for (const QVariant &v : rooms) {
            const QVariantMap r = v.toMap();
            if (r.value("state").toString() != QLatin1String("invited"))
                continue;
            const QString id = r.value("room_id").toString();
            open << id;
            if (!notified.contains(id))
                emit invited(id, r.value("name").toString(), r.value("inviter").toString(), r.value("is_dm").toBool());
        }
        // an invitation answered some other way (another client, a join
        // outside the list): its banner goes
        for (const QString &id : notified)
            if (!open.contains(id))
                emit inviteGone(id);
        if (open != notified)
            settings.setValue("invitesNotified", open);
    }
    sortRooms(groups);
    sortRooms(dms);
    sortRooms(favs);
    m_rooms->setRooms(groups);
    m_people->setRooms(dms);
    m_favourites->setRooms(favs);
    // rooms and people together, in the same order
    sortRooms(everything);
    m_all->setRooms(everything);
    sortRooms(spaces);
    m_spaces->setRooms(spaces);
    sortRooms(targets);
    m_targets->setRooms(targets);
    if (unread != m_unreadTotal || chats != m_unreadChats) {
        m_unreadTotal = unread;
        m_unreadChats = chats;
        emit unreadChanged();
    }
}

void Rustrx::handleEvent(const QVariantMap &ev)
{
    const QString type = ev.value("ev").toString();
    if (type == QLatin1String("log")) {
        // the core's own log, routed here because the sandbox drops its stderr
        // the core's own level is in the line (WARN/INFO/...); warn only on its warnings
        const QString line = ev.value("line").toString();
        if (line.contains(QLatin1String(" WARN ")) || line.contains(QLatin1String(" ERROR ")))
            qWarning().noquote() << "core:" << line;
        else
            qInfo().noquote() << "core:" << line;
        return;
    }
    if (type == QLatin1String("error"))
        qWarning().noquote() << "core error:" << ev.value("cmd").toString() << ev.value("message").toString();
    if (type == QLatin1String("ready") || type == QLatin1String("logged_in") || type == QLatin1String("sync_state")
            || (type == QLatin1String("rooms") && m_firstRooms)) {
        qInfo().noquote() << "startup:" << type << "after" << m_since.elapsed() << "ms";
        if (type == QLatin1String("rooms")) {
            m_firstRooms = false;
            emit roomsLoadedChanged();
        }
    }
    if ((type == QLatin1String("ready") || type == QLatin1String("logged_in"))
            && ev.value("proto").toInt() != kProto)
        qWarning() << "core speaks protocol" << ev.value("proto").toInt() << "the UI expects" << kProto;
    if (type == QLatin1String("ready")) {
        m_ready = true;
        m_loggedIn = ev.value("logged_in").toBool();
        m_keptUser = ev.value("kept_user").toString();
        emit stateChanged();
    } else if (type == QLatin1String("logged_in")) {
        m_logoutReason.clear();
        m_ready = true;
        m_loggedIn = true;
        m_loggingIn = false;
        m_userId = ev.value("user_id").toString();
        m_homeserver = ev.value("homeserver").toString();
        emit stateChanged();
    } else if (type == QLatin1String("login_error")) {
        m_loggingIn = false;
        emit stateChanged();
        emit loginError(ev.value("message").toString());
    } else if (type == QLatin1String("logged_out")) {
        m_loggedIn = false;
        m_logoutReason = ev.value("reason").toString();
        m_keptUser = ev.value("user_id").toString();
        m_userId.clear();
        m_openRoomId.clear();
        m_timeline->clear();
        m_rooms->setRooms({});
        m_people->setRooms({});
        m_favourites->setRooms({});
        m_all->setRooms({});
        m_targets->setRooms({});
        m_spaces->setRooms({});
        m_knocks.clear();
        m_knocksNotified.clear();
        emit knockRequestsChanged();
        m_notificationCounts.clear();
        m_roomsById.clear();
        m_homeserver.clear();
        m_syncState = QStringLiteral("idle");
        m_verificationState = QStringLiteral("unknown");
        m_recoveryState = QStringLiteral("unknown");
        m_firstRooms = true;
        emit roomsLoadedChanged();
        if (m_unreadTotal || m_unreadChats) {
            m_unreadTotal = m_unreadChats = 0;
            emit unreadChanged();
        }
        // the drafts belonged to that account
        QSettings settings;
        settings.remove(QStringLiteral("drafts"));
        emit stateChanged();
        emit openRoomChanged();
        emit syncStateChanged();
        emit encryptionChanged();
    } else if (type == QLatin1String("server_info")) {
        emit serverInfo(ev);
    } else if (type == QLatin1String("sync_state")) {
        m_syncState = ev.value("state").toString();
        emit syncStateChanged();
    } else if (type == QLatin1String("rooms")) {
        distributeRooms(ev.value("rooms").toList());
    } else if (type == QLatin1String("timeline")) {
        if (ev.value("room_id").toString() == m_openRoomId)
            m_timeline->setItems(ev.value("items").toList());
    } else if (type == QLatin1String("media")) {
        if (ev.value("exported").toBool())
            emit mediaExported(ev.value("event_id").toString(), ev.value("path").toString());
        else {
            m_timeline->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                     ev.value("path").toString());
            m_thread->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                   ev.value("path").toString());
        }
    } else if (type == QLatin1String("emote")) {
        const QString mxc = ev.value("mxc").toString(), path = ev.value("path").toString();
        m_timeline->setEmotePath(mxc, path);
        m_thread->setEmotePath(mxc, path);
        m_emotePaths.insert(mxc, path);
        emit emoteReady(mxc, QStringLiteral("file://") + path);
    } else if (type == QLatin1String("thread_timeline")) {
        if (ev.value("root").toString() == m_threadRoot)
            m_thread->setItems(ev.value("items").toList());
    } else if (type == QLatin1String("space_rooms")) {
        emit spaceRooms(ev.value("space_id").toString(), ev.value("rooms").toList(), ev.value("loading").toBool());
    } else if (type == QLatin1String("room_preview")) {
        emit roomPreview(ev);
    } else if (type == QLatin1String("knocked")) {
        emit knocked(ev.value("room_id").toString());
    } else if (type == QLatin1String("room_link")) {
        emit roomLink(ev.value("room_id").toString(), ev.value("link").toString());
    } else if (type == QLatin1String("knock_requests")) {
        const QString roomId = ev.value("room_id").toString();
        const QVariantList requests = ev.value("requests").toList();
        if (requests.isEmpty())
            m_knocks.remove(roomId);
        else
            m_knocks.insert(roomId, ev);
        for (const QVariant &v : requests) {
            const QVariantMap r = v.toMap();
            const QString id = r.value("event_id").toString();
            if (r.value("seen").toBool() || m_knocksNotified.contains(id))
                continue;
            m_knocksNotified.insert(id);
            emit newKnock(roomId, ev.value("room_name").toString(), ev.value("is_space").toBool(),
                          r.value("name").toString(), r.value("reason").toString());
        }
        emit knockRequestsChanged();
    } else if (type == QLatin1String("calls_allowed")) {
        emit callsAllowed(ev.value("room_id").toString());
    } else if (type == QLatin1String("call_ring")) {
        emit callRing(ev.value("room_id").toString(), ev.value("room_name").toString(),
                      ev.value("sender_name").toString(), ev.value("ring").toBool(),
                      ev.value("event_id").toString(), ev.value("expires_ms").toInt());
    } else if (type == QLatin1String("image_packs")) {
        emit imagePacks(ev.value("room_id").toString(), ev.value("packs").toList());
    } else if (type == QLatin1String("emote_added")) {
        emit emoteAdded(ev.value("shortcode").toString());
    } else if (type == QLatin1String("open_url")) {
        emit browserUrl(ev.value("url").toString());
    } else if (type == QLatin1String("identity_reset_approval")) {
        emit identityResetApproval(ev.value("url").toString());
    } else if (type == QLatin1String("error")) {
        const QString cmd = ev.value("cmd").toString();
        if (cmd == QLatin1String("login_password") || cmd == QLatin1String("login_sso")) {
            m_loggingIn = false;
            emit stateChanged();
            emit loginError(ev.value("message").toString());
        } else {
            emit errorOccurred(cmd, ev.value("message").toString());
        }
    } else if (type == QLatin1String("verification")) {
        emit verification(ev);
    } else if (type == QLatin1String("sas")) {
        emit sas(ev);
    } else if (type == QLatin1String("verification_state")) {
        m_verificationState = ev.value("state").toString();
        emit encryptionChanged();
    } else if (type == QLatin1String("recovery_state")) {
        m_recoveryState = ev.value("state").toString();
        emit encryptionChanged();
    } else if (type == QLatin1String("recovery_key")) {
        emit recoveryKey(ev.value("key").toString());
    } else if (type == QLatin1String("recovery_done")) {
        emit recoveryDone();
    } else if (type == QLatin1String("members")) {
        emit members(ev.value("room_id").toString(), ev.value("members").toList());
    } else if (type == QLatin1String("devices")) {
        emit devices(ev.value("devices").toList());
    } else if (type == QLatin1String("device_deleted")) {
        emit deviceDeleted(ev.value("device_id").toString());
    } else if (type == QLatin1String("profile")) {
        emit profile(ev);
    } else if (type == QLatin1String("dm_ready")) {
        emit dmReady(ev.value("room_id").toString(), ev.value("user_id").toString());
    } else if (type == QLatin1String("joined")) {
        emit joined(ev.value("room_id").toString());
    } else if (type == QLatin1String("paginated")) {
        emit paginated(ev.value("room_id").toString(), ev.value("at_start").toBool());
    } else if (type == QLatin1String("search_results")) {
        emit searchResults(ev);
    } else if (type == QLatin1String("typing")) {
        emit typing(ev.value("room_id").toString(), ev.value("names").toStringList());
    } else if (type == QLatin1String("notification_mode")) {
        emit notificationMode(ev.value("room_id").toString(), ev.value("mode").toString());
    } else if (type == QLatin1String("keys_exported")) {
        emit keysExported(ev.value("path").toString());
    } else if (type == QLatin1String("storage")) {
        emit storageInfo(ev);
    } else if (type == QLatin1String("keys_imported")) {
        emit keysImported(ev.value("imported").toInt(), ev.value("total").toInt());
    } else if (type == QLatin1String("call_url")) {
        emit callUrl(ev.value("room_id").toString(), ev.value("url").toString());
    } else if (type == QLatin1String("call_message")) {
        emit callMessage(ev.value("data").toString());
    } else if (type == QLatin1String("call_ended")) {
        emit callEnded(ev.value("room_id").toString());
    } else if (type == QLatin1String("event_source")) {
        emit eventSource(ev.value("event_id").toString(), ev.value("json").toString(), ev.value("edit").toString());
    }
}
