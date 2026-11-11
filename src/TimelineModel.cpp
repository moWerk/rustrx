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

#include <QSet>

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

// The core sends the whole timeline, oldest first; the model holds it
// newest first. Rows are matched by their unique id: removed rows go,
// new ones are inserted where they belong, moved ones move, and only rows
// whose content changed are reported. A page of older history therefore
// arrives as an insertion at the far end and the view stays where it is
// (a reset used to throw a reader back to the newest message).
void TimelineModel::setItems(const QVariantList &items)
{
    QVariantList target;
    target.reserve(items.size());
    for (int i = items.size() - 1; i >= 0; --i)
        target.append(items.at(i));

    auto idOf = [](const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString(); };

    const int before = m_items.size();
    QSet<QString> wanted;
    for (const QVariant &v : target)
        wanted.insert(idOf(v));

    // 1. rows that are gone, from the end so the indices stay valid
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!wanted.contains(idOf(m_items.at(i)))) {
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
        }
    }

    // 2. walk the target order: keep, move up, or insert
    for (int i = 0; i < target.size(); ++i) {
        const QString id = idOf(target.at(i));
        if (i < m_items.size() && idOf(m_items.at(i)) == id) {
            if (m_items.at(i) != target.at(i)) {
                m_items[i] = target.at(i);
                emit dataChanged(index(i), index(i));
            }
            continue;
        }
        int from = -1;
        for (int j = i + 1; j < m_items.size(); ++j) {
            if (idOf(m_items.at(j)) == id) {
                from = j;
                break;
            }
        }
        if (from >= 0) {
            beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
            m_items.move(from, i);
            endMoveRows();
            if (m_items.at(i) != target.at(i)) {
                m_items[i] = target.at(i);
                emit dataChanged(index(i), index(i));
            }
        } else {
            beginInsertRows(QModelIndex(), i, i);
            m_items.insert(i, target.at(i));
            endInsertRows();
        }
    }
    if (m_items.size() != before)
        emit countChanged();
}

void TimelineModel::setMediaPath(const QString &eventId, bool thumb, const QString &path)
{
    (thumb ? m_thumbs : m_media).insert(eventId, path);
    emit mediaChanged(eventId);
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
