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

    Q_INVOKABLE void command(const QVariantMap &cmd);
    Q_INVOKABLE void loginPassword(const QString &user, const QString &password, const QString &homeserver);
    Q_INVOKABLE void loginSso(const QString &user, const QString &homeserver, const QString &idp);
    Q_INVOKABLE void probeServer(const QString &user, const QString &homeserver);
    Q_INVOKABLE void openRoom(const QString &roomId);
    Q_INVOKABLE void closeRoom();
    Q_INVOKABLE void sendText(const QString &roomId, const QString &body, const QString &replyTo);
    Q_INVOKABLE void sendFile(const QString &roomId, const QString &path);
    Q_INVOKABLE void getMedia(const QString &roomId, const QString &eventId, bool thumb);
    // a copy in Downloads, where other apps may read it; answered by mediaExported
    Q_INVOKABLE void exportMedia(const QString &roomId, const QString &eventId);
    Q_INVOKABLE QVariantMap roomInfo(const QString &roomId) const;
    Q_INVOKABLE QString formatTime(const QVariant &ms) const;
    Q_INVOKABLE QString formatDate(const QVariant &ms) const;
    // unsent text per room, kept across pages and restarts
    // the newest JPEG the stock camera saved after `sinceMs`, or an empty string
    Q_INVOKABLE QString newestPhotoSince(qint64 sinceMs) const;
    Q_INVOKABLE QString draft(const QString &roomId) const;
    Q_INVOKABLE void setDraft(const QString &roomId, const QString &text);

signals:
    void stateChanged();
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

private slots:
    void handleEvent(const QVariantMap &ev);

private:
    static void eventCallback(void *ctx, const char *json);
    void distributeRooms(const QVariantList &rooms);

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
