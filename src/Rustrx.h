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
#ifndef RUSTRX_H
#define RUSTRX_H

#include <QCoreApplication>
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QElapsedTimer>

class RoomModel;
class TimelineModel;

// The bridge to librustrx: commands go out as JSON, events come back on
// tokio threads and are re-posted onto the GUI thread before anything here
// changes. One instance per process, exposed to QML as `rustrx`.
class Rustrx : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.harbour_rustrx")
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool loggingIn READ loggingIn NOTIFY stateChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString homeserver READ homeserver NOTIFY stateChanged)
    Q_PROPERTY(QString logoutReason READ logoutReason NOTIFY stateChanged)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged)
    Q_PROPERTY(QString verificationState READ verificationState NOTIFY encryptionChanged)
    Q_PROPERTY(QString recoveryState READ recoveryState NOTIFY encryptionChanged)
    Q_PROPERTY(QString openRoomId READ openRoomId NOTIFY openRoomChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadChanged)
    Q_PROPERTY(int unreadChats READ unreadChats NOTIFY unreadChanged)
    Q_PROPERTY(RoomModel *rooms READ rooms CONSTANT)
    Q_PROPERTY(RoomModel *people READ people CONSTANT)
    Q_PROPERTY(RoomModel *favourites READ favourites CONSTANT)
    Q_PROPERTY(TimelineModel *timeline READ timeline CONSTANT)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(bool downscaleImages READ downscaleImages WRITE setDownscaleImages NOTIFY settingsChanged)
    // the order of the room lists: "activity", "unread" (unread first) or "name"
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY settingsChanged)
    // notifications with the app closed: a stand-in, started with the session
    // and when the app is closed, wakes every few minutes
    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY settingsChanged)
    // this process is the stand-in: no window, ever
    Q_PROPERTY(bool standIn READ standIn CONSTANT)

public:
    explicit Rustrx(const QString &dataDir, QObject *parent = nullptr);
    ~Rustrx();

    bool ready() const { return m_ready; }
    bool loggedIn() const { return m_loggedIn; }
    bool loggingIn() const { return m_loggingIn; }
    QString userId() const { return m_userId; }
    QString homeserver() const { return m_homeserver; }
    QString syncState() const { return m_syncState; }
    QString logoutReason() const { return m_logoutReason; }
    QString verificationState() const { return m_verificationState; }
    QString recoveryState() const { return m_recoveryState; }
    QString openRoomId() const { return m_openRoomId; }
    int unreadTotal() const { return m_unreadTotal; }
    int unreadChats() const { return m_unreadChats; }
    RoomModel *rooms() const { return m_rooms; }
    RoomModel *people() const { return m_people; }
    RoomModel *favourites() const { return m_favourites; }
    TimelineModel *timeline() const { return m_timeline; }
    QString coreVersion() const;
    bool downscaleImages() const;
    void setDownscaleImages(bool on);
    QString sortOrder() const;
    void setSortOrder(const QString &order);
    bool background() const;
    void setBackground(bool on);
    bool standIn() const { return m_standIn; }
    void setStandIn(bool on) { m_standIn = on; }
    // the file the stand-in's unit looks for (in the sandbox's own config folder)
    static QString backgroundFlagPath();
    // a user unit: the stand-in (harbour-rustrx.service) or the app for it to
    // hand over to (harbour-rustrx-open.service)
    static bool startUnit(const QString &unit);
    // what the stand-in passed on: the room, link or share to show
    void takeHandover();

    Q_INVOKABLE void command(const QVariantMap &cmd);
    Q_INVOKABLE void loginPassword(const QString &user, const QString &password, const QString &homeserver);
    Q_INVOKABLE void loginSso(const QString &user, const QString &homeserver, const QString &idp);
    Q_INVOKABLE void probeServer(const QString &user, const QString &homeserver);
    Q_INVOKABLE void openRoom(const QString &roomId);
    Q_INVOKABLE void closeRoom();
    Q_INVOKABLE void sendText(const QString &roomId, const QString &body, const QString &replyTo);
    Q_INVOKABLE void sendFile(const QString &roomId, const QString &path, const QString &caption = QString());
    Q_INVOKABLE void sendVoice(const QString &roomId, const QString &path, qint64 durationMs);
    Q_INVOKABLE void getMedia(const QString &roomId, const QString &eventId, bool thumb);
    // a copy in Downloads, where other apps may read it; answered by mediaExported
    Q_INVOKABLE void exportMedia(const QString &roomId, const QString &eventId);
    Q_INVOKABLE QVariantMap roomInfo(const QString &roomId) const;
    // the most recent room with unread messages, for the cover; empty if none
    Q_INVOKABLE QString firstUnreadRoom() const;
    Q_INVOKABLE QString formatTime(const QVariant &ms) const;
    Q_INVOKABLE QString formatDate(const QVariant &ms) const;
    // unsent text per room, kept across pages and restarts
    // the newest JPEG the stock camera saved after `sinceMs`, or an empty string
    Q_INVOKABLE QString newestPhotoSince(qint64 sinceMs) const;
    Q_INVOKABLE QString draft(const QString &roomId) const;
    Q_INVOKABLE void setDraft(const QString &roomId, const QString &text);

