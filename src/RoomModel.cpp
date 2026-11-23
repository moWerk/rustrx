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
#include "RoomModel.h"
#include "Twemoji.h"

#include <QMetaMethod>
#include <QSet>
#include <algorithm>

// A row's key: its uid in the merged view (the same room can be there for
// two accounts), its room id otherwise.
static QString rowKey(const QVariantMap &room)
{
    const QString uid = room.value(QStringLiteral("uid")).toString();
    return uid.isEmpty() ? room.value(QStringLiteral("room_id")).toString() : uid;
}

// keys no delegate reads (their readers go through get() or roomInfo):
// a change in them alone tells the views nothing; latest_event_id changes
// with every message and rebound every role of the row
static bool silentKey(const QString &key)
{
    return key == QLatin1String("latest_event_id") || key == QLatin1String("dm_user")
            || key == QLatin1String("inviter");
}

// one emoji size for every list: the first delegate of the first list sets
// it, and no other list renders its names twice
static int s_nameEmojiSize = 42;
static QList<RoomModel *> s_models;

RoomModel::RoomModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "room_id", "name", "topic", "alias", "is_dm", "is_fav", "is_low", "unread",
                            "notifications", "highlights", "ts", "state", "encrypted", "section",
                            "avatar_path", "avatar_v", "has_avatar", "preview", "preview_sender",
                            "muted", "own_read", "replaced_by", "in_call", "inviter_known",
                            "is_space", "join_rule", "can_join_rules", "can_manage_children", "children", "parents",
                            "account", "uid", "name_html", "preview_html" };
    int role = Qt::UserRole + 1;
    for (const char *n : names) {
        m_roles.insert(role, QByteArray(n));
        m_keys.insert(role, QString::fromLatin1(n));
        m_roleOf.insert(QString::fromLatin1(n), role);
        ++role;
    }
    m_nameHtmlRole = m_roleOf.value(QStringLiteral("name_html"));
    m_previewHtmlRole = m_roleOf.value(QStringLiteral("preview_html"));
    m_sectionRole = m_roleOf.value(QStringLiteral("section"));
    s_models.append(this);
}

RoomModel::~RoomModel()
{
    s_models.removeOne(this);
}

int RoomModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rooms.size();
}

QVariant RoomModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rooms.size())
        return QVariant();
    if (role == m_nameHtmlRole)
        return html(index.row(), true).name;
    if (role == m_previewHtmlRole)
        return html(index.row(), false).preview;
    const QVariantMap room = m_rooms.at(index.row()).toMap();
    if (role == m_sectionRole) {
        if (room.value(QStringLiteral("state")).toString() != QLatin1String("invited"))
            return QString();
        //: list section for pending invitations
        //% "Invitations"
        return qtTrId("rustrx-se-invitations");
    }
    return room.value(m_keys.value(role));
}

QHash<int, QByteArray> RoomModel::roleNames() const
{
    return m_roles;
}

QVariantMap RoomModel::get(int row) const
{
    return row >= 0 && row < m_rooms.size() ? m_rooms.at(row).toMap() : QVariantMap();
}

bool RoomModel::watched() const
{
    // QObject::receivers counts QML's handlers and bindings as well, but
    // looks each signal up by its name: isSignalConnected first, a quick
    // no for the lists nothing follows (it can stay yes after a QML
    // follower went, so a yes is asked again)
    struct Followed {
        QMetaMethod method;
        const char *signal;
    };
    static const Followed followed[] = {
        { QMetaMethod::fromSignal(&QAbstractItemModel::dataChanged), SIGNAL(dataChanged(QModelIndex,QModelIndex,QVector<int>)) },
        { QMetaMethod::fromSignal(&QAbstractItemModel::rowsInserted), SIGNAL(rowsInserted(QModelIndex,int,int)) },
        { QMetaMethod::fromSignal(&QAbstractItemModel::rowsRemoved), SIGNAL(rowsRemoved(QModelIndex,int,int)) },
        { QMetaMethod::fromSignal(&QAbstractItemModel::rowsMoved), SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)) },
        { QMetaMethod::fromSignal(&QAbstractItemModel::modelReset), SIGNAL(modelReset()) },
        { QMetaMethod::fromSignal(&RoomModel::countChanged), SIGNAL(countChanged()) },
    };
    for (const Followed &f : followed)
        if (isSignalConnected(f.method) && receivers(f.signal) > 0)
            return true;
    return false;
}

