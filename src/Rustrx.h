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
#include <QHash>
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
class CallState;

// One account: its core (librustrx's number for it), its folder and the
// state its events bring. Every account's core runs; the app shows one.
struct Account {
    // rustrx_start's number for its core
    int core = -1;
    // its folder, and its name for the notifications and the settings:
    // 0 the data folder itself, n its accounts/n
    int key = 0;
    QString dir;
    // the tabs' room lists, unread totals, requests to join
    RoomLists *lists = nullptr;
    // the core's last `rooms`, for the merged view
    QVariantList lastRooms;
    // its push registration (UnifiedPush), in its own folder
    UnifiedPush *push = nullptr;
    // pushes that came before its session was back (the woken process)
    QList<QVariantMap> pendingPushes;
    bool ready = false;
    bool loggedIn = false;
    bool loggingIn = false;
    // logged in from the login page in this run (not a restored session)
    bool newLogin = false;
    // the classic /sync, not sliding sync (logged_in's sliding_sync)
    bool classicSync = false;
    // what the server offers for QR linking (server_features)
    bool deviceLogin = false;
    bool qrRendezvous = false;
    bool restoreFailed = false;
    bool restoring = false;
    // no room list of this login yet
    bool firstRooms = true;
    QString userId;
    QString homeserver;
    QString syncState = QStringLiteral("idle");
    QString logoutReason;
    QString keptUser;
    QString verificationState = QStringLiteral("unknown");
    QString recoveryState = QStringLiteral("unknown");
    QString storeState;
    // the own name and picture (the account button and list)
    QString displayName;
    QString avatarPath;
    qint64 avatarVersion = 0;
    // unsent text per room, as the store has it
    QHash<QString, QString> drafts;
};

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
    // logged in from the login page in this run (not a restored session):
    // a new device of the account, which the UI offers to verify once
    Q_PROPERTY(bool newLogin READ newLogin NOTIFY stateChanged)
    // the shown account's server lets new devices in by QR code (Link a new
    // device), and keeps the rendezvous for a code shown here
    // the shown account syncs the classic way (/sync): its server has no
    // sliding sync, or it is switched off for it (setClassicSync)
    Q_PROPERTY(bool classicSync READ classicSync NOTIFY stateChanged)
    Q_PROPERTY(bool deviceLogin READ deviceLogin NOTIFY stateChanged)
    Q_PROPERTY(bool qrRendezvous READ qrRendezvous NOTIFY stateChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString homeserver READ homeserver NOTIFY stateChanged)
    Q_PROPERTY(QString logoutReason READ logoutReason NOTIFY stateChanged)
    // the user whose store was kept when the server ended the session
    Q_PROPERTY(QString keptUser READ keptUser NOTIFY stateChanged)
    // the saved session could not be opened: the store's key did not come
    // (Secrets locked, its prompt not answered); retryRestore() asks again
    Q_PROPERTY(bool restoreFailed READ restoreFailed NOTIFY stateChanged)
    Q_PROPERTY(bool restoring READ restoring NOTIFY stateChanged)
    // the call page is dark at the ear (see CallState)
    Q_PROPERTY(bool callDark READ callDark NOTIFY callDarkChanged)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged)
    Q_PROPERTY(QString verificationState READ verificationState NOTIFY encryptionChanged)
    Q_PROPERTY(QString recoveryState READ recoveryState NOTIFY encryptionChanged)
    // how the local store is kept: encrypted, unencrypted, no_secrets; "" before login
    Q_PROPERTY(QString storeState READ storeState NOTIFY encryptionChanged)
    Q_PROPERTY(QString openRoomId READ openRoomId NOTIFY openRoomChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadChanged)
    Q_PROPERTY(int unreadChats READ unreadChats NOTIFY unreadChanged)
    // the shown account's lists: they change with the account
    Q_PROPERTY(RoomModel *rooms READ rooms NOTIFY shownAccountChanged)
    Q_PROPERTY(RoomModel *people READ people NOTIFY shownAccountChanged)
    Q_PROPERTY(RoomModel *favourites READ favourites NOTIFY shownAccountChanged)
    // everything in one list, for those who do not want rooms and people apart
    Q_PROPERTY(RoomModel *all READ all NOTIFY shownAccountChanged)
    // the joined spaces (invitations to spaces stay with the rooms' invitations)
    Q_PROPERTY(RoomModel *spaces READ spaces NOTIFY shownAccountChanged)
    // every joined room and chat, whatever the filter: what sharing offers
    Q_PROPERTY(RoomModel *targets READ targets NOTIFY shownAccountChanged)
    // every account with a login (or one the server ended), in the order
    // they were added: { key, user_id, name, avatar, avatar_v, logged_in,
    // shown, unread }
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    // the shown account's key (Account::key)
    Q_PROPERTY(int accountKey READ accountKey NOTIFY shownAccountChanged)
    // unread chats in the accounts not shown (the account button's dot)
    Q_PROPERTY(int otherUnread READ otherUnread NOTIFY accountsChanged)
    // all accounts in one list: the tabs show every account's rooms, each
    // opened in its own account (off by default); colourAccounts marks the
    // rows with their account's colour while it is on (on by default)
    Q_PROPERTY(bool mergedView READ mergedView WRITE setMergedView NOTIFY mergedViewChanged)
    Q_PROPERTY(bool colourAccounts READ colourAccounts WRITE setColourAccounts NOTIFY mergedViewChanged)
    Q_PROPERTY(TimelineModel *timeline READ timeline CONSTANT)
    // push through UnifiedPush (Foghorn), the stand-in's lighter successor;
    // the shown account's (each account registers its own)
    Q_PROPERTY(UnifiedPush *push READ push NOTIFY shownAccountChanged)
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

    bool ready() const { return m_cur->ready; }
    bool roomsLoaded() const { return !m_cur->firstRooms; }
    bool loggedIn() const { return m_cur->loggedIn; }
    bool loggingIn() const { return m_cur->loggingIn; }
    bool newLogin() const { return m_cur->newLogin; }
    bool deviceLogin() const { return m_cur->deviceLogin; }
    bool classicSync() const { return m_cur->classicSync; }
    // the shown account to the classic sync (or back to the server's
    // choice); its session restarts
    Q_INVOKABLE void setClassicSync(bool classic);
    bool qrRendezvous() const { return m_cur->qrRendezvous; }
    QString userId() const { return m_cur->userId; }
    QString homeserver() const { return m_cur->homeserver; }
    QString syncState() const { return m_cur->syncState; }
    QString logoutReason() const { return m_cur->logoutReason; }
    QString keptUser() const { return m_cur->keptUser; }
    bool restoreFailed() const { return m_cur->restoreFailed; }
    bool restoring() const { return m_cur->restoring; }
    QVariantList accounts() const;
    int accountKey() const { return m_cur->key; }
    int otherUnread() const;
    bool mergedView() const { return m_mergedView; }
    void setMergedView(bool on);
    bool colourAccounts() const { return m_colourAccounts; }
    void setColourAccounts(bool on);
    // show another account (its key); not away from a running call's account
    Q_INVOKABLE bool switchAccount(int key);
    // one more account: the login page for it (an account without a login
    // is used again before a new folder is made)
    Q_INVOKABLE void addAccount();
    // a command for that account (notifications act on theirs)
    Q_INVOKABLE void commandTo(int key, const QVariantMap &cmd);
    // a room of that account, as roomInfo has it for the shown one
    Q_INVOKABLE QVariantMap roomInfoIn(int key, const QString &roomId) const;
    Q_INVOKABLE bool accountLoggedIn(int key) const;
    // the account the classic call rings or runs on
    Q_INVOKABLE int voipAccount() const { return m_voipKey; }
    // a classic call from the shown account: it is that account's call
    // from its first event on
    Q_INVOKABLE void placeCall(const QString &roomId, const QString &invitee, const QString &name);
    bool callDark() const;
    Q_INVOKABLE void retryRestore();
    // the proximity sensor during a call (see CallState)
    Q_INVOKABLE void setCallNear(bool near);
    QString verificationState() const { return m_cur->verificationState; }
    QString recoveryState() const { return m_cur->recoveryState; }
    QString storeState() const { return m_cur->storeState; }
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
    UnifiedPush *push() const { return m_cur->push; }
    // the first account's, which exports the connector
    UnifiedPush *firstPush() const { return m_push; }
    // push on or off, for every account (Settings, "when Rustrx is closed")
    Q_INVOKABLE void setPushEnabled(bool on);
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
    // a picture of the camera page (grabToImage's result): a QR code in it
    // goes to the shown account (qr_scanned) and true comes back
    Q_INVOKABLE bool readQr(QObject *grab);
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
    // the room a notification asked for; its account is shown first
    Q_INVOKABLE QString takePendingRoom();
    Q_INVOKABLE QString takePendingCall() { return m_handover.takePendingCall(); }
    Q_INVOKABLE bool takePendingKnocks() { return m_handover.takePendingKnocks(); }
    // a link or a share that arrived before QML was loaded
    Q_INVOKABLE QString takePendingUri() { return m_handover.takePendingUri(); }
    Q_INVOKABLE QVariantMap takePendingShare() { return m_handover.takePendingShare(); }

