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
#include <QElapsedTimer>
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
    distributeRooms();
}

void RoomLists::setRoomFilter(const QString &text)
{
    const QString t = text.trimmed();
    if (t == m_roomFilter)
        return;
    m_roomFilter = t;
    emit roomFilterChanged();
    distributeRooms();
}

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
    distributeRooms();
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

RoomLists::TabOrder RoomLists::tabOrder(const QString &kind) const
{
    TabOrder o;
    const QString order = tabValue(kind, QLatin1String(Keys::kSortOrder), QLatin1String(Keys::kSortOrderDefault)).toString();
    o.favsFirst = tabValue(kind, QLatin1String(Keys::kFavsOnTop), Keys::kFavsOnTopDefault).toBool();
    o.lowAtEnd = tabValue(kind, QLatin1String(Keys::kLowLast), Keys::kLowLastDefault).toBool();
    o.byUnread = order == QLatin1String("unread");
    o.byName = order == QLatin1String("name");
    // "Hide muted" is each tab's own
    o.hideMuted = tabValue(kind, QLatin1String(Keys::kHideMuted), Keys::kHideMutedDefault).toBool();
    // made only for the order by name (a QCollator per sort, used or not)
    if (o.byName && !m_collator)
        m_collator.reset(new QCollator);
    return o;
}

RoomLists::SortKey RoomLists::sortKey(const Row &r, const TabOrder &o) const
{
    return { r.invited, o.favsFirst && r.fav, o.lowAtEnd && r.low, o.byUnread ? r.badge : 0,
             o.byName ? r.name : QString() };
}

bool RoomLists::keyLess(const SortKey &a, const SortKey &b, const TabOrder &o) const
{
    if (a.invited != b.invited)
        return a.invited;
    if (a.fav != b.fav)
        return a.fav;
    if (a.low != b.low)
        return b.low;
    // the most unread on top; equal counts keep the activity order
    if (o.byUnread)
        return a.unread > b.unread;
    if (o.byName)
        return m_collator->compare(a.name, b.name) < 0;
    return false;
}

void RoomLists::readRow(Row &r) const
{
    const QVariantMap m = r.row.toMap();
    r.id = m.value(QStringLiteral("room_id")).toString();
    r.invited = isInvited(m);
    r.fav = m.value(QStringLiteral("is_fav")).toBool();
    r.low = m.value(QStringLiteral("is_low")).toBool();
    r.dm = m.value(QStringLiteral("is_dm")).toBool();
    r.space = m.value(QStringLiteral("is_space")).toBool();
    r.muted = m.value(QStringLiteral("muted")).toBool();
    r.notifications = m.value(QStringLiteral("notifications")).toInt();
    // the same number the row badge shows
    r.badge = badgeCount(m);
    r.name = m.value(QStringLiteral("name")).toString();
    // the search in the filter menu: only rooms whose name or address
    // contains it (the unread counts still count every room)
    r.shown = m_roomFilter.isEmpty() || r.name.contains(m_roomFilter, Qt::CaseInsensitive)
            || m.value(QStringLiteral("alias")).toString().contains(m_roomFilter, Qt::CaseInsensitive);
}

uint RoomLists::tabsOf(const Row &r) const
{
    // a joined space goes to the spaces only (an invitation to one stays
    // with the invitations)
    if (r.space && !r.invited)
        return 1u << TabSpaces;
    if (!r.shown)
        return 0;
    // "Hide muted" in the filter menu, as the tab has it (invitations always show)
    const bool mutedRoom = r.muted && !r.invited;
    uint tabs = 0;
    if (r.dm) {
        if (!(mutedRoom && m_order[TabPeople].hideMuted))
            tabs |= 1u << TabPeople;
    } else if (!(mutedRoom && m_order[TabRooms].hideMuted)) {
        tabs |= 1u << TabRooms;
    }
    // the one list keeps the core's order (by activity) across rooms and
    // people; joining the two lists would put every room first
    if (!(mutedRoom && m_order[TabAll].hideMuted))
        tabs |= 1u << TabAll;
    // the Favs tab mixes rooms and people, by activity, no sections
    if (r.fav && !r.invited && !(mutedRoom && m_order[TabFavs].hideMuted))
        tabs |= 1u << TabFavs;
    return tabs;
}

