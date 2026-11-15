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
#include <QHash>
#include <QMap>
#include <QSet>

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
    // the first room list of this login has arrived (empty or not)
    Q_PROPERTY(bool roomsLoaded READ roomsLoaded NOTIFY roomsLoadedChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY stateChanged)
    Q_PROPERTY(bool loggingIn READ loggingIn NOTIFY stateChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString homeserver READ homeserver NOTIFY stateChanged)
    Q_PROPERTY(QString logoutReason READ logoutReason NOTIFY stateChanged)
    // the user whose store was kept when the server ended the session
    Q_PROPERTY(QString keptUser READ keptUser NOTIFY stateChanged)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged)
    Q_PROPERTY(QString verificationState READ verificationState NOTIFY encryptionChanged)
    Q_PROPERTY(QString recoveryState READ recoveryState NOTIFY encryptionChanged)
    Q_PROPERTY(QString openRoomId READ openRoomId NOTIFY openRoomChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadChanged)
    Q_PROPERTY(int unreadChats READ unreadChats NOTIFY unreadChanged)
    Q_PROPERTY(RoomModel *rooms READ rooms CONSTANT)
    Q_PROPERTY(RoomModel *people READ people CONSTANT)
    Q_PROPERTY(RoomModel *favourites READ favourites CONSTANT)
    // everything in one list, for those who do not want rooms and people apart
    Q_PROPERTY(RoomModel *all READ all CONSTANT)
    // the joined spaces (invitations to spaces stay with the rooms' invitations)
    Q_PROPERTY(RoomModel *spaces READ spaces CONSTANT)
    // every joined room and chat, whatever the filter: what sharing offers
    Q_PROPERTY(RoomModel *targets READ targets CONSTANT)
    Q_PROPERTY(TimelineModel *timeline READ timeline CONSTANT)
    // the open thread's messages (root and replies), beside the room's
    Q_PROPERTY(TimelineModel *thread READ thread CONSTANT)
    // changes whenever a thread is marked seen (bindings read it to update)
    Q_PROPERTY(int threadsSeen READ threadsSeenVersion NOTIFY threadsSeenChanged)
    // open requests to join the rooms and spaces we may invite to, each
    // { room_id, room_name, is_space, event_id, user_id, name, reason, ts, seen }
    Q_PROPERTY(QVariantList knockRequests READ knockRequests NOTIFY knockRequestsChanged)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(bool downscaleImages READ downscaleImages WRITE setDownscaleImages NOTIFY settingsChanged)
    // the order of the room lists: "activity", "unread" (unread first) or "name"
    // the tab the filter menu works on: its order and switches are its own
    Q_PROPERTY(QString currentTab READ currentTab WRITE setCurrentTab NOTIFY settingsChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY settingsChanged)
    // with any order: favourites first; low priority rooms last
    Q_PROPERTY(bool favsOnTop READ favsOnTop WRITE setFavsOnTop NOTIFY settingsChanged)
    Q_PROPERTY(bool lowLast READ lowLast WRITE setLowLast NOTIFY settingsChanged)
    Q_PROPERTY(bool hideMuted READ hideMuted WRITE setHideMuted NOTIFY settingsChanged)
    // the debug log in the data folder (on until 1.0; Settings)
    Q_PROPERTY(bool debugLog READ debugLog WRITE setDebugLog NOTIFY settingsChanged)
    // the lists show only rooms whose name or address contains this (not kept)
    Q_PROPERTY(QString roomFilter READ roomFilter WRITE setRoomFilter NOTIFY roomFilterChanged)
    // notifications with the app closed: a stand-in, started with the session
    // and when the app is closed, wakes every few minutes
    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY settingsChanged)
    // this process is the stand-in: no window, ever
    Q_PROPERTY(bool standIn READ standIn CONSTANT)