signals:
    void callDarkChanged();
    // a dev command for the UI (see dev())
    void devUi(const QVariantMap &request);
    void stateChanged();
    // the image packs offered in a room (custom emoji and stickers)
    void imagePacks(const QString &roomId, const QVariantList &packs);
    void emoteReady(const QString &mxc, const QString &path);
    void emoteAdded(const QString &shortcode);
    // someone calls (ring) or a group call starts (not ring); expiresMs: how
    // long it may ring
    // account: the key of the account it rings on
    void callRing(const QString &roomId, const QString &roomName, const QString &senderName,
                  bool ring, const QString &eventId, int expiresMs, int account);
    // answered or declined: the ringing notification goes
    void callAnswered(const QString &roomId);
    // starting a call opened the room's calls to everyone (permissions)
    void callsAllowed(const QString &roomId);
    void knockRequestsChanged();
    // a pushed message, fetched (the woken process shows it); account: whose
    void pushNotification(const QString &roomId, const QString &eventId, const QString &roomName, const QString &senderName,
                          const QString &body, bool isDm, bool noisy, int account);
    // a push came, for any account (the woken process stays a while)
    void pushed();
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
                  const QString &name, const QString &reason, int account);
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
    // count: the room's notifications now; account: whose room (every
    // account's lists notify, the shown one's and the others')
    void newMessages(const QString &roomId, const QString &roomName, int count, int account);
    void markReadRequested(const QString &roomId, int account);
    // the users this account ignores: {user_id, name}
    void ignoredUsers(const QVariantList &users);
    // one person for their page (user_info)
    void userInfo(const QVariantMap &info);
    // a QR login's progress: state qr (width, modules), scanned, waiting
    // (user_code), syncing, done
    void qrLogin(const QVariantMap &progress);
    // letting a new device in by QR code: state qr (width, modules),
    // scanned, check (check_code), consent, syncing, done, refused
    // (reason), error (message)
    void qrGrant(const QVariantMap &progress);
    void replyRequested(const QString &roomId, const QString &text, int account);
    void reactRequested(const QString &roomId, const QString &eventId, const QString &key, int account);
    // a room's notifications went to none (read here or in another client)
    void roomRead(const QString &roomId, int account);
    // an invitation not notified before (also one that came while closed)
    void invited(const QString &roomId, const QString &roomName, const QString &inviter, bool direct, bool known, int account);
    // an invitation that is no longer one (joined, declined, elsewhere)
    void inviteGone(const QString &roomId, int account);
    // the accounts or their unread counts changed
    void accountsChanged();
    // another account is shown: its lists replace the others
    void shownAccountChanged();
    void mergedViewChanged();
    // a notification or a logout wants that account shown: the window
    // closes the other account's pages first, then calls switchAccount
    void showAccountRequested(int key);
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
    // the same for a room of the account `account` (Account::key, as text:
    // a notification's arguments come as they were written); the app shows
    // that account first
    Q_SCRIPTABLE void openAccountRoom(const QString &account, const QString &roomId);
    // a second launch: true when this is the stand-in, which leaves at once,
    // and the caller waits for the name to go and becomes the app
    Q_SCRIPTABLE bool activate();
    // the stand-in's notification: open the app
    Q_SCRIPTABLE void open();
    // an incoming call's notification: Answer opens the room and joins
    // the call (the room starts it once it is open: takePendingCall)
    Q_SCRIPTABLE void answerCall(const QString &roomId);
    Q_SCRIPTABLE void answerAccountCall(const QString &account, const QString &roomId);
    // the requests' notification: open the app on the list of requests
    Q_SCRIPTABLE void openKnocks();
    // a room notification's "Mark as read" (Service.qml sends the receipt,
    // as the privacy setting has it, once logged in)
    Q_SCRIPTABLE void markRoomRead(const QString &roomId);
    Q_SCRIPTABLE void markAccountRoomRead(const QString &account, const QString &roomId);
    // a room notification's reply field (lipstick adds the typed text as
    // the last argument) and its emoji buttons
    Q_SCRIPTABLE void replyFromNotification(const QString &roomId, const QString &text);
    Q_SCRIPTABLE void reactFromNotification(const QString &roomId, const QString &eventId, const QString &key);
    Q_SCRIPTABLE void replyFromAccountNotification(const QString &account, const QString &roomId, const QString &text);
    Q_SCRIPTABLE void reactFromAccountNotification(const QString &account, const QString &roomId,
                                                   const QString &eventId, const QString &key);
    // Decline: the caller's clients are told (MSC4310)
    Q_SCRIPTABLE void declineCall(const QString &roomId, const QString &eventId);
    Q_SCRIPTABLE void declineAccountCall(const QString &account, const QString &roomId, const QString &eventId);
    // the stand-in's notification: Quit (after QML has closed that notification)
    Q_SCRIPTABLE void quit();
    // D-Bus net.mowerk.harbour_rustrx.openUrl, the desktop file's handler for
    // matrix: links; the launcher icon calls it too, without a link
    Q_SCRIPTABLE void openUrl(const QStringList &urls);
    // D-Bus net.mowerk.harbour_rustrx.dev, for tests from a shell (ssh),
    // answered only while a file "dev-commands" is in the data folder (no
    // package puts it there). A JSON object in, a JSON answer out; only
    // the requests the test tooling sends, all else refused (DevHook.h):
    // {"cmd": "call_start" | "refresh_rooms"} goes to the core as if from
    // the UI; {"rooms": true} lists the rooms; {"state": true} tells the
    // open room and the connection; {"ui": "open_room" (room_id) | "back"
    // | "activate" | "settings" (tab)} goes to the UI; {"voip": ...}
    // drives a call; {"quit": true} quits.
    Q_SCRIPTABLE QString dev(const QString &json);

