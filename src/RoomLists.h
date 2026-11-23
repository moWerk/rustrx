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

#include <QCollator>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QPair>
#include <QScopedPointer>
#include <QVector>
#include <functional>
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
    // settings: Rustrx's, which outlives every use here; prefix: where this
    // account's own keys go in it ("" for the first account, as always)
    RoomLists(QSettings *settings, const QString &prefix = QString(), QObject *parent = nullptr);

    RoomModel *rooms() const { return m_rooms; }
    RoomModel *people() const { return m_people; }
    RoomModel *favourites() const { return m_favourites; }
    RoomModel *all() const { return m_all; }
    RoomModel *spaces() const { return m_spaces; }
    RoomModel *targets() const;
    int unreadTotal() const { return m_unreadTotal; }
    int unreadChats() const { return m_unreadChats; }
    const QHash<QString, QVariantMap> &roomsById() const { return m_roomsById; }
    QVariantMap roomInfo(const QString &roomId) const;
    QVariantMap firstUnread() const;
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

    // the whole list at once (the merged view, tests): rows equal to the
    // last ones are kept as those; full: every tab sorted again, else only
    // the rows that changed are placed anew when the rest kept their order
    void setRooms(const QVariantList &rooms, bool full = false);
    // the same with each row's key beside it (mergeAccounts' uids)
    void setRooms(const QVariantList &rooms, const QVector<QString> &keys, bool full = false);
    // the core's `rooms` event: the order and only the new or changed rows
    // (docs/PROTOCOL.md, "Room list events"); RoomsResync: not applied, the
    // core is to send the whole list (`rooms_resync`); RoomsDropped: an old
    // event, or one after the resync was asked already
    enum Applied { RoomsApplied, RoomsDropped, RoomsResync };
    Applied applyRooms(const QVariantMap &ev);
    // the rows the last applied event changed, for perf: lines
    int lastChanged() const { return m_lastChanged; }
    // of the last update: the time in the handlers of its notification,
    // invitation and unread signals, and whether every tab was sorted anew
    qint64 lastSignalsUs() const { return m_lastSignalsUs; }
    bool lastFull() const { return m_lastFull; }
    // for the merged view (all accounts in one list): sorts, filters and
    // counts only; no notifications, no invitation bookkeeping, no settings
    // written but the tabs' own
    void setDisplayOnly(bool on) { m_displayOnly = on; }
    // every account's rooms in one list for the merged view, each tagged
    // with its account and a uid ("<account>/<room_id>"), in the core's
    // order (invitations first, then the last activity, then the uid)
    // cache: the merged rows of the last call per account and room id,
    // with their source and what the order reads; a source passed on
    // unchanged gives the same merged row; uids: each row's, beside it
    struct MergeRow {
        QVariant source, merged;
        QString uid;
        bool invited = false;
        qint64 ts = 0;
        quint32 seen = 0;
    };
    struct MergeCache {
        QHash<int, QHash<QString, MergeRow>> accounts;
        quint32 pass = 0;
    };
    static QVariantList mergeAccounts(const QList<QPair<int, QVariantList>> &accounts, MergeCache *cache = nullptr,
                                      QVector<QString> *uids = nullptr);
    // the last rooms as the lists hold them (unchanged rows as before)
    const QVariantList &lastRooms() const { return m_lastRooms; }
    // the core's `knock_requests` event
    void setKnockRequests(const QVariantMap &ev);
    // logged out: the lists, requests and banners go (the unread totals in
    // clearUnread, which Rustrx calls after its own reset)
    void clear();
    void clearUnread();
    // shown again after another account: the tabs' settings, which all
    // accounts share, may have changed meanwhile
    void reloadSettings();

