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

RoomModel::RoomModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "room_id", "name", "topic", "alias", "is_dm", "is_fav", "is_low", "unread",
                            "notifications", "highlights", "ts", "state", "encrypted", "section" };
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
void RoomModel::setRooms(const QVariantList &rooms)
{
    bool sameOrder = rooms.size() == m_rooms.size();
    for (int i = 0; sameOrder && i < rooms.size(); ++i)
        sameOrder = rooms.at(i).toMap().value("room_id") == m_rooms.at(i).toMap().value("room_id");
    if (sameOrder) {
        m_rooms = rooms;
        if (!rooms.isEmpty())
            emit dataChanged(index(0), index(rooms.size() - 1));
        return;
    }
    beginResetModel();
    m_rooms = rooms;
    endResetModel();
    emit countChanged();
}