private slots:
    void handleEvent(const QVariantMap &ev);

private:
    static void eventCallback(void *ctx, const char *json);
    // a core for the account in `dir` (key: see Account), its lists wired
    Account *startAccount(int key, const QString &dir);
    Account *account(int key) const;
    // what every account's events change; the UI hears of it only for the shown one
    void handleAccountEvent(Account *a, const QString &type, const QVariantMap &ev);
    // the stand-in leaves when no account is logged in any more
    void leaveIfNobodyLoggedIn();
    // asks the window to show that account (nothing in a process without one)
    void requestShow(int key);
    // everything QML reads changed: after a switch
    void emitAllChanged();
    QString m_dataDir;
    QList<Account *> m_accounts;
    // the account shown; never null
    Account *m_cur = nullptr;
    // the merged view's list (display only) and its switches
    RoomLists *m_merged = nullptr;
    bool m_mergedView = false;
    bool m_colourAccounts = true;
    // the lists the tabs show: the merged one while it is on
    RoomLists *shownLists() const;
    // the merged list fed anew from every logged-in account's rooms
    void refreshMerged();
    // the account the classic call is on (its events and commands)
    int m_voipKey = 0;
    // a call handed over by the stand-in, until its account is logged in
    QVariantMap m_pendingVoip;
    // handleEvent, looked up once for the queued calls from the core's threads
    QMetaMethod m_handleEvent;

    bool handStoreKeyOn();
    QString m_openRoomId;
    // before the lists and m_media, which use it
    QSettings m_settings;
    // the stand-in's and push-woken process's requests, the handover file
    Handover m_handover;
    // the first account's push
    UnifiedPush *m_push;
    Voip *m_voip;
    CallRoute *m_callRoute;
    CallState *m_callState;
    // sending files and pictures, avatars, media downloads, emote pictures
    Media *m_media;
    TimelineModel *m_timeline;
    TimelineModel *m_thread;
    QString m_threadRoot;
    QElapsedTimer m_since;   // start-up timing in the journal
};

#endif