signals:
    void stateChanged();
    void settingsChanged();
    void syncStateChanged();
    void encryptionChanged();
    void openRoomChanged();
    void unreadChanged();
    void loginError(const QString &message);
    void serverInfo(const QVariantMap &info);
    void openUrl(const QString &url);
    void errorOccurred(const QString &cmd, const QString &message);
    void verification(const QVariantMap &data);
    void sas(const QVariantMap &data);
    void recoveryKey(const QString &key);
    void recoveryDone();
    void members(const QString &roomId, const QVariantList &members);
    void devices(const QVariantList &devices);
    void deviceDeleted(const QString &deviceId);
    void searchResults(const QVariantMap &results);
    void profile(const QVariantMap &profile);
    void dmReady(const QString &roomId, const QString &userId);
    void joined(const QString &roomId);
    void paginated(const QString &roomId, bool atStart);
    void mediaExported(const QString &eventId, const QString &path);
    void newMessages(const QString &roomId, const QString &roomName, int count);
    // who is typing in the open room, the own user left out
    void typing(const QString &roomId, const QStringList &names);
    // "all", "mentions", "mute" or "default" (the account's rule)
    void notificationMode(const QString &roomId, const QString &mode);
    void keysExported(const QString &path);
    void keysImported(int imported, int total);
    // sizes in bytes: store, media_files, and the SDK's per-store sizes
    void storageInfo(const QVariantMap &info);
    // a notification was tapped: show this room
    void openRoomRequested(const QString &roomId);
    // show the window (a second launch, a notification, a link, a share)
    void activateRequested();
    // close the window as the tile's close does (debug and tests)
    void closeRequested();
    // the process is about to end for good: the stand-in's line goes
    void quitRequested();
    // a matrix: or matrix.to link to follow
    void matrixUriRequested(const QString &uri);
    // the share sheet hands over files (local paths) and/or a text
    void shareRequested(const QStringList &files, const QString &text);

public slots:
    // D-Bus net.mowerk.harbour_rustrx.openRoom, the notifications' action
    // kept until QML takes it: a cold start delivers the call before QML is loaded
    Q_SCRIPTABLE void openRoomFromNotification(const QString &roomId)
    {
        m_pendingRoom = roomId;
        if (handOver())
            return;
        emit activateRequested();
        emit openRoomRequested(roomId);
    }
    // a second launch: true when this is the stand-in, which leaves at once,
    // and the caller waits for the name to go and becomes the app
    Q_SCRIPTABLE bool activate();
    // the stand-in's notification: open the app
    Q_SCRIPTABLE void open() { if (!handOver()) emit activateRequested(); }
    // the stand-in's notification: Quit (after QML has closed that notification)
    Q_SCRIPTABLE void quit();
public:
    Q_INVOKABLE void closeWindow() { emit closeRequested(); }
    Q_INVOKABLE QString takePendingRoom() { const QString r = m_pendingRoom; m_pendingRoom.clear(); return r; }
    // a link or a share that arrived before QML was loaded
    Q_INVOKABLE QString takePendingUri() { const QString r = m_pendingUri; m_pendingUri.clear(); return r; }
    Q_INVOKABLE QVariantMap takePendingShare() { const QVariantMap r = m_pendingShare; m_pendingShare.clear(); return r; }
    // called by the share adaptor
    void receiveShare(const QStringList &files, const QString &text);
public slots:
    // D-Bus net.mowerk.harbour_rustrx.openUrl, the desktop file's handler for
    // matrix: links; the launcher icon calls it too, without a link
    Q_SCRIPTABLE void openUrl(const QStringList &urls)
    {
        if (urls.isEmpty()) {
            open();
            return;
        }
        m_pendingUri = urls.first();
        if (handOver())
            return;
        emit activateRequested();
        emit matrixUriRequested(m_pendingUri);
    }

private slots:
    void handleEvent(const QVariantMap &ev);

private:
    static void eventCallback(void *ctx, const char *json);
    void distributeRooms(const QVariantList &rooms);
    void sortRooms(QVariantList &rooms) const;
    QVariantList m_lastRooms;   // as the core sent them, for a new order
    // the stand-in: pass what was asked for to a new app process and leave
    bool handOver();

    bool m_ready = false;
    bool m_loggedIn = false;
    bool m_loggingIn = false;
    QString m_userId;
    QString m_homeserver;
    QString m_syncState = QStringLiteral("idle");
    QString m_logoutReason;
    QString m_verificationState = QStringLiteral("unknown");
    QString m_recoveryState = QStringLiteral("unknown");
    QString m_openRoomId;
    QString m_pendingRoom;
    bool m_standIn = false;
    QString m_pendingUri;
    QVariantMap m_pendingShare;
    int m_unreadTotal = 0;
    int m_unreadChats = 0;
    QHash<QString, int> m_notificationCounts;
    QHash<QString, QVariantMap> m_roomsById;
    RoomModel *m_rooms;
    RoomModel *m_people;
    RoomModel *m_favourites;
    TimelineModel *m_timeline;
    QElapsedTimer m_since;   // start-up timing in the journal
    bool m_firstRooms = true;
};

#endif
