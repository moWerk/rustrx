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
#include "TimelineModel.h"

TimelineModel::TimelineModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "id", "kind", "event_id", "txn_id", "sender", "sender_name", "is_own", "ts",
                            "editable", "highlight", "send_state", "type", "body", "html", "edited",
                            "reactions", "reply_to", "thread_root", "mime", "size", "w", "h", "has_thumb",
                            "filename", "duration", "geo_uri", "media_path", "thumb_path", "read_by" };
    int role = Qt::UserRole + 1;
    for (const char *n : names)
        m_roles.insert(role++, QByteArray(n));
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();
    const QVariantMap item = m_items.at(index.row()).toMap();
    const QByteArray name = m_roles.value(role);
    if (name == "media_path")
        return m_media.value(item.value("event_id").toString());
    if (name == "thumb_path")
        return m_thumbs.value(item.value("event_id").toString());
    return item.value(QString::fromLatin1(name));
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return m_roles;
}

QVariantMap TimelineModel::withMedia(QVariantMap item) const
{
    const QString id = item.value("event_id").toString();
    item.insert("media_path", m_media.value(id));
    item.insert("thumb_path", m_thumbs.value(id));
    return item;
}

QVariantMap TimelineModel::get(int row) const
{
    return row >= 0 && row < m_items.size() ? withMedia(m_items.at(row).toMap()) : QVariantMap();
}

// The core sends the whole timeline, oldest first. Reverse it; when only
// rows were added at the new end (the common case: a message arrived or
// was sent), insert them, else reset.
void TimelineModel::setItems(const QVariantList &items)
{
    QVariantList reversed;
    reversed.reserve(items.size());
    for (int i = items.size() - 1; i >= 0; --i)
        reversed.append(items.at(i));

    const int added = reversed.size() - m_items.size();
    bool appendOnly = added > 0 && !m_items.isEmpty();
    for (int i = 0; appendOnly && i < m_items.size(); ++i)
        appendOnly = reversed.at(i + added).toMap().value("id") == m_items.at(i).toMap().value("id");
    if (appendOnly) {
        beginInsertRows(QModelIndex(), 0, added - 1);
        for (int i = added - 1; i >= 0; --i)
            m_items.prepend(reversed.at(i));
        endInsertRows();
        // existing rows may have changed too (send state, reactions, edits)
        emit dataChanged(index(added), index(m_items.size() - 1));
        emit countChanged();
        return;
    }
    bool sameRows = reversed.size() == m_items.size();
    for (int i = 0; sameRows && i < reversed.size(); ++i)
        sameRows = reversed.at(i).toMap().value("id") == m_items.at(i).toMap().value("id");
    if (sameRows) {
        m_items = reversed;
        if (!m_items.isEmpty())
            emit dataChanged(index(0), index(m_items.size() - 1));
        return;
    }
    beginResetModel();
    m_items = reversed;
    endResetModel();
    emit countChanged();
}

void TimelineModel::setMediaPath(const QString &eventId, bool thumb, const QString &path)
{
    (thumb ? m_thumbs : m_media).insert(eventId, path);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).toMap().value("event_id").toString() == eventId) {
            emit dataChanged(index(i), index(i));
            break;
        }
    }
}

void TimelineModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}
