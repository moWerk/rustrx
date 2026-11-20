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
#include "DevHook.h"
#include "Media.h"
#include "RoomLists.h"
#include "RoomModel.h"
#include "ShareAdaptor.h"
#include "TimelineModel.h"
#include "Twemoji.h"
#include "UnifiedPush.h"
#include "Voip.h"
#include "CallRoute.h"
#include "CallState.h"
#include "Keys.h"
#include "Wipe.h"
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

// The store's key from the sailfish-secrets plugin, loaded only where it
// can be (its library is not a dependency of the package), handed to the
// core. The key goes from the plugin's buffer straight to the core, which
// keeps its own copy; this one is wiped (Wipe.h, audit B13). False when
// there is none (no plugin, Secrets locked, its prompt not answered).
bool Rustrx::handStoreKeyOn()
{
    QElapsedTimer secretsTime;
    secretsTime.start();
    char key[128];
    bool haveKey = false;
    QLibrary secrets(SailfishApp::pathTo(QString::fromLatin1(Keys::kSecretsPlugin)).toLocalFile());
    typedef int (*StoreKeyFn)(char *, int);
    if (StoreKeyFn fn = reinterpret_cast<StoreKeyFn>(secrets.resolve(Keys::kSecretsSymbol))) {
        const int n = fn(key, sizeof key);
        haveKey = n > 0 && n < int(sizeof key);
    } else {
        qInfo().noquote() << "startup: no sailfish-secrets plugin:" << secrets.errorString();
    }
    qInfo().noquote() << "startup: store key" << (haveKey ? "from sailfish-secrets" : "none")
                      << "after" << secretsTime.elapsed() << "ms";
    if (haveKey) {
        key[sizeof key - 1] = 0;
        rustrx_set_store_key(key);
    }
    Wipe::bytes(key, sizeof key);
    return haveKey;
}

bool Rustrx::callDark() const
{
    return m_callState->blanked();
}

void Rustrx::setCallNear(bool near)
{
    if (!m_handover.standIn())
        m_callState->setNear(near);
}

void Rustrx::retryRestore()
{
    if (!m_cur->restoreFailed || m_cur->restoring)
        return;
    m_cur->restoring = true;
    emit stateChanged();
    // after the page has shown that it is asking: Secrets' prompt comes
    // over it, and this waits for the answer; every account waiting for
    // the key gets it
    QTimer::singleShot(Keys::kRestoreRetryPaintMs, this, [this]() {
        const bool key = handStoreKeyOn();
        for (Account *a : m_accounts) {
            if (!a->restoreFailed)
                continue;
            if (key) {
                commandTo(a->key, { { "cmd", "restore" } });
            } else {
                a->restoring = false;
                if (a == m_cur)
                    emit stateChanged();
            }
        }
    });
}

QString Rustrx::settingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/harbour-rustrx.conf");
}

