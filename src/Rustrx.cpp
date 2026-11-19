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
#include "DebugLog.h"
#include "Media.h"
#include "RoomLists.h"
#include "RoomModel.h"
#include "ShareAdaptor.h"
#include "TimelineModel.h"
#include "Twemoji.h"
#include "UnifiedPush.h"
#include "Voip.h"
#include "Keys.h"
#include "rustrx_core.h"
#include <sailfishapp.h>

#include <QDBusConnection>
#include <QDBusError>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTimer>

// The core's tokio threads call eventCallback until the process ends; the
// lock and the pointer keep them away from a Rustrx that is being destroyed.
static QMutex s_lock;
static Rustrx *s_instance = nullptr;

QString Rustrx::localPath(const QString &path)
{
    return Media::localPath(path);
}

QString Rustrx::busName() { return QString::fromLatin1(Keys::kBusName); }
QString Rustrx::objectPath() { return QString::fromLatin1(Keys::kObjectPath); }
QString Rustrx::interfaceName() { return QString::fromLatin1(Keys::kInterface); }
QString Rustrx::connectorName() { return QString::fromLatin1(UnifiedPush::kConnectorName); }

QString Rustrx::settingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/harbour-rustrx.conf");
}

Rustrx::Rustrx(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_settings(settingsFile(), QSettings::IniFormat)
    , m_lists(new RoomLists(&m_settings, this))
    , m_media(new Media(&m_settings, this))
    , m_timeline(new TimelineModel(this))
    , m_thread(new TimelineModel(this))
{
    s_instance = this;
    connect(m_lists, &RoomLists::settingsChanged, this, &Rustrx::settingsChanged);
    connect(m_lists, &RoomLists::roomFilterChanged, this, &Rustrx::roomFilterChanged);
    connect(m_lists, &RoomLists::unreadChanged, this, &Rustrx::unreadChanged);
    connect(m_lists, &RoomLists::knockRequestsChanged, this, &Rustrx::knockRequestsChanged);
    connect(m_lists, &RoomLists::newKnock, this, &Rustrx::newKnock);
    connect(m_lists, &RoomLists::newMessages, this, &Rustrx::newMessages);
    connect(m_lists, &RoomLists::roomRead, this, &Rustrx::roomRead);
    connect(m_lists, &RoomLists::invited, this, &Rustrx::invited);
    connect(m_lists, &RoomLists::inviteGone, this, &Rustrx::inviteGone);
    connect(m_media, &Media::command, this, &Rustrx::command);
    connect(m_media, &Media::errorOccurred, this, &Rustrx::errorOccurred);
    connect(m_media, &Media::emoteReady, this, &Rustrx::emoteReady);
    connect(m_media, &Media::settingsChanged, this, &Rustrx::settingsChanged);
    m_push = new UnifiedPush(dataDir, this);
    connect(m_push, &UnifiedPush::command, this, &Rustrx::command);
    m_voip = new Voip(this);
    connect(m_voip, &Voip::command, this, &Rustrx::command);
    // a push: the woken process fetches the message; a running app or
    // stand-in has it from its sync anyway
    connect(m_push, &UnifiedPush::pushed, this, [this](const QString &roomId, const QString &eventId) {
        if (!m_handover.pushWake())
            return;
        const QVariantMap cmd{ { "cmd", "push_handle" }, { "room_id", roomId }, { "event_id", eventId } };
        // the session may still be on its way: after it
        if (m_loggedIn)
            command(cmd);
        else
            m_pendingPushes << cmd;
    });
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
    QLibrary secrets(SailfishApp::pathTo(QString::fromLatin1(Keys::kSecretsPlugin)).toLocalFile());
    typedef int (*StoreKeyFn)(char *, int);
    if (StoreKeyFn fn = reinterpret_cast<StoreKeyFn>(secrets.resolve(Keys::kSecretsSymbol))) {
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
    m_handleEvent = staticMetaObject.method(staticMetaObject.indexOfMethod(
            QMetaObject::normalizedSignature("handleEvent(QVariantMap)").constData()));
    if (!m_handleEvent.isValid())
        qWarning() << "handleEvent not found: no event will arrive";
    const int rc = rustrx_start(dataDir.toUtf8().constData(), &Rustrx::eventCallback, this);
    if (rc != 0)
        qWarning() << "rustrx_start failed:" << rc;
    qInfo().noquote() << "startup: core started after" << m_since.elapsed() << "ms";

    QDBusConnection bus = QDBusConnection::sessionBus();
    // the share sheet calls /share/<method>, the method named in the desktop
    // file's X-Share-Methods ("room"); /share stays for direct callers
    ShareAdaptor *share = new ShareAdaptor(this);
    bus.registerObject(QString::fromLatin1(Keys::kSharePath), share, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(QString::fromLatin1(Keys::kShareRoomPath), share, QDBusConnection::ExportScriptableSlots);
    // the name itself is main()'s, taken before the store is opened
    if (!bus.registerObject(QString::fromLatin1(Keys::kObjectPath), this, QDBusConnection::ExportScriptableSlots))
        qWarning() << "D-Bus object not registered:" << bus.lastError().message();
}

Rustrx::~Rustrx()
{
    QMutexLocker lock(&s_lock);
    rustrx_stop();
    s_instance = nullptr;
}

void Rustrx::receiveShare(const QStringList &files, const QString &text)
{
    m_handover.setPendingShare(files, text);
    if (m_handover.handOver())
        return;
    emit activateRequested();
    emit shareRequested(files, text);
}

void Rustrx::openRoomFromNotification(const QString &roomId)
{
    m_handover.setPendingRoom(roomId);
    if (m_handover.handOver())
        return;
    emit activateRequested();
    emit openRoomRequested(roomId);
}

void Rustrx::open()
{
    if (!m_handover.handOver())
        emit activateRequested();
}

void Rustrx::answerCall(const QString &roomId)
{
    // a classic call ringing in that room: answered here, its page follows
    // the call's state
    if (m_voip->state() == QLatin1String("incoming") && m_voip->roomId() == roomId) {
        m_voip->answer();
        emit callAnswered(roomId);
        open();
        return;
    }
    m_handover.setPendingCall(roomId);
    emit callAnswered(roomId);
    openRoomFromNotification(roomId);
}

void Rustrx::markRoomRead(const QString &roomId)
{
    qInfo() << "notification: mark as read";
    emit markReadRequested(roomId);
}

void Rustrx::replyFromNotification(const QString &roomId, const QString &text)
{
    qInfo() << "notification: reply";
    emit replyRequested(roomId, text);
}

void Rustrx::reactFromNotification(const QString &roomId, const QString &eventId, const QString &key)
{
    qInfo() << "notification: reaction";
    emit reactRequested(roomId, eventId, key);
}

void Rustrx::openKnocks()
{
    m_handover.setPendingKnocks(true);
    if (m_handover.handOver())
        return;
    emit activateRequested();
    emit knocksRequested();
}

void Rustrx::declineCall(const QString &roomId, const QString &eventId)
{
    emit callAnswered(roomId);
    if (m_voip->state() == QLatin1String("incoming") && m_voip->roomId() == roomId) {
        m_voip->reject();
        return;
    }
    if (!eventId.isEmpty())
        command({ { "cmd", "call_decline" }, { "room_id", roomId }, { "event_id", eventId } });
}

void Rustrx::openUrl(const QStringList &urls)
{
    if (urls.isEmpty()) {
        open();
        return;
    }
    const QString uri = urls.first();
    m_handover.setPendingUri(uri);
    if (m_handover.handOver())
        return;
    emit activateRequested();
    emit matrixUriRequested(uri);
}

void Rustrx::quit()
{
    emit quitRequested();
    // time for the close to reach the notification server
    QTimer::singleShot(Keys::kQuitDelayMs, qApp, &QCoreApplication::quit);
}

bool Rustrx::activate()
{
    if (!m_handover.standIn() && !m_handover.pushWake()) {
        emit activateRequested();
        return false;
    }
    qInfo() << "stand-in: the app is starting, leaving";
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

bool Rustrx::background() const
{
    return m_handover.background();
}

void Rustrx::setBackground(bool on)
{
    if (on == background())
        return;
    m_handover.setBackground(on);
    emit settingsChanged();
}

bool Rustrx::downscaleImages() const
{
    return m_media->downscaleImages();
}

void Rustrx::setDownscaleImages(bool on)
{
    m_media->setDownscaleImages(on);
}

bool Rustrx::stripMetadata() const
{
    return m_media->stripMetadata();
}

void Rustrx::setStripMetadata(bool on)
{
    m_media->setStripMetadata(on);
}

bool Rustrx::debugLog() const
{
    return m_settings.value(QLatin1String(Keys::kDebugLog), Keys::kDebugLogDefault).toBool();
}

void Rustrx::setDebugLog(bool on)
{
    if (on == debugLog())
        return;
    m_settings.setValue(QLatin1String(Keys::kDebugLog), on);
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

int Rustrx::clearLog()
{
    const int copies = DebugLog::clear(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    qInfo() << "debug log: cleared," << copies << "shared copies removed";
    return copies;
}

// the room lists' state, kept in RoomLists
int Rustrx::unreadTotal() const { return m_lists->unreadTotal(); }
int Rustrx::unreadChats() const { return m_lists->unreadChats(); }
RoomModel *Rustrx::rooms() const { return m_lists->rooms(); }
RoomModel *Rustrx::people() const { return m_lists->people(); }
RoomModel *Rustrx::favourites() const { return m_lists->favourites(); }
RoomModel *Rustrx::all() const { return m_lists->all(); }
RoomModel *Rustrx::spaces() const { return m_lists->spaces(); }
RoomModel *Rustrx::targets() const { return m_lists->targets(); }
QVariantList Rustrx::knockRequests() const { return m_lists->knockRequests(); }
QString Rustrx::currentTab() const { return m_lists->currentTab(); }
void Rustrx::setCurrentTab(const QString &kind) { m_lists->setCurrentTab(kind); }
QString Rustrx::sortOrder() const { return m_lists->sortOrder(); }
void Rustrx::setSortOrder(const QString &order) { m_lists->setSortOrder(order); }
bool Rustrx::favsOnTop() const { return m_lists->favsOnTop(); }
void Rustrx::setFavsOnTop(bool on) { m_lists->setFavsOnTop(on); }
bool Rustrx::lowLast() const { return m_lists->lowLast(); }
void Rustrx::setLowLast(bool on) { m_lists->setLowLast(on); }
bool Rustrx::hideMuted() const { return m_lists->hideMuted(); }
void Rustrx::setHideMuted(bool on) { m_lists->setHideMuted(on); }
QString Rustrx::roomFilter() const { return m_lists->roomFilter(); }
void Rustrx::setRoomFilter(const QString &text) { m_lists->setRoomFilter(text); }
QVariantMap Rustrx::roomInfo(const QString &roomId) const { return m_lists->roomInfo(roomId); }
QString Rustrx::firstUnreadRoom() const { return m_lists->firstUnreadRoom(); }

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
    self->m_handleEvent.invoke(self, Qt::QueuedConnection, Q_ARG(QVariantMap, doc.object().toVariantMap()));
}

QString Rustrx::dev(const QString &json)
{
    const QString flag = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QLatin1Char('/') + QLatin1String(Keys::kDevCommandsFlag);
    if (!QFile::exists(flag))
        return QStringLiteral("{\"error\":\"dev commands are off\"}");
    const QVariantMap req = QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
    qInfo().noquote() << "dev" << req.value("cmd", req.value("ui", req.keys().value(0))).toString();
    QJsonObject out;
    out["stand_in"] = m_handover.standIn();
    // the stand-in has no window: no pages, no call page
    if (m_handover.standIn() && (req.contains("ui") || req.contains("voip") || req.value("cmd").toString().startsWith(QLatin1String("call_")))) {
        out["error"] = QStringLiteral("the stand-in is running, not the app");
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    // never through a test hook, flag or not: what hands over keys or the
    // session, or ends it
    static const QStringList refused{ "export_keys", "import_keys", "logout", "delete_device",
                                      "login_password", "login_sso", "login_oauth", "login_qr", "qr_check_code", "recovery_recover", "recovery_enable",
                                      "recovery_reset", "identity_reset", "push_set", "push_remove" };
    if (refused.contains(req.value("cmd").toString())) {
        out["error"] = QStringLiteral("not a dev command");
    } else if (req.contains("cmd")) {
        command(req);
        out["ok"] = true;
    } else if (req.contains("voip")) {
        // calls driven from a shell: place (room_id, invitee, tone), answer,
        // hangup, status; "tone" plays a test tone instead of the microphone
        // and measures the far end instead of playing it
        const QString what = req.value("voip").toString();
        if (req.contains("tone"))
            m_voip->setTestTone(req.value("tone").toBool());
        if (what == QLatin1String("place"))
            m_voip->place(req.value("room_id").toString(), req.value("invitee").toString(), req.value("invitee").toString());
        else if (what == QLatin1String("answer"))
            m_voip->answer();
        else if (what == QLatin1String("hangup"))
            m_voip->hangup();
        out["voip"] = QJsonObject::fromVariantMap(m_voip->status());
    } else if (req.contains("rooms")) {
        QJsonArray rooms;
        const QHash<QString, QVariantMap> &byId = m_lists->roomsById();
        for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
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
        m_media->clearEmotes();
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

void Rustrx::loginOauth(const QString &user, const QString &homeserver, bool signup)
{
    m_loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_oauth"}, {"user", user}, {"homeserver", homeserver}, {"signup", signup}});
}

void Rustrx::loginQr(const QString &user, const QString &homeserver)
{
    m_loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_qr"}, {"user", user}, {"homeserver", homeserver}});
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
    command({ { "cmd", "open_thread" }, { "room_id", roomId }, { "thread_root", root } });
}

void Rustrx::closeThread()
{
    m_threadRoot.clear();
    m_thread->clear();
    command({ { "cmd", "close_thread" } });
}

void Rustrx::sendText(const QString &roomId, const QString &body, const QString &replyTo,
                      const QVariantMap &emotes, const QString &threadRoot, const QVariantMap &options)
{
    QVariantMap cmd{{"cmd", "send_text"}, {"room_id", roomId}, {"body", body}};
    if (options.contains("markdown"))
        cmd.insert("markdown", options.value("markdown").toBool());
    if (options.value("emote").toBool())
        cmd.insert("emote", true);
    if (!options.value("mentions").toList().isEmpty())
        cmd.insert("mentions", options.value("mentions"));
    if (!threadRoot.isEmpty())
        cmd.insert("thread_root", threadRoot);
    if (!replyTo.isEmpty())
        cmd.insert("reply_to", replyTo);
    if (!emotes.isEmpty())
        cmd.insert("emotes", emotes);
    command(cmd);
}

QString Rustrx::emojify(const QString &html, int height) const
{
    return Twemoji::inHtml(html, height);
}

QString Rustrx::emojiUrl(const QString &emoji) const
{
    return Twemoji::url(emoji);
}

// the media side, kept in Media
QString Rustrx::emotePath(const QString &mxc)
{
    return m_media->emotePath(mxc);
}

void Rustrx::setRoomAvatar(const QString &roomId, const QString &path)
{
    m_media->setRoomAvatar(roomId, path);
}

void Rustrx::setOwnAvatar(const QString &path)
{
    m_media->setOwnAvatar(path);
}

void Rustrx::sendFile(const QString &roomId, const QString &path, const QString &caption,
                      const QString &threadRoot)
{
    m_media->sendFile(roomId, path, caption, threadRoot);
}

void Rustrx::sendVoice(const QString &roomId, const QString &path, qint64 durationMs, const QString &caption)
{
    m_media->sendVoice(roomId, path, durationMs, caption);
}

void Rustrx::getMedia(const QString &roomId, const QString &eventId, bool thumb)
{
    m_media->getMedia(roomId, eventId, thumb);
}

void Rustrx::exportMedia(const QString &roomId, const QString &eventId)
{
    m_media->exportMedia(roomId, eventId);
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

// Drafts live in the core's store (encrypted where the store is); this is
// the copy QML reads at once, filled as rooms open ("draft").
QString Rustrx::draft(const QString &roomId) const
{
    return m_drafts.value(roomId);
}

void Rustrx::setDraft(const QString &roomId, const QString &text)
{
    if (m_drafts.value(roomId) == text)
        return;
    if (text.trimmed().isEmpty())
        m_drafts.remove(roomId);
    else
        m_drafts.insert(roomId, text);
    command({ { "cmd", "save_draft" }, { "room_id", roomId }, { "text", text.trimmed().isEmpty() ? QString() : text } });
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
    if (type == QLatin1String("ready") || type == QLatin1String("logged_in") || type == QLatin1String("sync_state")
            || (type == QLatin1String("rooms") && m_firstRooms)) {
        qInfo().noquote() << "startup:" << type << "after" << m_since.elapsed() << "ms";
        if (type == QLatin1String("rooms")) {
            m_firstRooms = false;
            emit roomsLoadedChanged();
        }
    }
    if ((type == QLatin1String("ready") || type == QLatin1String("logged_in"))
            && ev.value("proto").toInt() != Keys::kProto)
        qWarning() << "core speaks protocol" << ev.value("proto").toInt() << "the UI expects" << Keys::kProto;
    if (type == QLatin1String("ready")) {
        m_ready = true;
        m_loggedIn = ev.value("logged_in").toBool();
        m_keptUser = ev.value("kept_user").toString();
        emit stateChanged();
        // the stand-in has nothing to do logged out. A session it could not
        // open yet (the store's key before the first unlock) is retried by
        // its unit after a failure exit; a real logout ends it for good
        if (m_handover.standIn() && !m_loggedIn) {
            const bool retry = ev.value("restore_failed").toBool();
            qWarning() << "stand-in: not logged in," << (retry ? "the unit tries again later" : "leaving");
            QTimer::singleShot(0, qApp, [retry]() { QCoreApplication::exit(retry ? 1 : 0); });
        }
    } else if (type == QLatin1String("logged_in")) {
        m_logoutReason.clear();
        m_ready = true;
        m_loggedIn = true;
        m_loggingIn = false;
        m_userId = ev.value("user_id").toString();
        m_voip->setOwnUserId(m_userId);
        m_homeserver = ev.value("homeserver").toString();
        m_storeState = ev.value("store").toString();
        emit stateChanged();
        emit encryptionChanged();
        m_push->loggedIn();
        // not for one push: the woken process is gone in seconds
        if (!m_handover.pushWake())
            m_push->checkPusher();
        // drafts from before 0.4.7 sat in the plain settings file: into the
        // store with them, once
        m_settings.beginGroup(QLatin1String(Keys::kDrafts));
        const QStringList old = m_settings.childKeys();
        for (const QString &roomId : old)
            command({ { "cmd", "save_draft" }, { "room_id", roomId }, { "text", m_settings.value(roomId).toString() } });
        m_settings.endGroup();
        m_settings.remove(QLatin1String(Keys::kDrafts));
        // and the thread counts the UI kept itself until 0.4.7
        m_settings.remove(QLatin1String(Keys::kThreadsSeen));
        for (const QVariantMap &cmd : m_pendingPushes)
            command(cmd);
        m_pendingPushes.clear();
    } else if (type == QLatin1String("login_error")) {
        m_loggingIn = false;
        emit stateChanged();
        emit loginError(ev.value("message").toString());
    } else if (type == QLatin1String("logged_out")) {
        m_loggedIn = false;
        m_loggingIn = false;
        m_push->loggedOut();
        m_pendingPushes.clear();
        m_logoutReason = ev.value("reason").toString();
        m_keptUser = ev.value("user_id").toString();
        m_userId.clear();
        m_openRoomId.clear();
        m_timeline->clear();
        m_threadRoot.clear();
        m_thread->clear();
        m_media->clearEmotes();
        // the lists, the requests to join, the invitation banners
        m_lists->clear();
        m_homeserver.clear();
        m_syncState = QStringLiteral("idle");
        m_verificationState = QStringLiteral("unknown");
        m_recoveryState = QStringLiteral("unknown");
        m_storeState.clear();
        m_firstRooms = true;
        emit roomsLoadedChanged();
        // after roomsLoadedChanged, as before the move
        m_lists->clearUnread();
        // the drafts and what was read in threads belonged to that account
        m_drafts.clear();
        m_settings.remove(QLatin1String(Keys::kDrafts));
        m_settings.remove(QLatin1String(Keys::kThreadsSeen));
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
        m_lists->setRooms(ev.value("rooms").toList());
    } else if (type == QLatin1String("timeline")) {
        if (ev.value("room_id").toString() == m_openRoomId)
            m_timeline->setItems(ev.value("items").toList());
    } else if (type == QLatin1String("media")) {
        if (ev.value("exported").toBool())
            emit mediaExported(ev.value("event_id").toString(), ev.value("path").toString());
        else {
            m_timeline->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                     ev.value("path").toString());
            if (!m_threadRoot.isEmpty())
                m_thread->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                       ev.value("path").toString());
        }
    } else if (type == QLatin1String("emote")) {
        const QString mxc = ev.value("mxc").toString(), path = ev.value("path").toString();
        m_timeline->setEmotePath(mxc, path);
        m_thread->setEmotePath(mxc, path);
        m_media->emoteArrived(mxc, path);
    } else if (type == QLatin1String("thread_list")) {
        emit threadList(ev.value("room_id").toString(), ev.value("threads").toList(), ev.value("loading").toBool());
    } else if (type == QLatin1String("thread_timeline")) {
        if (ev.value("thread_root").toString() == m_threadRoot)
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
        m_lists->setKnockRequests(ev);
    } else if (type == QLatin1String("pusher_checked")) {
        m_push->pusherChecked(ev.value("endpoint").toString(), ev.value("present").toBool());
    } else if (type == QLatin1String("pusher_set")) {
        m_push->pusherSet(ev.value("endpoint").toString());
    } else if (type == QLatin1String("push_notification")) {
        emit pushNotification(ev.value("room_id").toString(), ev.value("event_id").toString(), ev.value("room_name").toString(),
                              ev.value("sender_name").toString(), ev.value("body").toString(),
                              ev.value("is_dm").toBool(), ev.value("noisy").toBool());
    } else if (type == QLatin1String("calls_allowed")) {
        emit callsAllowed(ev.value("room_id").toString());
    } else if (type == QLatin1String("voip")) {
        m_voip->handleEvent(ev);
    } else if (type == QLatin1String("turn_server")) {
        m_voip->handleTurn(ev);
    } else if (type == QLatin1String("voip_sent")) {
        m_voip->handleSent(ev);
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
        qWarning().noquote() << "core error:" << cmd << ev.value("message").toString();
        if (cmd == QLatin1String("login_password") || cmd == QLatin1String("login_sso")
                || cmd == QLatin1String("login_oauth") || cmd == QLatin1String("login_qr")) {
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
        emit recoveryKey(ev.value("recovery_key").toString());
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
    } else if (type == QLatin1String("qr_login")) {
        emit qrLogin(ev);
    } else if (type == QLatin1String("user_info")) {
        emit userInfo(ev);
    } else if (type == QLatin1String("ignored_users")) {
        emit ignoredUsers(ev.value("users").toList());
    } else if (type == QLatin1String("draft")) {
        const QString roomId = ev.value("room_id").toString();
        const QString text = ev.value("text").toString();
        if (text.isEmpty())
            m_drafts.remove(roomId);
        else
            m_drafts.insert(roomId, text);
        emit draftLoaded(roomId, text);
    } else if (type == QLatin1String("paginated")) {
        emit paginated(ev.value("room_id").toString(), ev.value("at_start").toBool());
    } else if (type == QLatin1String("search_results")) {
        emit searchResults(ev);
    } else if (type == QLatin1String("room_search_results")) {
        emit roomSearchResults(ev);
    } else if (type == QLatin1String("room_settings")) {
        emit roomSettings(ev);
    } else if (type == QLatin1String("room_upgraded")) {
        emit roomUpgraded(ev.value("room_id").toString(), ev.value("replacement").toString());
    } else if (type == QLatin1String("reported")) {
        emit reported(ev);
    } else if (type == QLatin1String("space_banned")) {
        emit spaceBanned(ev);
    } else if (type == QLatin1String("redact_progress")) {
        emit redactProgress(ev);
    } else if (type == QLatin1String("alias_added")) {
        emit aliasAdded(ev.value("room_id").toString(), ev.value("alias").toString());
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
