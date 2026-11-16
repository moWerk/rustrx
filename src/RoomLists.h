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
#ifndef ROOMLISTS_H
#define ROOMLISTS_H

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class QSettings;
class RoomModel;

// The room lists of the tabs, made from the core's `rooms` event: each tab's
// order and switches, the filter, the unread totals, the notifications and
// invitation banners they lead to, and the requests to join. Rustrx owns it
// and shows its properties and signals as its own.
class RoomLists : public QObject
{
    Q_OBJECT

public:
    // settings: Rustrx's, which outlives every use here
    RoomLists(QSettings *settings, QObject *parent = nullptr);

    RoomModel *rooms() const { return m_rooms; }
    RoomModel *people() const { return m_people; }
    RoomModel *favourites() const { return m_favourites; }
    RoomModel *all() const { return m_all; }
    RoomModel *spaces() const { return m_spaces; }
    RoomModel *targets() const { return m_targets; }
    int unreadTotal() const { return m_unreadTotal; }
    int unreadChats() const { return m_unreadChats; }
    const QHash<QString, QVariantMap> &roomsById() const { return m_roomsById; }
    QVariantMap roomInfo(const QString &roomId) const;
    QString firstUnreadRoom() const;
    QVariantList knockRequests() const;

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
    QString roomFilter() const { return m_roomFilter; }
    void setRoomFilter(const QString &text);

    // the open room gains no notification (kept in step with Rustrx's)
    void setOpenRoomId(const QString &roomId) { m_openRoomId = roomId; }
    // the core's `rooms` event
    void setRooms(const QVariantList &rooms);
    // the core's `knock_requests` event
    void setKnockRequests(const QVariantMap &ev);
    // logged out: the lists, requests and banners go (the unread totals in
    // clearUnread, which Rustrx calls after its own reset)
    void clear();
    void clearUnread();

signals:
    void settingsChanged();
    void roomFilterChanged();
    void unreadChanged();
    void knockRequestsChanged();
    void newKnock(const QString &roomId, const QString &roomName, bool isSpace,
                  const QString &name, const QString &reason);
    void newMessages(const QString &roomId, const QString &roomName, int count);
    void invited(const QString &roomId, const QString &roomName, const QString &inviter, bool direct);
    void inviteGone(const QString &roomId);

private:
    void distributeRooms(const QVariantList &rooms);
    // a tab's ("rooms", "people", "favs", "all", "spaces") own setting; one
    // never set starts from the setting all tabs shared before
    QVariant tabValue(const QString &kind, const QString &key, const QVariant &fallback) const;
    void setTabValue(const QString &key, const QVariant &value);
    void sortRooms(QVariantList &rooms, const QString &kind) const;

    QSettings *m_settings;
    QVariantList m_lastRooms;   // as the core sent them, for a new order
    QString m_roomFilter;
    QString m_openRoomId;
    QString m_currentTab = QStringLiteral("rooms");
    // the tabs' settings as read (QSettings per room update was a file
    // check each time); filled on first use, changed in setTabValue
    mutable QHash<QString, QVariant> m_tabCache;
    // the invitations already notified, as in the settings
    QStringList m_invitesNotified;
    // per room: { room_name, is_space, requests }
    QMap<QString, QVariantMap> m_knocks;
    QSet<QString> m_knocksNotified;
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
};

#endif