public:
    explicit Rustrx(const QString &dataDir, QObject *parent = nullptr);
    ~Rustrx();

    bool ready() const { return m_ready; }
    bool roomsLoaded() const { return !m_firstRooms; }
    bool loggedIn() const { return m_loggedIn; }
    bool loggingIn() const { return m_loggingIn; }
    QString userId() const { return m_userId; }
    QString homeserver() const { return m_homeserver; }
    QString syncState() const { return m_syncState; }
    QString logoutReason() const { return m_logoutReason; }
    QString keptUser() const { return m_keptUser; }
    QString verificationState() const { return m_verificationState; }
    QString recoveryState() const { return m_recoveryState; }
    QString openRoomId() const { return m_openRoomId; }
    int unreadTotal() const { return m_unreadTotal; }
    int unreadChats() const { return m_unreadChats; }
    RoomModel *rooms() const { return m_rooms; }
    RoomModel *people() const { return m_people; }
    RoomModel *favourites() const { return m_favourites; }
    RoomModel *all() const { return m_all; }
    RoomModel *spaces() const { return m_spaces; }
    RoomModel *targets() const { return m_targets; }
    TimelineModel *timeline() const { return m_timeline; }
    TimelineModel *thread() const { return m_thread; }
    int threadsSeenVersion() const { return m_threadsSeenVersion; }
    // a thread beside the open room: its messages come into `thread`
    Q_INVOKABLE void openThread(const QString &roomId, const QString &root);
    Q_INVOKABLE void closeThread();
    // replies in a thread already seen here: the rest show as new. A thread
    // seen the first time counts as read (old threads are not news)
    Q_INVOKABLE int threadSeen(const QString &root, int count);
    Q_INVOKABLE void markThreadSeen(const QString &root, int count);
    QVariantList knockRequests() const;
    QString coreVersion() const;
    bool downscaleImages() const;
    void setDownscaleImages(bool on);
    QString currentTab() const { return m_currentTab; }
    void setCurrentTab(const QString &kind);
    QString sortOrder() const;
    void setSortOrder(const QString &order);
    bool favsOnTop() const;
    void setFavsOnTop(bool on);
    bool lowLast() const;
    void setLowLast(bool on);
    bool hideMuted() const;
    void setHideMuted(bool on);
    bool debugLog() const;
    void setDebugLog(bool on);
    // the logs in one text file in Downloads, for the share sheet; its path
    Q_INVOKABLE QString shareableLog();
    QString roomFilter() const { return m_roomFilter; }
    void setRoomFilter(const QString &text);
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
    // emotes: the custom emoji the text uses, shortcode to mxc address
    // threadRoot: into that thread
    Q_INVOKABLE void sendText(const QString &roomId, const QString &body, const QString &replyTo,
                              const QVariantMap &emotes = QVariantMap(), const QString &threadRoot = QString());
    // a custom emoji's cached picture (file://), or "" while it is fetched
    // (emoteReady follows)
    Q_INVOKABLE QString emotePath(const QString &mxc);
    // emoji as Twemoji pictures: in HTML for a StyledText (height in px),
    // and one emoji's picture ("" when there is none)
    Q_INVOKABLE QString emojify(const QString &html, int height) const;
    Q_INVOKABLE QString emojiUrl(const QString &emoji) const;
    Q_INVOKABLE void sendFile(const QString &roomId, const QString &path, const QString &caption = QString(),
                              const QString &threadRoot = QString());
    // a picture from the phone as the room's picture: upright, at most 512 px
    Q_INVOKABLE void setRoomAvatar(const QString &roomId, const QString &path);
    // a picture from the phone as one's own picture, prepared the same way
    Q_INVOKABLE void setOwnAvatar(const QString &path);
    Q_INVOKABLE void sendVoice(const QString &roomId, const QString &path, qint64 durationMs,
                               const QString &caption = QString());
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
    // a dev command for the UI (see dev())
    void devUi(const QVariantMap &request);
    void stateChanged();
    // the image packs offered in a room (custom emoji and stickers)
    void imagePacks(const QString &roomId, const QVariantList &packs);
    void emoteReady(const QString &mxc, const QString &path);
    void emoteAdded(const QString &shortcode);
    // someone calls (ring) or a group call starts (not ring); expiresMs: how
    // long it may ring
    void callRing(const QString &roomId, const QString &roomName, const QString &senderName,
                  bool ring, const QString &eventId, int expiresMs);
    // answered or declined: the ringing notification goes
    void callAnswered(const QString &roomId);
    // starting a call opened the room's calls to everyone (permissions)
    void callsAllowed(const QString &roomId);
    void knockRequestsChanged();
    void threadsSeenChanged();
    // a room's threads (thread_list): { root, latest, count }
    void threadList(const QString &roomId, const QVariantList &threads, bool loading);
    // a space's rooms (from space_open), loading while pages come
    void spaceRooms(const QString &spaceId, const QVariantList &rooms, bool loading);
    // a room or space before joining: ok false when the server would not tell
    void roomPreview(const QVariantMap &preview);
    // asked to join (knocked)
    void knocked(const QString &roomId);
    // a room's or space's link to share
    void roomLink(const QString &roomId, const QString &link);
    // a request to join that was not notified yet
    void newKnock(const QString &roomId, const QString &roomName, bool isSpace,
                  const QString &name, const QString &reason);
    // the requests' notification was tapped
    void knocksRequested();
    void roomsLoadedChanged();
    void settingsChanged();
    void roomFilterChanged();
    void syncStateChanged();
    void encryptionChanged();
    void openRoomChanged();
    void unreadChanged();
    void loginError(const QString &message);
    void serverInfo(const QVariantMap &info);
    // a web page to show in the browser (the single sign-on page); not
    // "openUrl", which is the D-Bus method below and shadowed it in QML
    void browserUrl(const QString &url);
    // an encryption reset waits for its approval on this page
    void identityResetApproval(const QString &url);
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
    // an invitation not notified before (also one that came while closed)
    void invited(const QString &roomId, const QString &roomName, const QString &inviter, bool direct);
    // an invitation that is no longer one (joined, declined, elsewhere)
    void inviteGone(const QString &roomId);
    // who is typing in the open room, the own user left out
    void typing(const QString &roomId, const QStringList &names);
    // "all", "mentions", "mute" or "default" (the account's rule)
    void notificationMode(const QString &roomId, const QString &mode);
    void keysExported(const QString &path);
    void keysImported(int imported, int total);
    // a call: the Element Call page to show, widget API messages for it, its end
    void callUrl(const QString &roomId, const QString &url);
    void callMessage(const QString &data);
    void callEnded(const QString &roomId);
    // a message's source: its JSON as received, and its latest edit's
    void eventSource(const QString &eventId, const QString &json, const QString &edit);
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
    // an incoming call's notification: Answer opens the room and joins
    // the call (the room starts it once it is open: takePendingCall)
    Q_SCRIPTABLE void answerCall(const QString &roomId)
    {
        m_pendingCall = roomId;
        emit callAnswered(roomId);
        openRoomFromNotification(roomId);
    }
    // the requests' notification: open the app on the list of requests
    Q_SCRIPTABLE void openKnocks()
    {
        m_pendingKnocks = true;
        if (handOver())
            return;
        emit activateRequested();
        emit knocksRequested();
    }
    // Decline: the caller's clients are told (MSC4310)
    Q_SCRIPTABLE void declineCall(const QString &roomId, const QString &eventId)
    {
        emit callAnswered(roomId);
        if (!eventId.isEmpty())
            command({ { "cmd", "call_decline" }, { "room_id", roomId }, { "event_id", eventId } });
    }
    // the stand-in's notification: Quit (after QML has closed that notification)
    Q_SCRIPTABLE void quit();
    // D-Bus net.mowerk.harbour_rustrx.dev, for tests from a shell (ssh),
    // answered only while a file "dev-commands" is in the data folder (no
    // package puts it there). A JSON object in, a JSON answer out:
    // {"cmd": ...} goes to the core as if from the UI; {"rooms": true}
    // lists the rooms; {"state": true} tells the open room and the
    // connection; {"ui": "open_room" (room_id) | "back" | "activate"}
    // goes to the UI.
    Q_SCRIPTABLE QString dev(const QString &json);
