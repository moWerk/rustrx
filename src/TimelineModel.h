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
#ifndef TIMELINEMODEL_H
#define TIMELINEMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

// The open room's timeline, newest first (row 0), for a list laid out
// bottom to top. Rows are the core's item objects; media paths are kept
// across snapshots because the core only reports them once.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit TimelineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVariantList &items);
    void setMediaPath(const QString &eventId, bool thumb, const QString &path);
    void clear();
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    QVariantMap withMedia(QVariantMap item) const;

    QVariantList m_items;
    QHash<QString, QString> m_media;
    QHash<QString, QString> m_thumbs;
    QHash<int, QByteArray> m_roles;
};

#endif