// the roles whose values differ between two versions of a room; *all when
// a key without a role changed (the silent ones aside)
QVector<int> RoomModel::changedRoles(const QVariantMap &from, const QVariantMap &to, bool *all) const
{
    QVector<int> roles;
    bool unknown = false;
    auto note = [&](const QString &key) {
        if (silentKey(key))
            return;
        const int role = m_roleOf.value(key, -1);
        if (role < 0) {
            unknown = true;
            return;
        }
        if (!roles.contains(role))
            roles << role;
        if (key == QLatin1String("name"))
            roles << m_nameHtmlRole;
        else if (key == QLatin1String("preview"))
            roles << m_previewHtmlRole;
        else if (key == QLatin1String("state") && !roles.contains(m_sectionRole))
            roles << m_sectionRole;
    };
    for (auto it = from.constBegin(); it != from.constEnd(); ++it)
        if (it.value() != to.value(it.key()))
            note(it.key());
    for (auto it = to.constBegin(); it != to.constEnd(); ++it)
        if (!from.contains(it.key()))
            note(it.key());
    *all = unknown;
    return unknown ? QVector<int>() : roles;
}

// the name with its emoji as pictures (& and < escaped, as the QML did)
QString RoomModel::nameHtml(const QVariantMap &room) const
{
    return Twemoji::inHtml(StyledText::escaped(room.value(QStringLiteral("name")).toString(), false), s_nameEmojiSize);
}

// the last message escaped, in one line or with its breaks; its emoji stay
// text in the system font, as in the message field: as pictures they were
// drawn out of place by the preview's label (its first line shifted for the
// icons, its fade), a gap or a picture a line too low on the phone
QString RoomModel::previewHtml(const QVariantMap &room) const
{
    QString b = StyledText::escaped(room.value(QStringLiteral("preview")).toString(), false);
    if (m_previewMultiLine)
        b.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    else
        b = StyledText::oneLine(b);
    return b;
}

const RoomModel::Html &RoomModel::html(int row, bool name) const
{
    const QVariantMap room = m_rooms.at(row).toMap();
    Html &h = m_html[m_rowKeys.at(row)];
    if (name) {
        const QString from = room.value(QStringLiteral("name")).toString();
        if (h.nameSize != s_nameEmojiSize || h.nameFrom != from) {
            h.name = nameHtml(room);
            h.nameFrom = from;
            h.nameSize = s_nameEmojiSize;
        }
    } else {
        const QString from = room.value(QStringLiteral("preview")).toString();
        if (h.previewMulti != int(m_previewMultiLine) || h.previewFrom != from) {
            h.preview = previewHtml(room);
            h.previewFrom = from;
            h.previewMulti = int(m_previewMultiLine);
        }
    }
    return h;
}

void RoomModel::rerender(bool name)
{
    // the html is made again when a view asks for it
    if (!m_rooms.isEmpty() && watched())
        emit dataChanged(index(0), index(m_rooms.size() - 1), { name ? m_nameHtmlRole : m_previewHtmlRole });
}

int RoomModel::nameEmojiSize() const
{
    return s_nameEmojiSize;
}

void RoomModel::setNameEmojiSize(int size)
{
    if (size == s_nameEmojiSize || size <= 0)
        return;
    s_nameEmojiSize = size;
    for (RoomModel *m : s_models) {
        emit m->nameEmojiSizeChanged();
        m->rerender(true);
    }
}

void RoomModel::setPreviewMultiLine(bool multi)
{
    if (multi == m_previewMultiLine)
        return;
    m_previewMultiLine = multi;
    emit previewMultiLineChanged();
    rerender(false);
}

// the positions of a longest increasing run (not necessarily adjacent) in seq
static QVector<int> longestIncreasing(const QVector<int> &seq)
{
    QVector<int> tails, tailAt, prev(seq.size(), -1);
    for (int i = 0; i < seq.size(); ++i) {
        const int at = int(std::lower_bound(tails.begin(), tails.end(), seq.at(i)) - tails.begin());
        if (at == tails.size()) {
            tails.append(seq.at(i));
            tailAt.append(i);
        } else {
            tails[at] = seq.at(i);
            tailAt[at] = i;
        }
        prev[i] = at > 0 ? tailAt.at(at - 1) : -1;
    }
    QVector<int> out(tails.size());
    for (int i = tailAt.isEmpty() ? -1 : tailAt.last(), k = tails.size() - 1; i >= 0; i = prev.at(i), --k)
        out[k] = i;
    return out;
}