signals:
    void settingsChanged();
    void roomFilterChanged();
    void unreadChanged();
    void knockRequestsChanged();
    void newKnock(const QString &roomId, const QString &roomName, bool isSpace,
                  const QString &name, const QString &reason);
    void newMessages(const QString &roomId, const QString &roomName, int count);
    void roomRead(const QString &roomId);
    // known: the inviter shares a room with us
    void invited(const QString &roomId, const QString &roomName, const QString &inviter, bool direct, bool known);
    void inviteGone(const QString &roomId);

private:
    enum Tab { TabRooms, TabPeople, TabFavs, TabAll, TabSpaces, TabCount };
    // what the lists read of a row, once each time it comes or changes
    struct Row {
        QVariant row;
        QString id;
        int pos = -1;           // in the order
        quint32 seen = 0;       // the last update that had it
        bool invited = false, fav = false, low = false, dm = false, space = false, muted = false;
        bool shown = false;     // matches the filter
        int badge = 0, notifications = 0;
        QString name;
        uint tabs = 0;          // the tabs that hold it (1 << Tab)
    };
    // what a tab's order looks at, read from each row once instead of per comparison
    struct SortKey {
        bool invited;
        bool fav;
        bool low;
        int unread;
        QString name;
    };
    struct TabOrder {
        bool favsFirst = false, lowAtEnd = true, byUnread = false, byName = false, hideMuted = false;
    };
    // a tab's rows as its model has them: each one's key and sort key
    struct TabRows {
        QVector<QString> keys;
        QVector<SortKey> sort;
    };
    void readRow(Row &r) const;
    uint tabsOf(const Row &r) const;
    SortKey sortKey(const Row &r, const TabOrder &o) const;
    bool keyLess(const SortKey &a, const SortKey &b, const TabOrder &o) const;
    TabOrder tabOrder(const QString &kind) const;
    RoomModel *tabModel(int tab) const;
    // a new list in m_lastRooms / m_lastKeys, m_rows up to date: every tab
    // anew, or only the touched rows (old: each changed row as it was, an
    // empty id for a new one; gone: the rows no longer in the list)
    void distributeRooms();
    void placeTouched(const QVector<QString> &touched, const QVector<Row> &old, const QVector<Row> &gone);
    // the list as given, its order checked against the last: a full
    // distribute, or the touched rows placed
    void takeRows(const QVariantList &kept, const QVector<QString> &keys, const QVector<QString> &touched,
                  const QVector<Row> &old, bool inOrder, bool full);
    void notifyInvites();
    void updateTargets();
    void flushSignals();
    // a tab's ("rooms", "people", "favs", "all", "spaces") own setting; one
    // never set starts from the setting all tabs shared before
    QVariant tabValue(const QString &kind, const QString &key, const QVariant &fallback) const;
    void setTabValue(const QString &key, const QVariant &value);
    void sortRooms(QVariantList &rooms, const QString &kind) const;

    QSettings *m_settings;
    bool m_displayOnly = false;
    // this account's own keys: the invitations already notified
    QString m_invitesKey;
    QVariantList m_lastRooms;   // as the core sent them, for a new order
    QVector<QString> m_lastKeys; // their row keys
    // the same rows by row key, for the next event's compare
    QHash<QString, Row> m_rows;
    quint32 m_pass = 0;
    // the tabs as last placed (false: the next update sorts every tab)
    bool m_placed = false;
    TabOrder m_order[TabCount];
    TabRows m_tab[TabCount];
    mutable QScopedPointer<QCollator> m_collator;
    // the signals of an update, sent once every list holds it
    QVector<std::function<void()>> m_pendingSignals;
    qint64 m_lastSignalsUs = 0;
    bool m_lastFull = false;
    // the core's room list stream: the last event applied, and whether a
    // resync was asked since (once, until a full event comes)
    qint64 m_stream = 0;
    qint64 m_seq = 0;
    // logged out: the streams before are over (a late event is dropped)
    qint64 m_streamFloor = 0;
    bool m_resyncAsked = false;
    int m_lastChanged = 0;
    // the share targets, made and sorted only when read while no view shows them
    mutable bool m_targetsDirty = false;
    QString m_roomFilter;
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