RoomModel *RoomLists::tabModel(int tab) const
{
    switch (tab) {
    case TabRooms: return m_rooms;
    case TabPeople: return m_people;
    case TabFavs: return m_favourites;
    case TabAll: return m_all;
    default: return m_spaces;
    }
}

// The core sends the rooms by activity. Invitations stay on top; then, if
// switched on, favourites first and low priority rooms last; within that,
// the most unread first or by name, else the activity order stays (the sort
// is stable).
void RoomLists::sortRooms(QVariantList &rooms, const QString &kind) const
{
    const TabOrder o = tabOrder(kind);
    QVector<SortKey> keys;
    keys.reserve(rooms.size());
    for (const QVariant &v : rooms) {
        Row r;
        r.row = v;
        readRow(r);
        keys.append(sortKey(r, o));
    }
    QVector<int> index(rooms.size());
    for (int i = 0; i < index.size(); ++i)
        index[i] = i;
    std::stable_sort(index.begin(), index.end(), [&](int ia, int ib) { return keyLess(keys.at(ia), keys.at(ib), o); });
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
    // made and sorted when something reads it (the room pickers): joined
    // rooms and people, no spaces, in the core's order
    if (m_targetsDirty) {
        m_targetsDirty = false;
        QVariantList rows;
        for (const QString &key : m_lastKeys) {
            const Row &r = m_rows.value(key);
            if (!r.invited && !r.space)
                rows.append(r.row);
        }
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

// how many changed rows are still placed one by one; more and every tab is
// sorted anew (cheaper then)
static int touchedMax(int rows)
{
    return qMax(16, rows / 4);
}

void RoomLists::setRooms(const QVariantList &rooms, bool full)
{
    QVector<QString> keys;
    keys.reserve(rooms.size());
    for (const QVariant &v : rooms)
        keys.append(rowKey(v));
    setRooms(rooms, keys, full);
}

void RoomLists::setRooms(const QVariantList &rooms, const QVector<QString> &given, bool full)
{
    // A row equal to the last one is passed on as the last one: every list
    // then compares it by pointer (one deep compare here instead of one in
    // each model, the fresh parse shares nothing).
    const quint32 pass = ++m_pass;
    QVariantList kept;
    kept.reserve(rooms.size());
    QVector<QString> keys;
    keys.reserve(rooms.size());
    QVector<QString> touched;
    QVector<Row> old;
    bool inOrder = true;
    int lastPos = -1;
    for (int i = 0; i < rooms.size(); ++i) {
        const QVariant &v = rooms.at(i);
        const QString &key = given.at(i);
        auto it = m_rows.find(key);
        if (it == m_rows.end()) {
            Row r;
            r.row = v;
            it = m_rows.insert(key, r);
            touched.append(key);
            old.append(Row());
        } else if (it->seen == pass) {
            // twice in one list: no order to keep
            inOrder = false;
        } else if (!(it->row == v)) {
            touched.append(key);
            old.append(*it);
            it->row = v;
        } else {
            if (it->pos < lastPos)
                inOrder = false;
            lastPos = it->pos;
        }
        it->pos = i;
        it->seen = pass;
        kept.append(it->row);
        keys.append(key);
    }
    takeRows(kept, keys, touched, old, inOrder, full);
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
        if (!items.contains(id) && !m_rows.contains(id))
            return resync(QStringLiteral("an unknown room ") + id);
    }
    const quint32 pass = ++m_pass;
    QVariantList kept;
    kept.reserve(order.size());
    QVector<QString> keys;
    keys.reserve(order.size());
    QVector<QString> touched;
    QVector<Row> old;
    bool inOrder = true;
    int lastPos = -1;
    for (int i = 0; i < order.size(); ++i) {
        const QString id = order.at(i).toString();
        const auto sent = items.constFind(id);
        auto it = m_rows.find(id);
        if (it == m_rows.end()) {
            Row r;
            r.row = *sent;
            it = m_rows.insert(id, r);
            touched.append(id);
            old.append(Row());
        } else if (it->seen == pass) {
            inOrder = false;
        } else if (sent != items.constEnd() && !(it->row == *sent)) {
            touched.append(id);
            old.append(*it);
            it->row = *sent;
        } else {
            if (it->pos < lastPos)
                inOrder = false;
            lastPos = it->pos;
        }
        it->pos = i;
        it->seen = pass;
        kept.append(it->row);
        keys.append(id);
    }
    m_stream = stream;
    m_seq = seq;
    if (full)
        m_resyncAsked = false;
    m_lastChanged = touched.size();
    takeRows(kept, keys, touched, old, inOrder, full);
    return RoomsApplied;
}

void RoomLists::takeRows(const QVariantList &kept, const QVector<QString> &keys, const QVector<QString> &touched,
                         const QVector<Row> &old, bool inOrder, bool full)
{
    // the rows no longer in the list
    QVector<Row> gone;
    if (!inOrder || m_rows.size() != keys.size()) {
        for (auto it = m_rows.begin(); it != m_rows.end();) {
            if (it->seen != m_pass) {
                gone.append(*it);
                it = m_rows.erase(it);
            } else {
                ++it;
            }
        }
    }
    m_lastRooms = kept;
    m_lastKeys = keys;
    for (int i = 0; i < touched.size(); ++i)
        readRow(m_rows[touched.at(i)]);
    m_lastFull = full || !inOrder || !m_placed || touched.size() + gone.size() > touchedMax(keys.size());
    if (m_lastFull)
        distributeRooms();
    else if (!touched.isEmpty() || !gone.isEmpty())
        placeTouched(touched, old, gone);
    else
        flushSignals();
}

QVariantList RoomLists::mergeAccounts(const QList<QPair<int, QVariantList>> &accounts, MergeCache *cache,
                                      QVector<QString> *uids)
{
    MergeCache own;
    if (!cache)
        cache = &own;
    const quint32 pass = ++cache->pass;
    struct Key {
        bool invited;
        qint64 ts;
        QString uid;
        QVariant row;
    };
    QVector<Key> keys;
    QSet<int> shown;
    for (const auto &account : accounts) {
        shown.insert(account.first);
        QHash<QString, MergeRow> &rows = cache->accounts[account.first];
        const QString prefix = QString::number(account.first) + QLatin1Char('/');
        int seen = 0;
        for (const QVariant &v : account.second) {
            const QVariantMap source = v.toMap();
            const QString id = source.value(QStringLiteral("room_id")).toString();
            MergeRow &m = rows[id];
            // the row of the last call when its source is the same (RoomLists
            // passes unchanged rows on as they were: a pointer compare)
            if (m.seen == 0 || !(m.source == v)) {
                QVariantMap r = source;
                m.uid = prefix + id;
                r.insert(QStringLiteral("account"), account.first);
                r.insert(QStringLiteral("uid"), m.uid);
                m.source = v;
                m.merged = r;
                m.invited = isInvited(source);
                m.ts = source.value(QStringLiteral("ts")).toLongLong();
            }
            if (m.seen != pass)
                ++seen;
            m.seen = pass;
            keys.append({ m.invited, m.ts, m.uid, m.merged });
        }
        if (seen != rows.size()) {
            for (auto it = rows.begin(); it != rows.end();) {
                if (it->seen == pass)
                    ++it;
                else
                    it = rows.erase(it);
            }
        }
    }
    for (auto it = cache->accounts.begin(); it != cache->accounts.end();) {
        if (shown.contains(it.key()))
            ++it;
        else
            it = cache->accounts.erase(it);
    }
    // the core's own order (serialize.rs sorted_rooms), across accounts
    QVector<int> index(keys.size());
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
    sorted.reserve(keys.size());
    if (uids) {
        uids->clear();
        uids->reserve(keys.size());
    }
    for (int i : index) {
        sorted.append(keys.at(i).row);
        if (uids)
            uids->append(keys.at(i).uid);
    }
    return sorted;
}

// the room's notifications while running: it gained some (count: the
// room's own, not a sum of ours); the open room too, as Rustrx may be on the
// cover (Service.qml leaves out the room in front); read elsewhere, its
// notification goes. Sent once every list holds the update.
void RoomLists::distributeRooms()
{
    for (int t = 0; t < TabCount; ++t) {
        static const char *kinds[] = { "rooms", "people", "favs", "all", "spaces" };
        m_order[t] = tabOrder(QLatin1String(kinds[t]));
    }
    QVector<int> members[TabCount];
    int unread = 0, chats = 0;
    m_roomsById.clear();
    for (int i = 0; i < m_lastKeys.size(); ++i) {
        Row &r = m_rows[m_lastKeys.at(i)];
        readRow(r);
        // no copies: the rows go on as they came (the section is a role);
        // the merged list has none, roomInfo is the accounts' own
        if (!m_displayOnly)
            m_roomsById.insert(r.id, r.row.toMap());
        r.tabs = tabsOf(r);
        for (int t = 0; t < TabCount; ++t)
            if (r.tabs & (1u << t))
                members[t].append(i);
        if (r.space && !r.invited)
            continue;
        if (!r.invited && r.badge > 0) {
            unread += r.badge;
            ++chats;
        }
        if (m_displayOnly)
            continue;
        const int before = m_notificationCounts.value(r.id, -1);
        const QString id = r.id, name = r.name;
        const int n = r.notifications;
        const QString latest = r.row.toMap().value(QStringLiteral("latest_event_id")).toString();
        if (before >= 0 && n > before)
            announce(id, name, n, latest);
        else if (before > 0 && n == 0)
            m_pendingSignals.append([this, id]() { emit roomRead(id); });
        else if (m_awaitLatest.contains(id) && latest != m_latestSeen.value(id))
            announce(id, name, n, latest);
        m_notificationCounts.insert(id, n);
        m_latestSeen.insert(id, latest);
    }
    if (!m_displayOnly)
        notifyInvites();
    for (int t = 0; t < TabCount; ++t) {
        const TabOrder &o = m_order[t];
        QVector<int> &index = members[t];
        QVector<SortKey> keys;
        keys.reserve(index.size());
        for (int i : index)
            keys.append(sortKey(m_rows[m_lastKeys.at(i)], o));
        QVector<int> at(index.size());
        for (int k = 0; k < at.size(); ++k)
            at[k] = k;
        std::stable_sort(at.begin(), at.end(), [&](int a, int b) { return keyLess(keys.at(a), keys.at(b), o); });
        TabRows &tab = m_tab[t];
        QVariantList rows;
        tab.keys.clear();
        tab.sort.clear();
        rows.reserve(at.size());
        tab.keys.reserve(at.size());
        tab.sort.reserve(at.size());
        for (int k : at) {
            rows.append(m_lastRooms.at(index.at(k)));
            tab.keys.append(m_lastKeys.at(index.at(k)));
            tab.sort.append(keys.at(k));
        }
        tabModel(t)->setRows(rows, tab.keys);
    }
    m_placed = true;
    updateTargets();
    if (unread != m_unreadTotal || chats != m_unreadChats) {
        m_unreadTotal = unread;
        m_unreadChats = chats;
        m_pendingSignals.append([this]() { emit unreadChanged(); });
    }
    flushSignals();
}

// Only the rows that changed, came or went: each leaves the tabs it was in
// and goes into the ones it belongs to now, at the place a full sort would
// give it (the order's keys, then the core's order). The rows that did not
// change kept their order against each other (takeRows checked), so the
// rest of each tab stays sorted as it was.
void RoomLists::placeTouched(const QVector<QString> &touched, const QVector<Row> &old, const QVector<Row> &gone)
{
    bool invites = false;
    int unread = m_unreadTotal, chats = m_unreadChats;
    const auto uncount = [&](const Row &r) {
        if (!r.space && !r.invited && r.badge > 0) {
            unread -= r.badge;
            --chats;
        }
    };
    QVector<QString> goneKeys;
    for (const Row &r : gone) {
        invites = invites || r.invited;
        uncount(r);
        goneKeys.append(rowKey(r.row));
        m_roomsById.remove(r.id);
    }
    // touched in the order of the list: the signals come in that order
    QVector<int> order;
    order.reserve(touched.size());
    for (int k = 0; k < touched.size(); ++k)
        order.append(k);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return m_rows[touched.at(a)].pos < m_rows[touched.at(b)].pos; });
    for (int k : order) {
        Row &r = m_rows[touched.at(k)];
        const Row &before = old.at(k);
        const bool known = !before.id.isEmpty();
        if (known) {
            invites = invites || before.invited;
            uncount(before);
        }
        invites = invites || r.invited;
        if (!m_displayOnly)
            m_roomsById.insert(r.id, r.row.toMap());
        r.tabs = tabsOf(r);
        if (r.space && !r.invited)
            continue;
        if (!r.invited && r.badge > 0) {
            unread += r.badge;
            ++chats;
        }
        if (m_displayOnly)
            continue;
        const int was = m_notificationCounts.value(r.id, -1);
        const QString id = r.id, name = r.name;
        const int n = r.notifications;
        const QString latest = r.row.toMap().value(QStringLiteral("latest_event_id")).toString();
        if (was >= 0 && n > was)
            announce(id, name, n, latest);
        else if (was > 0 && n == 0) {
            m_awaitLatest.remove(id);
            m_pendingSignals.append([this, id]() { emit roomRead(id); });
        } else if (m_awaitLatest.contains(id) && latest != m_latestSeen.value(id))
            announce(id, name, n, latest);
        m_notificationCounts.insert(id, n);
        m_latestSeen.insert(id, latest);
    }
    if (invites && !m_displayOnly)
        notifyInvites();
    for (int t = 0; t < TabCount; ++t) {
        const uint bit = 1u << t;
        TabRows &tab = m_tab[t];
        const TabOrder &o = m_order[t];
        RoomModel::Touched inTab;
        // out: the rows gone and the touched ones (back in below if they
        // still belong here, with their fresh row and keys)
        bool any = false;
        for (const Row &r : gone)
            any = any || (r.tabs & bit);
        for (int k = 0; k < touched.size() && !any; ++k)
            any = (old.at(k).tabs & bit) || (m_rows[touched.at(k)].tabs & bit);
        if (!any)
            continue;
        const auto takeOut = [&](const QString &key) {
            const int i = tab.keys.indexOf(key);
            if (i < 0)
                return;
            inTab.append(qMakePair(key, QVariant()));
            tab.keys.remove(i);
            tab.sort.remove(i);
        };
        for (int g = 0; g < gone.size(); ++g)
            if (gone.at(g).tabs & bit)
                takeOut(goneKeys.at(g));
        for (int k = 0; k < touched.size(); ++k)
            if (old.at(k).tabs & bit)
                takeOut(touched.at(k));
        for (int k : order) {
            const QString &key = touched.at(k);
            const Row &r = m_rows[key];
            if (!(r.tabs & bit))
                continue;
            const SortKey sk = sortKey(r, o);
            // the first row that comes after this one
            int lo = 0, hi = tab.keys.size();
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                const SortKey &mk = tab.sort.at(mid);
                const bool after = keyLess(sk, mk, o) || (!keyLess(mk, sk, o) && r.pos < m_rows[tab.keys.at(mid)].pos);
                if (after)
                    hi = mid;
                else
                    lo = mid + 1;
            }
            tab.keys.insert(lo, key);
            tab.sort.insert(lo, sk);
            // back in (or in for the first time): with its fresh row
            int seen = 0;
            while (seen < inTab.size() && inTab.at(seen).first != key)
                ++seen;
            if (seen < inTab.size())
                inTab[seen].second = r.row;
            else
                inTab.append(qMakePair(key, r.row));
        }
        tabModel(t)->setTouchedRows(tab.keys, inTab);
    }
    updateTargets();
    if (unread != m_unreadTotal || chats != m_unreadChats) {
        m_unreadTotal = unread;
        m_unreadChats = chats;
        m_pendingSignals.append([this]() { emit unreadChanged(); });
    }
    flushSignals();
}