public:
    Q_INVOKABLE void closeWindow() { emit closeRequested(); }
    Q_INVOKABLE QString takePendingRoom() { const QString r = m_pendingRoom; m_pendingRoom.clear(); return r; }
    Q_INVOKABLE QString takePendingCall() { const QString r = m_pendingCall; m_pendingCall.clear(); return r; }
    Q_INVOKABLE bool takePendingKnocks() { const bool r = m_pendingKnocks; m_pendingKnocks = false; return r; }
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
    // a tab's ("rooms", "people", "favs", "all", "spaces") own setting; one
    // never set starts from the setting all tabs shared before
    QVariant tabValue(const QString &kind, const QString &key, const QVariant &fallback) const;
    void setTabValue(const QString &key, const QVariant &value);
    void sortRooms(QVariantList &rooms, const QString &kind) const;
    QVariantList m_lastRooms;   // as the core sent them, for a new order
    // an avatar picture upright, at most 512 px, in the cache; its path and size
    QString prepareAvatar(const QString &path, const QString &cmd, QSize *size);
    QString m_roomFilter;
    // the stand-in: pass what was asked for to a new app process and leave
    bool handOver();

    bool m_ready = false;
    bool m_loggedIn = false;
    bool m_loggingIn = false;
    QString m_userId;
    QString m_homeserver;
    QString m_syncState = QStringLiteral("idle");
    QString m_logoutReason;
    QString m_keptUser;
    QString m_verificationState = QStringLiteral("unknown");
    QString m_recoveryState = QStringLiteral("unknown");
    QString m_openRoomId;
    QString m_pendingRoom;
    QString m_pendingCall;
    QString m_currentTab = QStringLiteral("rooms");
    // the tabs' settings as read (QSettings per room update was a file
    // check each time); filled on first use, changed in setTabValue
    mutable QHash<QString, QVariant> m_tabCache;
    bool m_pendingKnocks = false;
    // per room: { room_name, is_space, requests }
    QMap<QString, QVariantMap> m_knocks;
    QSet<QString> m_knocksNotified;
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
    RoomModel *m_all;
    RoomModel *m_spaces;
    RoomModel *m_targets;
    QHash<QString, QString> m_emotePaths;
    QSet<QString> m_emotesAsked;
    TimelineModel *m_timeline;
    TimelineModel *m_thread;
    QString m_threadRoot;
    int m_threadsSeenVersion = 0;
    QElapsedTimer m_since;   // start-up timing in the journal
    bool m_firstRooms = true;
};

#endif
