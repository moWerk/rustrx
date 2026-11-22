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
#include "RoomLists.h"
#include "Keys.h"
#include "RoomModel.h"

#include <QCollator>
#include <QDebug>
#include <QScopedPointer>
#include <QSettings>
#include <QVector>
#include <algorithm>

static bool isInvited(const QVariantMap &room)
{
    return room.value("state").toString() == QLatin1String("invited");
}

// the number a room's badge shows: notifications, else unread messages
static int badgeCount(const QVariantMap &room)
{
    const int n = room.value("notifications").toInt();
    return n > 0 ? n : room.value("unread").toInt();
}

RoomLists::RoomLists(QSettings *settings, const QString &prefix, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_invitesKey(prefix + QLatin1String(Keys::kInvitesNotified))
    , m_rooms(new RoomModel(this))
    , m_people(new RoomModel(this))
    , m_favourites(new RoomModel(this))
    , m_all(new RoomModel(this))
    , m_spaces(new RoomModel(this))
    , m_targets(new RoomModel(this))
{
    m_invitesNotified = m_settings->value(m_invitesKey).toStringList();
}

void RoomLists::reloadSettings()
{
    m_tabCache.clear();
    distributeRooms(m_lastRooms);
}

void RoomLists::setRoomFilter(const QString &text)
{
    const QString t = text.trimmed();
    if (t == m_roomFilter)
        return;
    m_roomFilter = t;
    emit roomFilterChanged();
    distributeRooms(m_lastRooms);
}

// The core sends the rooms by activity. Invitations stay on top; then, if
// switched on, favourites first and low priority rooms last; within that,
// the most unread first or by name, else the activity order stays (the sort
// is stable).
QVariant RoomLists::tabValue(const QString &kind, const QString &key, const QVariant &fallback) const
{
    const QString own = QString::fromLatin1(Keys::kTabs) + QLatin1Char('/') + kind + QLatin1Char('/') + key;
    const auto cached = m_tabCache.constFind(own);
    if (cached != m_tabCache.constEnd())
        return *cached;
    const QVariant v = m_settings->contains(own) ? m_settings->value(own) : m_settings->value(key, fallback);
    m_tabCache.insert(own, v);
    return v;
}

void RoomLists::setTabValue(const QString &key, const QVariant &value)
{
    const QString own = QString::fromLatin1(Keys::kTabs) + QLatin1Char('/') + m_currentTab + QLatin1Char('/') + key;
    m_settings->setValue(own, value);
    m_tabCache.insert(own, value);
    emit settingsChanged();
    distributeRooms(m_lastRooms);
}

void RoomLists::setCurrentTab(const QString &kind)
{
    if (kind.isEmpty() || kind == m_currentTab)
        return;
    m_currentTab = kind;
    emit settingsChanged();
}

QString RoomLists::sortOrder() const
{
    return tabValue(m_currentTab, QLatin1String(Keys::kSortOrder), QLatin1String(Keys::kSortOrderDefault)).toString();
}

void RoomLists::setSortOrder(const QString &order)
{
    if (order != sortOrder())
        setTabValue(QLatin1String(Keys::kSortOrder), order);
}

bool RoomLists::favsOnTop() const
{
    return tabValue(m_currentTab, QLatin1String(Keys::kFavsOnTop), Keys::kFavsOnTopDefault).toBool();
}

void RoomLists::setFavsOnTop(bool on)
{
    if (on != favsOnTop())
        setTabValue(QLatin1String(Keys::kFavsOnTop), on);
}

bool RoomLists::hideMuted() const
{
    return tabValue(m_currentTab, QLatin1String(Keys::kHideMuted), Keys::kHideMutedDefault).toBool();
}

void RoomLists::setHideMuted(bool on)
{
    if (on != hideMuted())
        setTabValue(QLatin1String(Keys::kHideMuted), on);
}

bool RoomLists::lowLast() const
{
    return tabValue(m_currentTab, QLatin1String(Keys::kLowLast), Keys::kLowLastDefault).toBool();
}

void RoomLists::setLowLast(bool on)
{
    if (on != lowLast())
        setTabValue(QLatin1String(Keys::kLowLast), on);
}

void RoomLists::sortRooms(QVariantList &rooms, const QString &kind) const
{
    const QString order = tabValue(kind, QLatin1String(Keys::kSortOrder), QLatin1String(Keys::kSortOrderDefault)).toString();
    const bool favsFirst = tabValue(kind, QLatin1String(Keys::kFavsOnTop), Keys::kFavsOnTopDefault).toBool();
    const bool lowAtEnd = tabValue(kind, QLatin1String(Keys::kLowLast), Keys::kLowLastDefault).toBool();
    const bool byUnread = order == QLatin1String("unread");
    const bool byName = order == QLatin1String("name");
    // what the order looks at, read from each room once instead of per comparison
    struct Key {
        bool invited;
        bool fav;
        bool low;
        int unread;
        QString name;
    };
    QVector<Key> keys;
    keys.reserve(rooms.size());
    for (const QVariant &v : rooms) {
        const QVariantMap r = v.toMap();
        Key k;
        k.invited = isInvited(r);
        k.fav = favsFirst && r.value("is_fav").toBool();
        k.low = lowAtEnd && r.value("is_low").toBool();
        k.unread = byUnread ? badgeCount(r) : 0;
        if (byName)
            k.name = r.value("name").toString();
        keys.append(k);
    }
    QVector<int> index(rooms.size());
    for (int i = 0; i < index.size(); ++i)
        index[i] = i;
    // made only for the order by name (a QCollator per sort, used or not)
    QScopedPointer<QCollator> collator(byName ? new QCollator : nullptr);
    std::stable_sort(index.begin(), index.end(), [&](int ia, int ib) {
        const Key &a = keys.at(ia), &b = keys.at(ib);
        if (a.invited != b.invited)
            return a.invited;
        if (a.fav != b.fav)
            return a.fav;
        if (a.low != b.low)
            return b.low;
        // the most unread on top; equal counts keep the activity order
        if (byUnread)
            return a.unread > b.unread;
        if (byName)
            return collator->compare(a.name, b.name) < 0;
        return false;
    });
    QVariantList sorted;
    sorted.reserve(rooms.size());
    for (int i : index)
        sorted.append(rooms.at(i));
    rooms.swap(sorted);
}

QVariantList RoomLists::knockRequests() const
{
    QVariantList out;
    for (auto it = m_knocks.constBegin(); it != m_knocks.constEnd(); ++it) {
        for (const QVariant &v : it.value().value("requests").toList()) {
            QVariantMap r = v.toMap();
            r.insert("room_id", it.key());
            r.insert("room_name", it.value().value("room_name"));
            r.insert("is_space", it.value().value("is_space"));
            out << r;
        }
    }
    return out;
}

void RoomLists::setKnockRequests(const QVariantMap &ev)
{
    const QString roomId = ev.value("room_id").toString();
    const QVariantList requests = ev.value("requests").toList();
    if (requests.isEmpty())
        m_knocks.remove(roomId);
    else
        m_knocks.insert(roomId, ev);
    for (const QVariant &v : requests) {
        const QVariantMap r = v.toMap();
        const QString id = r.value("event_id").toString();
        if (r.value("seen").toBool() || m_knocksNotified.contains(id))
            continue;
        m_knocksNotified.insert(id);
        emit newKnock(roomId, ev.value("room_name").toString(), ev.value("is_space").toBool(),
                      r.value("name").toString(), r.value("reason").toString());
    }
    emit knockRequestsChanged();
}

RoomModel *RoomLists::targets() const
{
    // sorted and handed over when something reads it (the room pickers)
    if (m_targetsDirty) {
        m_targetsDirty = false;
        QVariantList rows = m_pendingTargets;
        m_pendingTargets.clear();
        sortRooms(rows, QStringLiteral("rooms"));
        m_targets->setRooms(rows);
    }
    return m_targets;
}

QVariantMap RoomLists::roomInfo(const QString &roomId) const
{
    return m_roomsById.value(roomId);
}

QVariantMap RoomLists::firstUnread() const
{
    // the core sends the rooms by activity; take the first with something
    // unread, a room or a person, whatever the tabs' filters (the rooms the
    // cover counts); a merged row carries its account
    for (const QVariant &v : m_lastRooms) {
        const QVariantMap r = v.toMap();
        if (!isInvited(r) && !r.value("is_space").toBool() && badgeCount(r) > 0)
            return r;
    }
    return QVariantMap();
}

// as RoomModel's: the uid in the merged view, the room id otherwise
static QString rowKey(const QVariant &v)
{
    const QVariantMap room = v.toMap();
    const QString uid = room.value(QStringLiteral("uid")).toString();
    return uid.isEmpty() ? room.value(QStringLiteral("room_id")).toString() : uid;
}

void RoomLists::setRooms(const QVariantList &rooms)
{
    // A row equal to the last one is passed on as the last one: every list
    // then compares it by pointer (one deep compare here instead of one in
    // each model, the fresh parse shares nothing).
    QVariantList kept;
    kept.reserve(rooms.size());
    QHash<QString, QVariant> byKey;
    byKey.reserve(rooms.size());
    for (const QVariant &v : rooms) {
        const QString key = rowKey(v);
        const auto it = m_lastByKey.constFind(key);
        kept.append(it != m_lastByKey.constEnd() && *it == v ? *it : v);
        byKey.insert(key, kept.last());
    }
    m_lastByKey.swap(byKey);
    m_lastRooms = kept;
    distributeRooms(m_lastRooms);
}

// Every stream starts full; its next events have to follow one by one
// (`seq`), and each room in `order` has to be known, else nothing is
// applied and the whole list is asked for again. Rows not sent keep their
// QVariant, so the models and the merged view compare them by pointer; a
// full event's rows are compared once here, as setRooms does (room ids
// stay the same across streams, a resync rebinds nothing unchanged).
RoomLists::Applied RoomLists::applyRooms(const QVariantMap &ev)
{
    const qint64 stream = ev.value(QStringLiteral("stream")).toLongLong();
    const qint64 seq = ev.value(QStringLiteral("seq")).toLongLong();
    const bool full = ev.value(QStringLiteral("full")).toBool();
    const QVariantMap items = ev.value(QStringLiteral("items")).toMap();
    const QVariantList order = ev.value(QStringLiteral("order")).toList();
    if ((m_stream != 0 && stream < m_stream) || stream < m_streamFloor)
        return RoomsDropped;
    const auto resync = [this](const QString &why) {
        if (m_resyncAsked)
            return RoomsDropped;
        qInfo().noquote() << "rooms:" << why << ", all again";
        m_resyncAsked = true;
        return RoomsResync;
    };
    if (!full && (stream != m_stream || seq != m_seq + 1))
        return resync(QStringLiteral("stream %1 event %2 after %3 %4").arg(stream).arg(seq).arg(m_stream).arg(m_seq));
    for (const QVariant &v : order) {
        const QString id = v.toString();
        if (!items.contains(id) && !m_lastByKey.contains(id))
            return resync(QStringLiteral("an unknown room ") + id);
    }
    QVariantList kept;
    kept.reserve(order.size());
    QHash<QString, QVariant> byKey;
    byKey.reserve(order.size());
    int changed = 0;
    for (const QVariant &v : order) {
        const QString id = v.toString();
        const auto sent = items.constFind(id);
        const auto last = m_lastByKey.constFind(id);
        if (sent == items.constEnd()) {
            kept.append(*last);
        } else if (last != m_lastByKey.constEnd() && *last == *sent) {
            kept.append(*last);
        } else {
            kept.append(*sent);
            ++changed;
        }
        byKey.insert(id, kept.last());
    }
    m_stream = stream;
    m_seq = seq;
    if (full)
        m_resyncAsked = false;
    m_lastChanged = changed;
    m_lastByKey.swap(byKey);
    m_lastRooms = kept;
    distributeRooms(m_lastRooms);
    return RoomsApplied;
}

QVariantList RoomLists::mergeAccounts(const QList<QPair<int, QVariantList>> &accounts, MergeCache *cache)
{
    QVariantList merged;
    MergeCache next;
    for (const auto &account : accounts) {
        for (const QVariant &v : account.second) {
            const QVariantMap source = v.toMap();
            const QString uid = QString::number(account.first) + QLatin1Char('/') + source.value("room_id").toString();
            // the row of the last call when its source is the same (RoomLists
            // passes unchanged rows on as they were: a pointer compare)
            if (cache) {
                const auto it = cache->constFind(uid);
                if (it != cache->constEnd() && it->first == v) {
                    merged.append(it->second);
                    next.insert(uid, *it);
                    continue;
                }
            }
            QVariantMap r = source;
            r.insert(QStringLiteral("account"), account.first);
            r.insert(QStringLiteral("uid"), uid);
            merged.append(r);
            if (cache)
                next.insert(uid, qMakePair(v, merged.last()));
        }
    }
    if (cache)
        cache->swap(next);
    // the core's own order (serialize.rs sorted_rooms), across accounts
    struct Key {
        bool invited;
        qint64 ts;
        QString uid;
    };
    QVector<Key> keys;
    keys.reserve(merged.size());
    for (const QVariant &v : merged) {
        const QVariantMap r = v.toMap();
        keys.append({ isInvited(r), r.value("ts").toLongLong(), r.value("uid").toString() });
    }
    QVector<int> index(merged.size());
    for (int i = 0; i < index.size(); ++i)
        index[i] = i;
    std::stable_sort(index.begin(), index.end(), [&](int ia, int ib) {
        const Key &a = keys.at(ia), &b = keys.at(ib);
        if (a.invited != b.invited)
            return a.invited;
        if (a.ts != b.ts)
            return a.ts > b.ts;
        return a.uid < b.uid;
    });
    QVariantList sorted;
    sorted.reserve(merged.size());
    for (int i : index)
        sorted.append(merged.at(i));
    return sorted;
}

void RoomLists::distributeRooms(const QVariantList &rooms)
{
    if (&rooms != &m_lastRooms)
        m_lastRooms = rooms;
    QVariantList groups, dms, favs, everything, targets, spaces;
    // "Hide muted" is each tab's own
    const auto hides = [this](const char *kind) { return tabValue(QLatin1String(kind), QLatin1String(Keys::kHideMuted), Keys::kHideMutedDefault).toBool(); };
    const bool hideRooms = hides("rooms"), hidePeople = hides("people"), hideFavs = hides("favs"), hideAll = hides("all");
    int unread = 0, chats = 0;
    m_roomsById.clear();
    for (const QVariant &v : rooms) {
        // no copies: the rows go on as they came (the section is a role)
        const QVariantMap r = v.toMap();
        const QString id = r.value("room_id").toString();
        const bool invited = isInvited(r);
        const bool fav = r.value("is_fav").toBool();
        m_roomsById.insert(id, r);
        // a joined space goes to the spaces only (an invitation to one stays
        // with the invitations)
        if (r.value("is_space").toBool() && !invited) {
            spaces.append(v);
            continue;
        }
        // the same number the row badge shows
        const int rowUnread = badgeCount(r);
        if (!invited && rowUnread > 0) {
            unread += rowUnread;
            ++chats;
        }
        // the search in the filter menu: only rooms whose name or address
        // contains it (the unread counts above still count every room)
        const bool shown = m_roomFilter.isEmpty()
                || r.value("name").toString().contains(m_roomFilter, Qt::CaseInsensitive)
                || r.value("alias").toString().contains(m_roomFilter, Qt::CaseInsensitive);
        // "Hide muted" in the filter menu, as the tab has it (invitations always show)
        const bool mutedRoom = r.value("muted").toBool() && !invited;
        if (shown) {
            if (r.value("is_dm").toBool()) {
                if (!(mutedRoom && hidePeople))
                    dms.append(v);
            } else if (!(mutedRoom && hideRooms)) {
                groups.append(v);
            }
            // the one list keeps the core's order (by activity) across rooms
            // and people; joining the two lists would put every room first
            if (!(mutedRoom && hideAll))
                everything.append(v);
        }
        if (!invited)
            targets.append(v);
        // the Favs tab mixes rooms and people, by activity, no sections
        if (shown && fav && !invited && !(mutedRoom && hideFavs))
            favs.append(v);
        if (m_displayOnly)
            continue;
        // notifications while running: a room gained notifications (count:
        // the room's own, not a sum of ours); the open room too, as Rustrx
        // may be on the cover (Service.qml leaves out the room in front);
        // read elsewhere, its notification goes
        const int n = r.value("notifications").toInt();
        const int before = m_notificationCounts.value(id, -1);
        if (before >= 0 && n > before)
            emit newMessages(id, r.value("name").toString(), n);
        else if (before > 0 && n == 0)
            emit roomRead(id);
        m_notificationCounts.insert(id, n);
    }
    // every invitation once, with a banner: the ones already notified are
    // kept in the settings (pruned to the open ones), so a restart does not
    // repeat them and one that came while Rustrx was closed still shows
    if (!m_displayOnly) {
        const QStringList notified = m_invitesNotified;
        QStringList open;
        for (const QVariant &v : rooms) {
            const QVariantMap r = v.toMap();
            if (!isInvited(r))
                continue;
            const QString id = r.value("room_id").toString();
            open << id;
            if (!notified.contains(id))
                emit invited(id, r.value("name").toString(), r.value("inviter").toString(), r.value("is_dm").toBool(),
                             r.value("inviter_known").toBool());
        }
        // an invitation answered some other way (another client, a join
        // outside the list): its banner goes
        for (const QString &id : notified)
            if (!open.contains(id))
                emit inviteGone(id);
        if (open != notified) {
            m_invitesNotified = open;
            m_settings->setValue(m_invitesKey, open);
        }
    }
    sortRooms(groups, QStringLiteral("rooms"));
    sortRooms(dms, QStringLiteral("people"));
    sortRooms(favs, QStringLiteral("favs"));
    m_rooms->setRooms(groups);
    m_people->setRooms(dms);
    m_favourites->setRooms(favs);
    // rooms and people together, in the same order
    sortRooms(everything, QStringLiteral("all"));
    m_all->setRooms(everything);
    sortRooms(spaces, QStringLiteral("spaces"));
    m_spaces->setRooms(spaces);
    // what the pickers offer: only while one shows it, else when read
    if (m_targets->watched()) {
        m_targetsDirty = false;
        m_pendingTargets.clear();
        sortRooms(targets, QStringLiteral("rooms"));
        m_targets->setRooms(targets);
    } else {
        m_pendingTargets = targets;
        m_targetsDirty = true;
    }
    if (unread != m_unreadTotal || chats != m_unreadChats) {
        m_unreadTotal = unread;
        m_unreadChats = chats;
        emit unreadChanged();
    }
}

void RoomLists::clear()
{
    // a filter or tab setting changed later must not bring them back
    m_lastRooms.clear();
    m_lastByKey.clear();
    m_streamFloor = qMax(m_streamFloor, m_stream + 1);
    m_stream = m_seq = 0;
    m_resyncAsked = false;
    m_lastChanged = 0;
    m_pendingTargets.clear();
    m_targetsDirty = false;
    m_rooms->setRooms({});
    m_people->setRooms({});
    m_favourites->setRooms({});
    m_all->setRooms({});
    m_targets->setRooms({});
    m_spaces->setRooms({});
    m_knocks.clear();
    m_knocksNotified.clear();
    emit knockRequestsChanged();
    m_notificationCounts.clear();
    m_roomsById.clear();
    if (m_displayOnly)
        return;
    // that account's invitation banners go; the next account starts afresh
    const QStringList notified = m_invitesNotified;
    m_invitesNotified.clear();
    for (const QString &id : notified)
        emit inviteGone(id);
    m_settings->remove(m_invitesKey);
}

void RoomLists::clearUnread()
{
    if (m_unreadTotal || m_unreadChats) {
        m_unreadTotal = m_unreadChats = 0;
        emit unreadChanged();
    }
}