// every invitation once, with a banner: the ones already notified are kept
// in the settings (pruned to the open ones), so a restart does not repeat
// them and one that came while Rustrx was closed still shows
void RoomLists::notifyInvites()
{
    const QStringList notified = m_invitesNotified;
    QStringList open;
    for (const QString &key : m_lastKeys) {
        const Row &r = m_rows[key];
        if (!r.invited)
            continue;
        open << r.id;
        if (!notified.contains(r.id)) {
            const QVariantMap m = r.row.toMap();
            const QString id = r.id, name = r.name, inviter = m.value(QStringLiteral("inviter")).toString();
            const bool direct = r.dm, known = m.value(QStringLiteral("inviter_known")).toBool();
            m_pendingSignals.append([this, id, name, inviter, direct, known]() { emit invited(id, name, inviter, direct, known); });
        }
    }
    // an invitation answered some other way (another client, a join
    // outside the list): its banner goes
    for (const QString &id : notified)
        if (!open.contains(id))
            m_pendingSignals.append([this, id]() { emit inviteGone(id); });
    if (open != notified) {
        m_invitesNotified = open;
        m_settings->setValue(m_invitesKey, open);
    }
}

void RoomLists::updateTargets()
{
    // what the pickers offer: only while one shows it, else when read
    m_targetsDirty = true;
    if (m_targets->watched())
        targets();
}

