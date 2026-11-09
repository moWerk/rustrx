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
#ifndef ROOMMODEL_H
#define ROOMMODEL_H

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

// A list of rooms as the core sorts them (invitations, favourites, then by
// activity). Each row is the room's JSON object; roles map to its keys.
class RoomModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit RoomModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRooms(const QVariantList &rooms);
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    QVariantList m_rooms;
    QHash<int, QByteArray> m_roles;
};

#endif
