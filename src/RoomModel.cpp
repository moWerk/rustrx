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

#include <QSet>

RoomModel::RoomModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "room_id", "name", "topic", "alias", "is_dm", "is_fav", "is_low", "unread",
                            "notifications", "highlights", "ts", "state", "encrypted", "section",
                            "avatar", "avatar_v", "has_avatar", "preview", "preview_sender",
                            "muted", "own_read", "replaced_by", "in_call",
                            "is_space", "join_rule", "can_join_rules", "can_manage_children", "children", "parents",
                            "name_html", "preview_html" };
    int role = Qt::UserRole + 1;
    for (const char *n : names) {
        m_roles.insert(role, QByteArray(n));
        m_keys.insert(role, QString::fromLatin1(n));
        m_roleOf.insert(QString::fromLatin1(n), role);
        ++role;
    }
    m_nameHtmlRole = m_roleOf.value(QStringLiteral("name_html"));
    m_previewHtmlRole = m_roleOf.value(QStringLiteral("preview_html"));
}

int RoomModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rooms.size();
}

QVariant RoomModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rooms.size())
        return QVariant();
    const QVariantMap room = m_rooms.at(index.row()).toMap();
    if (role == m_nameHtmlRole)
        return m_html.value(room.value(QStringLiteral("room_id")).toString()).name;
    if (role == m_previewHtmlRole)
        return m_html.value(room.value(QStringLiteral("room_id")).toString()).preview;
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

// the roles whose values differ between two versions of a room; no roles
// (all of them) when a key without a role changed
QVector<int> RoomModel::changedRoles(const QVariantMap &from, const QVariantMap &to) const
{
    QVector<int> roles;
    bool unknown = false;
    auto note = [&](const QString &key) {
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
    };
    for (auto it = from.constBegin(); it != from.constEnd(); ++it)
        if (it.value() != to.value(it.key()))
            note(it.key());
    for (auto it = to.constBegin(); it != to.constEnd(); ++it)
        if (!from.contains(it.key()))
            note(it.key());
    return unknown ? QVector<int>() : roles;
}

// the name with its emoji as pictures (& and < escaped, as the QML did)
QString RoomModel::nameHtml(const QVariantMap &room) const
{
    return Twemoji::inHtml(StyledText::escaped(room.value(QStringLiteral("name")).toString(), false), m_nameEmojiSize);
}

// the last message likewise: in one line its white space collapses first,
// so the pictures' attributes keep their spaces
QString RoomModel::previewHtml(const QVariantMap &room) const
{
    QString b = StyledText::escaped(room.value(QStringLiteral("preview")).toString(), false);
    if (m_previewMultiLine)
        b.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    else
        b = StyledText::oneLine(b);
    return Twemoji::inHtml(b, m_previewEmojiSize);
}

void RoomModel::rerender(bool name)
{
    for (const QVariant &v : m_rooms) {
        const QVariantMap room = v.toMap();
        Html &h = m_html[room.value(QStringLiteral("room_id")).toString()];
        if (name)
            h.name = nameHtml(room);
        else
            h.preview = previewHtml(room);
    }
    if (!m_rooms.isEmpty())
        emit dataChanged(index(0), index(m_rooms.size() - 1), { name ? m_nameHtmlRole : m_previewHtmlRole });
}

void RoomModel::setNameEmojiSize(int size)
{
    if (size == m_nameEmojiSize || size <= 0)
        return;
    m_nameEmojiSize = size;
    emit nameEmojiSizeChanged();
    rerender(true);
}

void RoomModel::setPreviewEmojiSize(int size)
{
    if (size == m_previewEmojiSize || size <= 0)
        return;
    m_previewEmojiSize = size;
    emit previewEmojiSizeChanged();
    rerender(false);
}

void RoomModel::setPreviewMultiLine(bool multi)
{
    if (multi == m_previewMultiLine)
        return;
    m_previewMultiLine = multi;
    emit previewMultiLineChanged();
    rerender(false);
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
            m_html.remove(roomIdOf(m_rooms.at(i)));
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
            // ready before the view asks for it
            const QVariantMap room = rooms.at(i).toMap();
            m_html.insert(id, Html { nameHtml(room), previewHtml(room) });
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
    // and roles that changed (every row on every update rebound every
    // delegate, and a range rebound the unchanged rows inside it)
    QVector<QPair<int, QVector<int>>> changed;
    for (int i = 0; i < rooms.size(); ++i) {
        if (m_rooms.at(i) == rooms.at(i))
            continue;
        const QVariantMap from = m_rooms.at(i).toMap(), to = rooms.at(i).toMap();
        changed.append(qMakePair(i, changedRoles(from, to)));
        Html &h = m_html[roomIdOf(rooms.at(i))];
        if (from.value(QStringLiteral("name")) != to.value(QStringLiteral("name")))
            h.name = nameHtml(to);
        if (from.value(QStringLiteral("preview")) != to.value(QStringLiteral("preview")))
            h.preview = previewHtml(to);
    }
    m_rooms = rooms;
    for (const auto &c : changed)
        emit dataChanged(index(c.first), index(c.first), c.second);
    if (before != rooms.size())
        emit countChanged();
}