// From the shown rows (none of them gone) to the new order: the rows of a
// longest run that is already in order stay, the others move, rows next to
// each other as one; new rows come in runs. Walked from the end: each row
// goes in front of the one after it.
void RoomModel::reorder(const QVariantList &rooms, const QVector<QString> &keys)
{
    QHash<QString, int> oldAt;
    for (int i = 0; i < m_rowKeys.size(); ++i)
        oldAt.insert(m_rowKeys.at(i), i);
    QVector<int> seq, seqRow;
    for (int i = 0; i < keys.size(); ++i) {
        const auto it = oldAt.constFind(keys.at(i));
        if (it != oldAt.constEnd()) {
            seq.append(*it);
            seqRow.append(i);
        }
    }
    QSet<QString> stays;
    for (int i : longestIncreasing(seq))
        stays.insert(keys.at(seqRow.at(i)));
    const auto posOf = [this](const QString &key, int hint) {
        // usually right in front of the last one placed
        if (hint > 0 && hint <= m_rowKeys.size() && m_rowKeys.at(hint - 1) == key)
            return hint - 1;
        return m_rowKeys.indexOf(key);
    };
    int anchor = m_rowKeys.size();
    for (int i = keys.size() - 1; i >= 0;) {
        const QString &key = keys.at(i);
        if (stays.contains(key)) {
            anchor = posOf(key, anchor);
            --i;
            continue;
        }
        int j = i;
        if (!oldAt.contains(key)) {
            while (j > 0 && !oldAt.contains(keys.at(j - 1)))
                --j;
            beginInsertRows(QModelIndex(), anchor, anchor + i - j);
            for (int t = j; t <= i; ++t) {
                m_rooms.insert(anchor + t - j, rooms.at(t));
                m_rowKeys.insert(anchor + t - j, keys.at(t));
            }
            endInsertRows();
            i = j - 1;
            continue;
        }
        const int last = posOf(key, anchor);
        while (j > 0 && oldAt.contains(keys.at(j - 1)) && !stays.contains(keys.at(j - 1))
               && last - (i - j) - 1 >= 0 && m_rowKeys.at(last - (i - j) - 1) == keys.at(j - 1))
            --j;
        const int first = last - (i - j), n = i - j + 1;
        if (last + 1 != anchor) {
            beginMoveRows(QModelIndex(), first, last, QModelIndex(), anchor);
            const int to = anchor > last ? anchor - n : anchor;
            const QVariantList block = m_rooms.mid(first, n);
            const QVector<QString> blockKeys = m_rowKeys.mid(first, n);
            for (int t = 0; t < n; ++t) {
                m_rooms.removeAt(first);
                m_rowKeys.remove(first);
            }
            for (int t = 0; t < n; ++t) {
                m_rooms.insert(to + t, block.at(t));
                m_rowKeys.insert(to + t, blockKeys.at(t));
            }
            endMoveRows();
            anchor = to;
        } else {
            anchor = first;
        }
        i = j - 1;
    }
}

// Removes, inserts and moves instead of a reset, so the list view keeps its
// place (a reset throws it back to the top, after leaving a room or when a
// room moves up while the user is reading another one). Neighbouring rows
// go in one step, and a room that moved is one move.
void RoomModel::setRooms(const QVariantList &rooms)
{
    QVector<QString> keys;
    keys.reserve(rooms.size());
    for (const QVariant &v : rooms)
        keys.append(rowKey(v.toMap()));
    setRows(rooms, keys);
}

bool RoomModel::takeUnwatched(const QVariantList &rooms, const QVector<QString> &keys)
{
    // nothing follows the list: nothing to tell, the views that come ask
    if (watched())
        return false;
    const int before = m_rooms.size();
    m_rooms = rooms;
    m_rowKeys = keys;
    if (m_html.size() > 2 * rooms.size() + 16)
        m_html.clear();
    if (before != rooms.size())
        emit countChanged();
    return true;
}

void RoomModel::setRows(const QVariantList &rooms, const QVector<QString> &keys)
{
    if (takeUnwatched(rooms, keys))
        return;
    const int before = m_rooms.size();

    // the first fill in one step
    if (m_rooms.isEmpty()) {
        if (!rooms.isEmpty()) {
            beginInsertRows(QModelIndex(), 0, rooms.size() - 1);
            m_rooms = rooms;
            m_rowKeys = keys;
            endInsertRows();
            emit countChanged();
        }
        return;
    }

    if (keys != m_rowKeys) {
        const QSet<QString> wanted = QSet<QString>::fromList(keys.toList());
        for (int i = m_rowKeys.size() - 1; i >= 0; --i) {
            if (wanted.contains(m_rowKeys.at(i)))
                continue;
            const int last = i;
            while (i > 0 && !wanted.contains(m_rowKeys.at(i - 1)))
                --i;
            beginRemoveRows(QModelIndex(), i, last);
            for (int k = last; k >= i; --k) {
                m_html.remove(m_rowKeys.at(k));
                m_rooms.removeAt(k);
                m_rowKeys.remove(k);
            }
            endRemoveRows();
        }
        if (keys != m_rowKeys)
            reorder(rooms, keys);
    }

    // same order now; take the fresh fields, and tell only about the rows
    // and roles that changed (every row on every update rebound every
    // delegate, and a range rebound the unchanged rows inside it)
    QVector<int> rows;
    for (int i = 0; i < rooms.size(); ++i)
        if (!(m_rooms.at(i) == rooms.at(i)))
            rows.append(i);
    tellChanged(rooms, rows);
    if (before != rooms.size())
        emit countChanged();
}

