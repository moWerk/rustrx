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

#include "Handover.h"

#include <QElapsedTimer>
#include <QMetaMethod>
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

class Media;
class RoomLists;
class RoomModel;
class TimelineModel;
class UnifiedPush;
class Voip;
class CallRoute;

// The bridge to librustrx: commands go out as JSON, events come back on
// tokio threads and are re-posted onto the GUI thread before anything here
// changes. One instance per process, exposed to QML as `rustrx`.
class Rustrx : public QObject
{
    Q_OBJECT
    // Keys::kInterface; Q_CLASSINFO takes a literal only
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
    // how the local store is kept: encrypted, unencrypted, no_secrets; "" before login
    Q_PROPERTY(QString storeState READ storeState NOTIFY encryptionChanged)
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
    // push through UnifiedPush (Foghorn), the stand-in's lighter successor
    Q_PROPERTY(UnifiedPush *push READ push CONSTANT)
    // classic 1:1 calls (m.call.*, WebRTC through GStreamer)
    Q_PROPERTY(Voip *voip READ voip CONSTANT)
    // the open thread's messages (root and replies), beside the room's
    Q_PROPERTY(TimelineModel *thread READ thread CONSTANT)
    // changes whenever a thread is marked seen (bindings read it to update)
    // open requests to join the rooms and spaces we may invite to, each
    // { room_id, room_name, is_space, event_id, user_id, name, reason, ts, seen }
    Q_PROPERTY(QVariantList knockRequests READ knockRequests NOTIFY knockRequestsChanged)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(bool downscaleImages READ downscaleImages WRITE setDownscaleImages NOTIFY settingsChanged)
    Q_PROPERTY(int metadataLevel READ metadataLevel WRITE setMetadataLevel NOTIFY settingsChanged)
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
    // calls through the server's relay only (Settings > Privacy)
    Q_PROPERTY(bool callsRelayOnly READ callsRelayOnly WRITE setCallsRelayOnly NOTIFY settingsChanged)
    // the lists show only rooms whose name or address contains this (not kept)
    Q_PROPERTY(QString roomFilter READ roomFilter WRITE setRoomFilter NOTIFY roomFilterChanged)
    // notifications with the app closed: a stand-in, started with the session
    // and when the app is closed, wakes every few minutes
    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY settingsChanged)
    // this process is the stand-in: no window, ever
    Q_PROPERTY(bool standIn READ standIn CONSTANT)
    // this app's D-Bus name, object and interface (Keys.h), for the
    // notification actions that call back into it
    Q_PROPERTY(QString busName READ busName CONSTANT)
    Q_PROPERTY(QString objectPath READ objectPath CONSTANT)
    Q_PROPERTY(QString interfaceName READ interfaceName CONSTANT)
    // the push connector's bus name: a notification's action sent there wakes
    // the window-less push process when no Rustrx runs (D-Bus activation),
    // instead of opening the app
    Q_PROPERTY(QString connectorName READ connectorName CONSTANT)

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
    QString storeState() const { return m_storeState; }
    QString openRoomId() const { return m_openRoomId; }
    int unreadTotal() const;
    int unreadChats() const;
    RoomModel *rooms() const;
    RoomModel *people() const;
    RoomModel *favourites() const;
    RoomModel *all() const;
    RoomModel *spaces() const;
    RoomModel *targets() const;
    TimelineModel *timeline() const { return m_timeline; }
    TimelineModel *thread() const { return m_thread; }
    // a thread beside the open room: its messages come into `thread`
    Q_INVOKABLE void openThread(const QString &roomId, const QString &root);
    Q_INVOKABLE void closeThread();
    QVariantList knockRequests() const;
    QString coreVersion() const;
    bool downscaleImages() const;
    void setDownscaleImages(bool on);
    int metadataLevel() const;
    void setMetadataLevel(int level);
    QString currentTab() const;
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
    bool callsRelayOnly() const;
    void setCallsRelayOnly(bool on);
    // the logs in one text file in Downloads, for the share sheet; its path
    Q_INVOKABLE QString shareableLog();
    // the debug log emptied and its shared copies removed
    Q_INVOKABLE int clearLog();
    QString roomFilter() const;
    void setRoomFilter(const QString &text);
    bool background() const;
    void setBackground(bool on);
    bool standIn() const { return m_handover.standIn(); }
    static QString busName();
    static QString objectPath();
    static QString interfaceName();
    static QString connectorName();
    void setStandIn(bool on) { m_handover.setStandIn(on); }
    // a process the distributor started for one push (no window, no sync)
    void setPushWake(bool on) { m_handover.setPushWake(on); }
    UnifiedPush *push() const { return m_push; }
    Voip *voip() const { return m_voip; }
    // the file the stand-in's unit looks for (in the sandbox's own config folder)
    static QString backgroundFlagPath() { return Handover::backgroundFlagPath(); }
    // a user unit: the stand-in (harbour-rustrx.service) or the app for it to
    // hand over to (harbour-rustrx-open.service)
    static bool startUnit(const QString &unit) { return Handover::startUnit(unit); }
    // what the stand-in passed on: the room, link or share to show
    void takeHandover() { m_handover.takeHandover(); }
    // called by the share adaptor
    void receiveShare(const QStringList &files, const QString &text);
    // a file:// URL as a local path; a path stays as it is
    static QString localPath(const QString &path);
    // the settings file, in the app's own config folder: the sandbox keeps
    // only that one, a file next to it is lost when the app ends
    static QString settingsFile();

    Q_INVOKABLE void command(const QVariantMap &cmd);
    Q_INVOKABLE void loginPassword(const QString &user, const QString &password, const QString &homeserver);
    Q_INVOKABLE void loginSso(const QString &user, const QString &homeserver, const QString &idp);
    // through the server's account service in the browser (OAuth); signup:
    // its page offers to make an account first
    Q_INVOKABLE void loginOauth(const QString &user, const QString &homeserver, bool signup);
    // with a QR code a logged-in device scans (qrLogin carries the progress)
    Q_INVOKABLE void loginQr(const QString &user, const QString &homeserver);
    Q_INVOKABLE void probeServer(const QString &user, const QString &homeserver);
    Q_INVOKABLE void openRoom(const QString &roomId);
    Q_INVOKABLE void closeRoom();
    // emotes: the custom emoji the text uses, shortcode to mxc address
    // threadRoot: into that thread
    // options: markdown (default true) and emote, as the slash commands set them
    Q_INVOKABLE void sendText(const QString &roomId, const QString &body, const QString &replyTo,
                              const QVariantMap &emotes = QVariantMap(), const QString &threadRoot = QString(),
                              const QVariantMap &options = QVariantMap());
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
    Q_INVOKABLE QString draft(const QString &roomId) const;
    Q_INVOKABLE void setDraft(const QString &roomId, const QString &text);
    Q_INVOKABLE void closeWindow() { emit closeRequested(); }
    Q_INVOKABLE QString takePendingRoom() { return m_handover.takePendingRoom(); }
    Q_INVOKABLE QString takePendingCall() { return m_handover.takePendingCall(); }
    Q_INVOKABLE bool takePendingKnocks() { return m_handover.takePendingKnocks(); }
    // a link or a share that arrived before QML was loaded
    Q_INVOKABLE QString takePendingUri() { return m_handover.takePendingUri(); }
    Q_INVOKABLE QVariantMap takePendingShare() { return m_handover.takePendingShare(); }

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
    // a pushed message, fetched (the woken process shows it)
    void pushNotification(const QString &roomId, const QString &eventId, const QString &roomName, const QString &senderName,
                          const QString &body, bool isDm, bool noisy);
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
    void roomSearchResults(const QVariantMap &results);
    // a room's permissions and settings (RoomInfoPage and the moderation pages)
    void roomSettings(const QVariantMap &settings);
    void roomUpgraded(const QString &roomId, const QString &replacement);
    void reported(const QVariantMap &what);
    void aliasAdded(const QString &roomId, const QString &alias);
    void spaceBanned(const QVariantMap &result);
    void redactProgress(const QVariantMap &progress);
    void profile(const QVariantMap &profile);
    void dmReady(const QString &roomId, const QString &userId);
    void joined(const QString &roomId);
    void paginated(const QString &roomId, bool atStart);
    // a room's draft from the store, as the room opens
    void draftLoaded(const QString &roomId, const QString &text);
    void mediaExported(const QString &eventId, const QString &path);
    // count: the room's notifications now
    void newMessages(const QString &roomId, const QString &roomName, int count);
    void markReadRequested(const QString &roomId);
    // the users this account ignores: {user_id, name}
    void ignoredUsers(const QVariantList &users);
    // one person for their page (user_info)
    void userInfo(const QVariantMap &info);
    // a QR login's progress: state qr (width, modules), scanned, waiting
    // (user_code), syncing, done
    void qrLogin(const QVariantMap &progress);
    void replyRequested(const QString &roomId, const QString &text);
    void reactRequested(const QString &roomId, const QString &eventId, const QString &key);
    // a room's notifications went to none (read here or in another client)
    void roomRead(const QString &roomId);
    // an invitation not notified before (also one that came while closed)
    void invited(const QString &roomId, const QString &roomName, const QString &inviter, bool direct, bool known);
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
    Q_SCRIPTABLE void openRoomFromNotification(const QString &roomId);
    // a second launch: true when this is the stand-in, which leaves at once,
    // and the caller waits for the name to go and becomes the app
    Q_SCRIPTABLE bool activate();
    // the stand-in's notification: open the app
    Q_SCRIPTABLE void open();
    // an incoming call's notification: Answer opens the room and joins
    // the call (the room starts it once it is open: takePendingCall)
    Q_SCRIPTABLE void answerCall(const QString &roomId);
    // the requests' notification: open the app on the list of requests
    Q_SCRIPTABLE void openKnocks();
    // a room notification's "Mark as read" (Service.qml sends the receipt,
    // as the privacy setting has it, once logged in)
    Q_SCRIPTABLE void markRoomRead(const QString &roomId);
    // a room notification's reply field (lipstick adds the typed text as
    // the last argument) and its emoji buttons
    Q_SCRIPTABLE void replyFromNotification(const QString &roomId, const QString &text);
    Q_SCRIPTABLE void reactFromNotification(const QString &roomId, const QString &eventId, const QString &key);
    // Decline: the caller's clients are told (MSC4310)
    Q_SCRIPTABLE void declineCall(const QString &roomId, const QString &eventId);
    // the stand-in's notification: Quit (after QML has closed that notification)
    Q_SCRIPTABLE void quit();
    // D-Bus net.mowerk.harbour_rustrx.openUrl, the desktop file's handler for
    // matrix: links; the launcher icon calls it too, without a link
    Q_SCRIPTABLE void openUrl(const QStringList &urls);
    // D-Bus net.mowerk.harbour_rustrx.dev, for tests from a shell (ssh),
    // answered only while a file "dev-commands" is in the data folder (no
    // package puts it there). A JSON object in, a JSON answer out:
    // {"cmd": ...} goes to the core as if from the UI (not the commands
    // that hand over keys or the session, or end it); {"rooms": true}
    // lists the rooms; {"state": true} tells the open room and the
    // connection; {"ui": "open_room" (room_id) | "back" | "activate"}
    // goes to the UI.
    Q_SCRIPTABLE QString dev(const QString &json);

private slots:
    void handleEvent(const QVariantMap &ev);

private:
    static void eventCallback(void *ctx, const char *json);
    // handleEvent, looked up once for the queued calls from the core's threads
    QMetaMethod m_handleEvent;

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
    QString m_storeState;
    QString m_openRoomId;
    // before m_lists and m_media, which use it
    QSettings m_settings;
    QHash<QString, QString> m_drafts;
    // the stand-in's and push-woken process's requests, the handover file
    Handover m_handover;
    QList<QVariantMap> m_pendingPushes;
    UnifiedPush *m_push;
    Voip *m_voip;
    CallRoute *m_callRoute;
    // the tabs' room lists, their settings, unread totals and requests to join
    RoomLists *m_lists;
    // sending files and pictures, avatars, media downloads, emote pictures
    Media *m_media;
    TimelineModel *m_timeline;
    TimelineModel *m_thread;
    QString m_threadRoot;
    QElapsedTimer m_since;   // start-up timing in the journal
    bool m_firstRooms = true;
};

#endif