// a room's count rose: told at once when its newest message changed with
// it, else once that message arrives (or after a short wait)
void RoomLists::announce(const QString &id, const QString &name, int n, const QString &latest)
{
    if (latest != m_latestSeen.value(id) || latest.isEmpty()) {
        m_awaitLatest.remove(id);
        m_pendingSignals.append([this, id, name, n]() { emit newMessages(id, name, n); });
        return;
    }
    m_awaitLatest.insert(id, qMakePair(name, n));
    if (!m_awaitTimer) {
        m_awaitTimer = new QTimer(this);
        m_awaitTimer->setSingleShot(true);
        m_awaitTimer->setInterval(Keys::kNotifyLatestWaitMs);
        connect(m_awaitTimer, &QTimer::timeout, this, &RoomLists::flushAwaited);
    }
    if (!m_awaitTimer->isActive())
        m_awaitTimer->start();
}

void RoomLists::flushAwaited()
{
    const auto waiting = m_awaitLatest;
    m_awaitLatest.clear();
    for (auto it = waiting.cbegin(); it != waiting.cend(); ++it)
        emit newMessages(it.key(), it.value().first, it.value().second);
}

void RoomLists::flushSignals()
{
    if (m_pendingSignals.isEmpty()) {
        m_lastSignalsUs = 0;
        return;
    }
    QElapsedTimer t;
    t.start();
    // a handler that updates the lists again sends that update's own
    QVector<std::function<void()>> pending;
    pending.swap(m_pendingSignals);
    for (const auto &emitIt : pending)
        emitIt();
    m_lastSignalsUs = t.nsecsElapsed() / 1000;
}

void RoomLists::clear()
{
    // a filter or tab setting changed later must not bring them back
    m_lastRooms.clear();
    m_lastKeys.clear();
    m_rows.clear();
    m_placed = false;
    for (TabRows &tab : m_tab)
        tab = TabRows();
    m_streamFloor = qMax(m_streamFloor, m_stream + 1);
    m_stream = m_seq = 0;
    m_resyncAsked = false;
    m_lastChanged = 0;
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
    m_latestSeen.clear();
    m_awaitLatest.clear();
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
    // the totals are kept by row from here on: the next update counts anew
    m_placed = false;
    if (m_unreadTotal || m_unreadChats) {
        m_unreadTotal = m_unreadChats = 0;
        emit unreadChanged();
    }
}
