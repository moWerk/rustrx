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

#include <QSet>

RoomModel::RoomModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "room_id", "name", "topic", "alias", "is_dm", "is_fav", "is_low", "unread",
                            "notifications", "highlights", "ts", "state", "encrypted", "section",
                            "avatar", "avatar_v", "has_avatar", "preview", "preview_sender",
                            "muted", "own_read", "replaced_by", "in_call",
                            "is_space", "join_rule", "can_join_rules", "can_manage_children", "children", "parents" };
    int role = Qt::UserRole + 1;
    for (const char *n : names)
        m_roles.insert(role++, QByteArray(n));
}

int RoomModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rooms.size();
}

QVariant RoomModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rooms.size())
        return QVariant();
    return m_rooms.at(index.row()).toMap().value(QString::fromLatin1(m_roles.value(role)));
}

QHash<int, QByteArray> RoomModel::roleNames() const
{
    return m_roles;
}

QVariantMap RoomModel::get(int row) const
{
    return row >= 0 && row < m_rooms.size() ? m_rooms.at(row).toMap() : QVariantMap();
}

// Same rows in the same order: update in place, so the list does not jump
// under the finger. Anything else: reset.
static QString roomIdOf(const QVariant &v)
{
    return v.toMap().value("room_id").toString();
}

// Removes, inserts and moves instead of a reset, so the list view keeps its
// place (a reset throws it back to the top, after leaving a room or when a
// room moves up while the user is reading another one).
void RoomModel::setRooms(const QVariantList &rooms)
{
    const int before = m_rooms.size();
    QSet<QString> wanted;
    for (const QVariant &v : rooms)
        wanted.insert(roomIdOf(v));

    for (int i = m_rooms.size() - 1; i >= 0; --i) {
        if (!wanted.contains(roomIdOf(m_rooms.at(i)))) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rooms.removeAt(i);
            endRemoveRows();
        }
    }

    for (int i = 0; i < rooms.size(); ++i) {
        const QString id = roomIdOf(rooms.at(i));
        if (i < m_rooms.size() && roomIdOf(m_rooms.at(i)) == id)
            continue;
        int j = -1;
        for (int k = i + 1; k < m_rooms.size(); ++k) {
            if (roomIdOf(m_rooms.at(k)) == id) {
                j = k;
                break;
            }
        }
        if (j < 0) {
            beginInsertRows(QModelIndex(), i, i);
            m_rooms.insert(i, rooms.at(i));
            endInsertRows();
        } else {
            beginMoveRows(QModelIndex(), j, j, QModelIndex(), i);
            m_rooms.move(j, i);
            endMoveRows();
        }
    }

    // same order now; take the fresh fields, and tell only about the rows
    // that changed (every row on every update rebound every delegate)
    int first = -1, last = -1;
    for (int i = 0; i < rooms.size(); ++i) {
        if (m_rooms.at(i) != rooms.at(i)) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    m_rooms = rooms;
    if (first >= 0)
        emit dataChanged(index(first), index(last));
    if (before != rooms.size())
        emit countChanged();
}