void RoomModel::tellChanged(const QVariantList &rooms, const QVector<int> &rows)
{
    QVector<QPair<int, QVector<int>>> changed;
    for (int i : rows) {
        bool all = false;
        const QVector<int> roles = changedRoles(m_rooms.at(i).toMap(), rooms.at(i).toMap(), &all);
        if (all || !roles.isEmpty())
            changed.append(qMakePair(i, roles));
    }
    m_rooms = rooms;
    for (const auto &c : changed)
        emit dataChanged(index(c.first), index(c.first), c.second);
}

// Each touched row in the order of the new list goes right behind the row
// before it there (the rows before it are in place by then, the untouched
// ones never move against each other): one move or insert per row that is
// not there yet, the same steps setRows takes for a single row. The rows
// are changed in place (a list shared with the caller would be copied).
void RoomModel::setTouchedRows(const QVector<QString> &keys, const Touched &touched)
{
    const bool tell = watched();
    const int before = m_rooms.size();
    // where the touched rows are in the new list (few: a look each)
    QVector<QPair<int, int>> places;
    for (int t = 0; t < touched.size(); ++t) {
        const QString &key = touched.at(t).first;
        const int j = keys.indexOf(key);
        if (j >= 0) {
            places.append(qMakePair(j, t));
            continue;
        }
        // gone from the list
        const int i = m_rowKeys.indexOf(key);
        if (i < 0)
            continue;
        if (tell)
            beginRemoveRows(QModelIndex(), i, i);
        m_html.remove(key);
        m_rooms.removeAt(i);
        m_rowKeys.remove(i);
        if (tell)
            endRemoveRows();
    }
    std::sort(places.begin(), places.end());
    QVector<QPair<int, QVariant>> changed;
    for (const auto &p : places) {
        const int j = p.first;
        const QString &key = keys.at(j);
        const QVariant &row = touched.at(p.second).second;
        const int dest = j == 0 ? 0 : m_rowKeys.indexOf(keys.at(j - 1)) + 1;
        const int at = m_rowKeys.indexOf(key);
        if (at < 0) {
            if (tell)
                beginInsertRows(QModelIndex(), dest, dest);
            m_rooms.insert(dest, row);
            m_rowKeys.insert(dest, key);
            if (tell)
                endInsertRows();
            continue;
        }
        if (at != dest) {
            if (tell)
                beginMoveRows(QModelIndex(), at, at, QModelIndex(), dest);
            const QVariant was = m_rooms.takeAt(at);
            m_rowKeys.remove(at);
            const int to = at < dest ? dest - 1 : dest;
            m_rooms.insert(to, was);
            m_rowKeys.insert(to, key);
            if (tell)
                endMoveRows();
        }
        changed.append(qMakePair(j, row));
    }
    if (m_rowKeys != keys) {
        // not what the caller promised: the whole compare from here
        qWarning("rooms: touched rows did not give the new order");
        QHash<QString, QVariant> rows;
        for (int i = 0; i < m_rowKeys.size(); ++i)
            rows.insert(m_rowKeys.at(i), m_rooms.at(i));
        for (const auto &t : touched)
            rows.insert(t.first, t.second);
        QVariantList all;
        for (const QString &key : keys)
            all.append(rows.value(key));
        setRows(all, keys);
        return;
    }
    // the fresh fields, and only the roles that changed told
    for (const auto &c : changed) {
        const QVariant was = m_rooms.at(c.first);
        if (was == c.second)
            continue;
        m_rooms[c.first] = c.second;
        if (!tell)
            continue;
        bool all = false;
        const QVector<int> roles = changedRoles(was.toMap(), c.second.toMap(), &all);
        if (all || !roles.isEmpty())
            emit dataChanged(index(c.first), index(c.first), roles);
    }
    if (!tell && m_html.size() > 2 * m_rooms.size() + 16)
        m_html.clear();
    if (before != m_rooms.size())
        emit countChanged();
}