Rustrx::Rustrx(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_settings(settingsFile(), QSettings::IniFormat)
    , m_media(new Media(&m_settings, this))
    , m_timeline(new TimelineModel(this))
    , m_thread(new TimelineModel(this))
{
    s_instance = this;
    m_dataDir = dataDir;
    connect(m_media, &Media::command, this, &Rustrx::command);
    connect(m_media, &Media::errorOccurred, this, &Rustrx::errorOccurred);
    connect(m_media, &Media::emoteReady, this, &Rustrx::emoteReady);
    connect(m_media, &Media::settingsChanged, this, &Rustrx::settingsChanged);
    // push is the first account's (docs/MULTI-ACCOUNT.md)
    m_push = new UnifiedPush(dataDir, this);
    connect(m_push, &UnifiedPush::command, this, [this](const QVariantMap &cmd) { commandTo(0, cmd); });
    m_voip = new Voip(this);
    // a call's events go to the account it is on
    connect(m_voip, &Voip::command, this, [this](const QVariantMap &cmd) { commandTo(m_voipKey, cmd); });
    m_voip->setRelayOnly(callsRelayOnly());
    // the earpiece during a call (the loudspeaker on the speaker button)
    m_callRoute = new CallRoute(this);
    connect(m_voip, &Voip::audioNeeded, m_callRoute, &CallRoute::setActive);
    connect(m_voip, &Voip::speakerChanged, m_callRoute, &CallRoute::setSpeaker);
    // PulseAudio makes the stream a moment after the player starts
    connect(m_voip, &Voip::farAudioStarted, m_callRoute, [this] {
        for (int ms : Keys::kCallRouteFollowMs)
            QTimer::singleShot(ms, m_callRoute, &CallRoute::follow);
    });
    // the screen goes dark at the ear, as in the phone app; the app's call
    // only (the stand-in rings through its notification)
    m_callState = new CallState(this);
    connect(m_voip, &Voip::changed, m_callState, [this]() {
        if (!m_handover.standIn())
            m_callState->follow(m_voip->state());
    });
    connect(m_voip, &Voip::speakerChanged, m_callState, &CallState::setSpeaker);
    connect(m_callState, &CallState::blankedChanged, this, &Rustrx::callDarkChanged);
    // the stand-in rings but never runs a call: whatever hands over to the
    // app (a tap, Answer) takes the ringing call along
    connect(m_voip, &Voip::changed, this, [this]() {
        if (m_handover.standIn()) {
            m_handover.setPendingVoip(m_voip->ringing());
            m_handover.setPendingAccount(m_voipKey);
        }
    });
    // a push: the woken process fetches the message; a running app or
    // stand-in has it from its sync anyway
    connect(m_push, &UnifiedPush::pushed, this, [this](const QString &roomId, const QString &eventId) {
        if (!m_handover.pushWake())
            return;
        const QVariantMap cmd{ { "cmd", "push_handle" }, { "room_id", roomId }, { "event_id", eventId } };
        // the session may still be on its way: after it
        if (m_accounts.value(0) && m_accounts.first()->loggedIn)
            commandTo(0, cmd);
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
    handStoreKeyOn();
    m_handleEvent = staticMetaObject.method(staticMetaObject.indexOfMethod(
            QMetaObject::normalizedSignature("handleEvent(QVariantMap)").constData()));
    if (!m_handleEvent.isValid())
        qWarning() << "handleEvent not found: no event will arrive";
    // the first account in the data folder as always, the others in
    // accounts/<n>; a folder without a session is left from a logout or a
    // login given up, and goes
    m_cur = startAccount(0, dataDir);
    QDir more(dataDir + QLatin1String("/accounts"));
    QList<int> keys;
    for (const QString &name : more.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int key = name.toInt(&ok);
        if (!ok || key <= 0)
            continue;
        if (!QFile::exists(more.filePath(name + QLatin1String("/session.json")))
                && !QFile::exists(more.filePath(name + QLatin1String("/relogin.json")))) {
            qInfo() << "accounts: folder" << key << "has no login, removed";
            QDir(more.filePath(name)).removeRecursively();
            continue;
        }
        keys << key;
    }
    std::sort(keys.begin(), keys.end());
    for (int key : keys)
        startAccount(key, more.filePath(QString::number(key)));
    if (Account *last = account(m_settings.value(QLatin1String(Keys::kShownAccount), 0).toInt()))
        m_cur = last;
    qInfo().noquote() << "startup:" << m_accounts.size() << "accounts, cores started after" << m_since.elapsed() << "ms";

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

void Rustrx::openAccountRoom(const QString &account, const QString &roomId)
{
    const int key = account.toInt();
    m_handover.setPendingAccount(key);
    // the app shows that account; the stand-in hands it over with the room
    requestShow(key);
    openRoomFromNotification(roomId);
}

QString Rustrx::takePendingRoom()
{
    const int key = m_handover.takePendingAccount();
    const QString room = m_handover.takePendingRoom();
    if (!room.isEmpty())
        requestShow(key);
    return room;
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
        emit callAnswered(roomId);
        if (m_handover.standIn() || m_handover.pushWake()) {
            QVariantMap invite = m_voip->ringing();
            invite.insert("answer", true);
            m_handover.setPendingVoip(invite);
            m_handover.handOver();
            return;
        }
        m_voip->answer();
        open();
        return;
    }
    m_handover.setPendingCall(roomId);
    emit callAnswered(roomId);
    openRoomFromNotification(roomId);
}

void Rustrx::answerAccountCall(const QString &account, const QString &roomId)
{
    const int key = account.toInt();
    m_handover.setPendingAccount(key);
    requestShow(key);
    answerCall(roomId);
}

void Rustrx::declineAccountCall(const QString &account, const QString &roomId, const QString &eventId)
{
    emit callAnswered(roomId);
    if (m_voip->state() == QLatin1String("incoming") && m_voip->roomId() == roomId && m_voipKey == account.toInt()) {
        m_voip->reject();
        return;
    }
    if (!eventId.isEmpty())
        commandTo(account.toInt(), { { "cmd", "call_decline" }, { "room_id", roomId }, { "event_id", eventId } });
}

// a notification's buttons from before 0.7 name no account: the first one's
void Rustrx::markRoomRead(const QString &roomId)
{
    markAccountRoomRead(QStringLiteral("0"), roomId);
}

void Rustrx::replyFromNotification(const QString &roomId, const QString &text)
{
    replyFromAccountNotification(QStringLiteral("0"), roomId, text);
}

void Rustrx::reactFromNotification(const QString &roomId, const QString &eventId, const QString &key)
{
    reactFromAccountNotification(QStringLiteral("0"), roomId, eventId, key);
}

void Rustrx::markAccountRoomRead(const QString &account, const QString &roomId)
{
    qInfo() << "notification: mark as read, account" << account;
    emit markReadRequested(roomId, account.toInt());
}

void Rustrx::replyFromAccountNotification(const QString &account, const QString &roomId, const QString &text)
{
    qInfo() << "notification: reply, account" << account;
    emit replyRequested(roomId, text, account.toInt());
}

void Rustrx::reactFromAccountNotification(const QString &account, const QString &roomId,
                                          const QString &eventId, const QString &key)
{
    qInfo() << "notification: reaction, account" << account;
    emit reactRequested(roomId, eventId, key, account.toInt());
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

int Rustrx::metadataLevel() const
{
    return m_media->metadataLevel();
}

void Rustrx::setMetadataLevel(int level)
{
    m_media->setMetadataLevel(level);
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

bool Rustrx::callsRelayOnly() const
{
    return m_settings.value(QLatin1String(Keys::kVoipRelayOnly), Keys::kVoipRelayOnlyDefault).toBool();
}

void Rustrx::setCallsRelayOnly(bool on)
{
    if (on == callsRelayOnly())
        return;
    m_settings.setValue(QLatin1String(Keys::kVoipRelayOnly), on);
    m_voip->setRelayOnly(on);
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

// the room lists' state, kept in each account's RoomLists
int Rustrx::unreadTotal() const { return m_cur->lists->unreadTotal(); }
int Rustrx::unreadChats() const { return m_cur->lists->unreadChats(); }
RoomModel *Rustrx::rooms() const { return m_cur->lists->rooms(); }
RoomModel *Rustrx::people() const { return m_cur->lists->people(); }
RoomModel *Rustrx::favourites() const { return m_cur->lists->favourites(); }
RoomModel *Rustrx::all() const { return m_cur->lists->all(); }
RoomModel *Rustrx::spaces() const { return m_cur->lists->spaces(); }
RoomModel *Rustrx::targets() const { return m_cur->lists->targets(); }
QVariantList Rustrx::knockRequests() const { return m_cur->lists->knockRequests(); }
QString Rustrx::currentTab() const { return m_cur->lists->currentTab(); }
void Rustrx::setCurrentTab(const QString &kind) { m_cur->lists->setCurrentTab(kind); }
QString Rustrx::sortOrder() const { return m_cur->lists->sortOrder(); }
void Rustrx::setSortOrder(const QString &order) { m_cur->lists->setSortOrder(order); }
bool Rustrx::favsOnTop() const { return m_cur->lists->favsOnTop(); }
void Rustrx::setFavsOnTop(bool on) { m_cur->lists->setFavsOnTop(on); }
bool Rustrx::lowLast() const { return m_cur->lists->lowLast(); }
void Rustrx::setLowLast(bool on) { m_cur->lists->setLowLast(on); }
bool Rustrx::hideMuted() const { return m_cur->lists->hideMuted(); }
void Rustrx::setHideMuted(bool on) { m_cur->lists->setHideMuted(on); }
QString Rustrx::roomFilter() const { return m_cur->lists->roomFilter(); }
void Rustrx::setRoomFilter(const QString &text) { m_cur->lists->setRoomFilter(text); }
QVariantMap Rustrx::roomInfo(const QString &roomId) const { return m_cur->lists->roomInfo(roomId); }
QString Rustrx::firstUnreadRoom() const { return m_cur->lists->firstUnreadRoom(); }

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
    Rustrx *self = s_instance;
    if (!self)
        return;
    // whose core it came from: its account (fixed from its start on)
    QVariantMap ev = doc.object().toVariantMap();
    ev.insert(QStringLiteral("account"), static_cast<const Account *>(ctx)->key);
    self->m_handleEvent.invoke(self, Qt::QueuedConnection, Q_ARG(QVariantMap, ev));
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
    out["push_wake"] = m_handover.pushWake();
    // the stand-in has no window: no pages, no call page
    if (m_handover.standIn() && (req.contains("ui") || req.contains("voip") || req.value("cmd").toString().startsWith(QLatin1String("call_")))) {
        out["error"] = QStringLiteral("the stand-in is running, not the app");
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    // only what the test tooling sends (DevHook.h), flag or not
    const QString refused = DevHook::refusal(req);
    if (!refused.isEmpty()) {
        qWarning().noquote() << "dev: refused," << refused;
        out["error"] = refused;
    } else if (req.contains("cmd")) {
        command(req);
        out["ok"] = true;
    } else if (req.value("quit").toBool()) {
        // tests: a clean exit before an install (a kill while the window is
        // up has crashed lipstick, on both phones)
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
        out["ok"] = true;
    } else if (req.contains("voip")) {
        // calls driven from a shell: place (room_id, invitee, tone), answer,
        // hangup, status; "tone" plays a test tone instead of the microphone
        // and measures the far end instead of playing it
        const QString what = req.value("voip").toString();
        if (req.contains("tone"))
            m_voip->setTestTone(req.value("tone").toBool());
        if (req.contains("aec"))
            m_voip->setEchoCancelling(req.value("aec").toBool());
        if (req.contains("speaker"))
            m_voip->setSpeaker(req.value("speaker").toBool());
        if (what == QLatin1String("place"))
            m_voip->place(req.value("room_id").toString(), req.value("invitee").toString(), req.value("invitee").toString());
        else if (what == QLatin1String("answer"))
            m_voip->answer();
        else if (what == QLatin1String("hangup"))
            m_voip->hangup();
        out["voip"] = QJsonObject::fromVariantMap(m_voip->status());
    } else if (req.contains("rooms")) {
        QJsonArray rooms;
        const QHash<QString, QVariantMap> &byId = m_cur->lists->roomsById();
        for (auto it = byId.cbegin(); it != byId.cend(); ++it) {
            QJsonObject r;
            for (const char *key : { "room_id", "name", "is_dm", "encrypted", "unread", "notifications", "state" })
                r[key] = QJsonValue::fromVariant(it.value().value(key));
            rooms.append(r);
        }
        out["rooms"] = rooms;
    } else if (req.contains("state")) {
        out["logged_in"] = m_cur->loggedIn;
        out["sync"] = m_cur->syncState;
        out["open_room"] = m_openRoomId;
        out["verification"] = m_cur->verificationState;
        out["recovery"] = m_cur->recoveryState;
        out["new_login"] = m_cur->newLogin;
        out["account"] = m_cur->key;
        out["accounts"] = QJsonArray::fromVariantList(accounts());
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
    // one call at a time: no Element Call while a classic call runs (two
    // audio paths at once)
    if (cmd.value("cmd").toString() == QLatin1String("call_start") && m_voip->state() != QLatin1String("idle")) {
        qInfo() << "call_start refused: a classic call is going on";
        return;
    }
    // the emote pictures live in the media folder
    if (cmd.value("cmd").toString() == QLatin1String("clear_media")) {
        m_timeline->clearEmotes();
        m_thread->clearEmotes();
        m_media->clearEmotes();
    }
    commandTo(m_cur->key, cmd);
}

void Rustrx::commandTo(int key, const QVariantMap &cmd)
{
    Account *a = account(key);
    if (!a) {
        qWarning().noquote() << "command" << cmd.value("cmd").toString() << "for no account" << key;
        return;
    }
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(cmd)).toJson(QJsonDocument::Compact);
    const int rc = rustrx_command_to(a->core, json.constData());
    // one line per command in the journal; the password is not part of the message
    if (rc)
        qWarning().noquote() << "command" << cmd.value("cmd").toString() << "account" << key << "rc" << rc;
    else
        qInfo().noquote() << "command" << cmd.value("cmd").toString() << "account" << key;
}

void Rustrx::loginPassword(const QString &user, const QString &password, const QString &homeserver)
{
    // one login at a time (the core refuses a second one too, audit K11)
    if (m_cur->loggingIn)
        return;
    m_cur->loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_password"}, {"user", user}, {"password", password}, {"homeserver", homeserver}});
}

void Rustrx::loginSso(const QString &user, const QString &homeserver, const QString &idp)
{
    if (m_cur->loggingIn)
        return;
    m_cur->loggingIn = true;
    emit stateChanged();
    QVariantMap cmd{{"cmd", "login_sso"}, {"user", user}, {"homeserver", homeserver}};
    if (!idp.isEmpty())
        cmd.insert("idp", idp);
    command(cmd);
}

void Rustrx::loginOauth(const QString &user, const QString &homeserver, bool signup)
{
    if (m_cur->loggingIn)
        return;
    m_cur->loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_oauth"}, {"user", user}, {"homeserver", homeserver}, {"signup", signup}});
}

void Rustrx::loginQr(const QString &user, const QString &homeserver)
{
    if (m_cur->loggingIn)
        return;
    m_cur->loggingIn = true;
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
    return m_cur->drafts.value(roomId);
}

void Rustrx::setDraft(const QString &roomId, const QString &text)
{
    if (m_cur->drafts.value(roomId) == text)
        return;
    if (text.trimmed().isEmpty())
        m_cur->drafts.remove(roomId);
    else
        m_cur->drafts.insert(roomId, text);
    command({ { "cmd", "save_draft" }, { "room_id", roomId }, { "text", text.trimmed().isEmpty() ? QString() : text } });
}

static bool isLoginCommand(const QString &cmd)
{
    return cmd == QLatin1String("login_password") || cmd == QLatin1String("login_sso")
            || cmd == QLatin1String("login_oauth") || cmd == QLatin1String("login_qr");
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
    Account *a = account(ev.value("account").toInt());
    if (!a)
        return;
    if (type == QLatin1String("ready") || type == QLatin1String("logged_in") || type == QLatin1String("sync_state")
            || (type == QLatin1String("rooms") && a->firstRooms))
        qInfo().noquote() << "startup:" << type << "account" << a->key << "after" << m_since.elapsed() << "ms";
    if ((type == QLatin1String("ready") || type == QLatin1String("logged_in"))
            && ev.value("proto").toInt() != Keys::kProto)
        qWarning() << "core speaks protocol" << ev.value("proto").toInt() << "the UI expects" << Keys::kProto;
    handleAccountEvent(a, type, ev);
    // the rest answers what the shown account's pages asked for
    if (a != m_cur)
        return;
    if (type == QLatin1String("server_info")) {
        emit serverInfo(ev);
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
    } else if (type == QLatin1String("calls_allowed")) {
        emit callsAllowed(ev.value("room_id").toString());
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
        if (!isLoginCommand(cmd) && cmd != QLatin1String("voip_send"))
            emit errorOccurred(cmd, ev.value("message").toString());
    } else if (type == QLatin1String("verification")) {
        emit verification(ev);
    } else if (type == QLatin1String("sas")) {
        emit sas(ev);
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
        emit draftLoaded(ev.value("room_id").toString(), ev.value("text").toString());
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

// Every account's events that change its own state: its login, sync,
// encryption, lists (and through them the notifications), drafts, its
// calls and push. The UI hears of a change only for the shown account.
void Rustrx::handleAccountEvent(Account *a, const QString &type, const QVariantMap &ev)
{
    const bool shown = a == m_cur;
    if (type == QLatin1String("ready")) {
        a->ready = true;
        a->loggedIn = ev.value("logged_in").toBool();
        a->keptUser = ev.value("kept_user").toString();
        // a saved session the store's key was missing for (Secrets' prompt
        // not answered): not a logout, the start page offers to ask again
        a->restoreFailed = ev.value("restore_failed").toBool();
        a->restoring = false;
        if (shown)
            emit stateChanged();
        emit accountsChanged();
        leaveIfNobodyLoggedIn();
    } else if (type == QLatin1String("logged_in")) {
        a->logoutReason.clear();
        a->restoreFailed = false;
        a->restoring = false;
        a->ready = true;
        a->loggedIn = true;
        // only a login started here makes a new device; a restore does not
        a->newLogin = a->loggingIn;
        a->loggingIn = false;
        a->userId = ev.value("user_id").toString();
        a->homeserver = ev.value("homeserver").toString();
        a->storeState = ev.value("store").toString();
        if (a->key == m_voipKey)
            m_voip->setOwnUserId(a->userId);
        // a call that rang in the stand-in, handed over with the app's start
        if (!m_handover.standIn() && !m_handover.pushWake()) {
            if (m_pendingVoip.isEmpty()) {
                const int key = m_handover.takePendingAccount();
                m_pendingVoip = m_handover.takePendingVoip();
                m_pendingVoip.insert(QStringLiteral("account"), key);
                // the room's account comes back for takePendingRoom
                m_handover.setPendingAccount(key);
            }
            if (m_pendingVoip.value("account").toInt() == a->key) {
                QVariantMap invite = m_pendingVoip;
                m_pendingVoip.clear();
                invite.remove(QStringLiteral("account"));
                if (!invite.isEmpty()) {
                    m_voipKey = a->key;
                    m_voip->setOwnUserId(a->userId);
                    if (invite.value("answer").toBool())
                        requestShow(a->key);
                    m_voip->takeRinging(invite);
                }
            }
        }
        // the own name and picture, for the account button
        commandTo(a->key, { { "cmd", "profile" } });
        if (shown) {
            emit stateChanged();
            emit encryptionChanged();
        }
        emit accountsChanged();
        if (a->key == 0) {
            m_push->loggedIn();
            // not for one push: the woken process is gone in seconds
            if (!m_handover.pushWake())
                m_push->checkPusher();
            // drafts from before 0.4.7 sat in the plain settings file: into the
            // store with them, once
            m_settings.beginGroup(QLatin1String(Keys::kDrafts));
            const QStringList old = m_settings.childKeys();
            for (const QString &roomId : old)
                commandTo(0, { { "cmd", "save_draft" }, { "room_id", roomId }, { "text", m_settings.value(roomId).toString() } });
            m_settings.endGroup();
            m_settings.remove(QLatin1String(Keys::kDrafts));
            // and the thread counts the UI kept itself until 0.4.7
            m_settings.remove(QLatin1String(Keys::kThreadsSeen));
            for (const QVariantMap &cmd : m_pendingPushes)
                commandTo(0, cmd);
            m_pendingPushes.clear();
        }
    } else if (type == QLatin1String("login_error")
               || (type == QLatin1String("error") && isLoginCommand(ev.value("cmd").toString()))) {
        a->loggingIn = false;
        if (shown) {
            emit stateChanged();
            emit loginError(ev.value("message").toString());
        }
    } else if (type == QLatin1String("error")) {
        qWarning().noquote() << "core error: account" << a->key << ev.value("cmd").toString() << ev.value("message").toString();
        // a call event that did not leave: the call cannot be set up
        if (ev.value("cmd").toString() == QLatin1String("voip_send") && a->key == m_voipKey)
            m_voip->sendFailed();
    } else if (type == QLatin1String("logged_out")) {
        a->loggedIn = false;
        a->loggingIn = false;
        a->newLogin = false;
        if (a->key == 0) {
            m_push->loggedOut();
            m_pendingPushes.clear();
        }
        a->logoutReason = ev.value("reason").toString();
        a->keptUser = ev.value("user_id").toString();
        a->userId.clear();
        a->homeserver.clear();
        a->syncState = QStringLiteral("idle");
        a->verificationState = QStringLiteral("unknown");
        a->recoveryState = QStringLiteral("unknown");
        a->storeState.clear();
        a->displayName.clear();
        a->avatarPath.clear();
        a->firstRooms = true;
        a->drafts.clear();
        // the lists, the requests to join, the invitation banners
        a->lists->clear();
        if (shown) {
            m_openRoomId.clear();
            m_timeline->clear();
            m_threadRoot.clear();
            m_thread->clear();
            m_media->clearEmotes();
            emit roomsLoadedChanged();
        }
        // after roomsLoadedChanged, as before the move
        a->lists->clearUnread();
        if (a->key == 0) {
            // the drafts and what was read in threads belonged to that account
            m_settings.remove(QLatin1String(Keys::kDrafts));
            m_settings.remove(QLatin1String(Keys::kThreadsSeen));
        }
        if (shown) {
            emit stateChanged();
            emit openRoomChanged();
            emit syncStateChanged();
            emit encryptionChanged();
        }
        emit accountsChanged();
        // logged out of the shown account while others stay: the next one
        // with a login is shown (one whose session the server ended stays,
        // its login page asks again)
        const bool expired = a->logoutReason == QLatin1String("session_expired");
        if (shown && !expired) {
            for (Account *other : m_accounts)
                if (other != a && other->loggedIn) {
                    requestShow(other->key);
                    break;
                }
        }
        leaveIfNobodyLoggedIn();
    } else if (type == QLatin1String("sync_state")) {
        a->syncState = ev.value("state").toString();
        if (shown)
            emit syncStateChanged();
    } else if (type == QLatin1String("rooms")) {
        const bool first = a->firstRooms;
        a->firstRooms = false;
        a->lists->setRooms(ev.value("rooms").toList());
        if (first && shown)
            emit roomsLoadedChanged();
    } else if (type == QLatin1String("knock_requests")) {
        a->lists->setKnockRequests(ev);
    } else if (type == QLatin1String("verification_state")) {
        a->verificationState = ev.value("state").toString();
        if (shown)
            emit encryptionChanged();
    } else if (type == QLatin1String("recovery_state")) {
        a->recoveryState = ev.value("state").toString();
        if (shown)
            emit encryptionChanged();
    } else if (type == QLatin1String("draft")) {
        const QString roomId = ev.value("room_id").toString();
        const QString text = ev.value("text").toString();
        if (text.isEmpty())
            a->drafts.remove(roomId);
        else
            a->drafts.insert(roomId, text);
    } else if (type == QLatin1String("profile")) {
        a->displayName = ev.value("display_name").toString();
        a->avatarPath = ev.value("avatar_path").toString();
        a->avatarVersion = ev.value("avatar_v").toLongLong();
        emit accountsChanged();
    } else if (type == QLatin1String("call_ring")) {
        emit callRing(ev.value("room_id").toString(), ev.value("room_name").toString(),
                      ev.value("sender_name").toString(), ev.value("ring").toBool(),
                      ev.value("event_id").toString(), ev.value("expires_ms").toInt(), a->key);
    } else if (type == QLatin1String("voip") || type == QLatin1String("turn_server") || type == QLatin1String("voip_sent")) {
        // the call belongs to the account it rings or runs on; another
        // account's call meanwhile is not taken (its caller hears nothing)
        if (a->key != m_voipKey) {
            if (m_voip->state() != QLatin1String("idle")) {
                qInfo() << "voip: account" << a->key << "event while a call runs on" << m_voipKey << ", left";
                return;
            }
            m_voipKey = a->key;
            m_voip->setOwnUserId(a->userId);
        }
        if (type == QLatin1String("voip"))
            m_voip->handleEvent(ev);
        else if (type == QLatin1String("turn_server"))
            m_voip->handleTurn(ev);
        else
            m_voip->handleSent(ev);
    } else if (type == QLatin1String("pusher_checked")) {
        if (a->key == 0)
            m_push->pusherChecked(ev.value("endpoint").toString(), ev.value("present").toBool());
    } else if (type == QLatin1String("pusher_set")) {
        if (a->key == 0)
            m_push->pusherSet(ev.value("endpoint").toString());
    } else if (type == QLatin1String("push_notification")) {
        emit pushNotification(ev.value("room_id").toString(), ev.value("event_id").toString(), ev.value("room_name").toString(),
                              ev.value("sender_name").toString(), ev.value("body").toString(),
                              ev.value("is_dm").toBool(), ev.value("noisy").toBool());
    }
}

Account *Rustrx::startAccount(int key, const QString &dir)
{
    QDir().mkpath(dir);
    Account *a = new Account;
    a->key = key;
    a->dir = dir;
    // the first account's keys stay where they always were
    a->lists = new RoomLists(&m_settings, key ? QStringLiteral("account%1/").arg(key) : QString(), this);
    RoomLists *lists = a->lists;
    // the shown account's lists speak for the app; every account's notify
    connect(lists, &RoomLists::settingsChanged, this, [this, a]() { if (a == m_cur) emit settingsChanged(); });
    connect(lists, &RoomLists::roomFilterChanged, this, [this, a]() { if (a == m_cur) emit roomFilterChanged(); });
    connect(lists, &RoomLists::unreadChanged, this, [this, a]() {
        if (a == m_cur)
            emit unreadChanged();
        emit accountsChanged();
    });
    connect(lists, &RoomLists::knockRequestsChanged, this, [this, a]() { if (a == m_cur) emit knockRequestsChanged(); });
    connect(lists, &RoomLists::newKnock, this,
            [this, a](const QString &roomId, const QString &roomName, bool isSpace, const QString &name, const QString &reason) {
        emit newKnock(roomId, roomName, isSpace, name, reason, a->key);
    });
    connect(lists, &RoomLists::newMessages, this, [this, a](const QString &roomId, const QString &roomName, int count) {
        emit newMessages(roomId, roomName, count, a->key);
    });
    connect(lists, &RoomLists::roomRead, this, [this, a](const QString &roomId) { emit roomRead(roomId, a->key); });
    connect(lists, &RoomLists::invited, this,
            [this, a](const QString &roomId, const QString &roomName, const QString &inviter, bool direct, bool known) {
        emit invited(roomId, roomName, inviter, direct, known, a->key);
    });
    connect(lists, &RoomLists::inviteGone, this, [this, a](const QString &roomId) { emit inviteGone(roomId, a->key); });
    m_accounts << a;
    // the core's events name this account (eventCallback reads a->key)
    a->core = rustrx_start(dir.toUtf8().constData(), &Rustrx::eventCallback, a);
    if (a->core < 0)
        qWarning() << "rustrx_start failed for account" << key << ":" << a->core;
    return a;
}

Account *Rustrx::account(int key) const
{
    for (Account *a : m_accounts)
        if (a->key == key)
            return a;
    return nullptr;
}

QVariantList Rustrx::accounts() const
{
    QVariantList out;
    for (const Account *a : m_accounts) {
        // one with a login, or whose session the server ended (it asks again)
        if (!a->loggedIn && a->keptUser.isEmpty())
            continue;
        out << QVariantMap{
            { "key", a->key },
            { "user_id", a->loggedIn ? a->userId : a->keptUser },
            { "name", a->displayName },
            { "avatar", a->avatarPath },
            { "avatar_v", a->avatarVersion },
            { "logged_in", a->loggedIn },
            { "shown", a == m_cur },
            { "unread", a->lists->unreadChats() },
        };
    }
    return out;
}

bool Rustrx::accountLoggedIn(int key) const
{
    const Account *a = account(key);
    return a && a->loggedIn;
}

int Rustrx::otherUnread() const
{
    int n = 0;
    for (const Account *a : m_accounts)
        if (a != m_cur && a->loggedIn)
            n += a->lists->unreadChats();
    return n;
}

QVariantMap Rustrx::roomInfoIn(int key, const QString &roomId) const
{
    const Account *a = account(key);
    return a ? a->lists->roomInfo(roomId) : QVariantMap();
}

bool Rustrx::switchAccount(int key)
{
    Account *a = account(key);
    if (!a)
        return false;
    if (a == m_cur)
        return true;
    // a call stays with the account it is on, and that one stays shown
    if (m_voip->state() != QLatin1String("idle")) {
        qInfo() << "accounts: no switch during a call";
        return false;
    }
    qInfo() << "accounts: showing" << key;
    closeThread();
    closeRoom();
    m_media->clearEmotes();
    m_timeline->clearEmotes();
    m_thread->clearEmotes();
    m_cur = a;
    a->lists->reloadSettings();
    m_settings.setValue(QLatin1String(Keys::kShownAccount), key);
    emitAllChanged();
    return true;
}

void Rustrx::addAccount()
{
    // an account without a login and nothing kept (logged out, or a login
    // given up) is used again
    for (Account *a : m_accounts)
        if (a->ready && !a->loggedIn && !a->loggingIn && a->keptUser.isEmpty() && !a->restoreFailed) {
            qInfo() << "accounts: a login for account" << a->key;
            emit showAccountRequested(a->key);
            return;
        }
    int key = 0;
    for (const Account *a : m_accounts)
        key = qMax(key, a->key);
    key++;
    qInfo() << "accounts: a login for the new account" << key;
    startAccount(key, m_dataDir + QLatin1String("/accounts/") + QString::number(key));
    emit showAccountRequested(key);
}

void Rustrx::requestShow(int key)
{
    if (m_handover.standIn() || m_handover.pushWake() || key == m_cur->key)
        return;
    emit showAccountRequested(key);
}

void Rustrx::emitAllChanged()
{
    emit shownAccountChanged();
    emit accountsChanged();
    emit stateChanged();
    emit encryptionChanged();
    emit syncStateChanged();
    emit roomsLoadedChanged();
    emit unreadChanged();
    emit knockRequestsChanged();
    emit settingsChanged();
    emit roomFilterChanged();
    emit openRoomChanged();
}

void Rustrx::leaveIfNobodyLoggedIn()
{
    // the stand-in has nothing to do logged out. A session it could not
    // open yet (the store's key before the first unlock) is retried by its
    // unit after a failure exit; a real logout ends it for good
    if (!m_handover.standIn())
        return;
    bool retry = false;
    for (const Account *a : m_accounts) {
        if (!a->ready || a->loggedIn)
            return;
        retry = retry || a->restoreFailed;
    }
    qWarning() << "stand-in: no account logged in," << (retry ? "the unit tries again later" : "leaving");
    QTimer::singleShot(0, qApp, [retry]() { QCoreApplication::exit(retry ? 1 : 0); });
}
