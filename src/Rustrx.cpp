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
#include "Rustrx.h"
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
    , m_timeline(new TimelineModel(this))
{
    s_instance = this;
    m_since.start();
    QDir().mkpath(dataDir);
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

// The core sends the rooms by activity. Invitations stay on top and low
// priority rooms go to the bottom in every order; within that, unread rooms
// first or by name, else the activity order stays (the sort is stable).
void Rustrx::sortRooms(QVariantList &rooms) const
{
    const QString order = sortOrder();
    const auto unread = [](const QVariantMap &r) {
        return r.value("notifications").toInt() > 0 || r.value("unread").toInt() > 0;
    };
    std::stable_sort(rooms.begin(), rooms.end(), [&](const QVariant &va, const QVariant &vb) {
        const QVariantMap a = va.toMap(), b = vb.toMap();
        const bool ia = a.value("state").toString() == QLatin1String("invited");
        const bool ib = b.value("state").toString() == QLatin1String("invited");
        if (ia != ib)
            return ia;
        const bool la = a.value("is_low").toBool(), lb = b.value("is_low").toBool();
        if (la != lb)
            return lb;
        if (order == QLatin1String("unread")) {
            const bool ua = unread(a), ub = unread(b);
            if (ua != ub)
                return ua;
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

void Rustrx::command(const QVariantMap &cmd)
{
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

void Rustrx::sendText(const QString &roomId, const QString &body, const QString &replyTo)
{
    QVariantMap cmd{{"cmd", "send_text"}, {"room_id", roomId}, {"body", body}};
    if (!replyTo.isEmpty())
        cmd.insert("reply_to", replyTo);
    command(cmd);
}

void Rustrx::sendFile(const QString &roomId, const QString &path, const QString &caption)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", p}};
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

void Rustrx::sendVoice(const QString &roomId, const QString &path, qint64 durationMs)
{
    command({{"cmd", "send_file"}, {"room_id", roomId}, {"path", path}, {"voice", true}, {"duration_ms", durationMs}});
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
    QVariantList groups, dms, favs;
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
        // the same number the row badge shows: notifications, else unread messages
        const int rowUnread = r.value("notifications").toInt() > 0 ? r.value("notifications").toInt()
                                                                   : r.value("unread").toInt();
        if (!invited && rowUnread > 0) {
            unread += rowUnread;
            ++chats;
        }
        if (r.value("is_dm").toBool())
            dms.append(r);
        else
            groups.append(r);
        if (fav && !invited) {
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
        if (open != notified)
            settings.setValue("invitesNotified", open);
    }
    sortRooms(groups);
    sortRooms(dms);
    sortRooms(favs);
    m_rooms->setRooms(groups);
    m_people->setRooms(dms);
    m_favourites->setRooms(favs);
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
        if (type == QLatin1String("rooms"))
            m_firstRooms = false;
    }
    if ((type == QLatin1String("ready") || type == QLatin1String("logged_in"))
            && ev.value("proto").toInt() != kProto)
        qWarning() << "core speaks protocol" << ev.value("proto").toInt() << "the UI expects" << kProto;
    if (type == QLatin1String("ready")) {
        m_ready = true;
        m_loggedIn = ev.value("logged_in").toBool();
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
        m_userId.clear();
        m_openRoomId.clear();
        m_timeline->clear();
        m_rooms->setRooms({});
        m_people->setRooms({});
        m_favourites->setRooms({});
        m_notificationCounts.clear();
        m_roomsById.clear();
        m_homeserver.clear();
        m_syncState = QStringLiteral("idle");
        m_verificationState = QStringLiteral("unknown");
        m_recoveryState = QStringLiteral("unknown");
        m_firstRooms = true;
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
        else
            m_timeline->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                     ev.value("path").toString());
    } else if (type == QLatin1String("open_url")) {
        emit openUrl(ev.value("url").toString());
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
    }
}
