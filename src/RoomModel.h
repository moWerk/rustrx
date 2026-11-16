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
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// A list of rooms as the core sorts them (invitations, favourites, then by
// activity). Each row is the room's JSON object; roles map to its keys.
// name_html and preview_html are the name and the last message with their
// emoji as Twemoji pictures, made when a row comes or changes.
class RoomModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // the height of Twemoji in the name and in the preview, in pixels
    Q_PROPERTY(int nameEmojiSize READ nameEmojiSize WRITE setNameEmojiSize NOTIFY nameEmojiSizeChanged)
    Q_PROPERTY(int previewEmojiSize READ previewEmojiSize WRITE setPreviewEmojiSize NOTIFY previewEmojiSizeChanged)
    // the preview shows more than one line: its breaks are kept
    Q_PROPERTY(bool previewMultiLine READ previewMultiLine WRITE setPreviewMultiLine NOTIFY previewMultiLineChanged)

public:
    explicit RoomModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRooms(const QVariantList &rooms);
    Q_INVOKABLE QVariantMap get(int row) const;

    int nameEmojiSize() const { return m_nameEmojiSize; }
    void setNameEmojiSize(int size);
    int previewEmojiSize() const { return m_previewEmojiSize; }
    void setPreviewEmojiSize(int size);
    bool previewMultiLine() const { return m_previewMultiLine; }
    void setPreviewMultiLine(bool multi);

signals:
    void countChanged();
    void nameEmojiSizeChanged();
    void previewEmojiSizeChanged();
    void previewMultiLineChanged();

private:
    struct Html { QString name, preview; };
    QVector<int> changedRoles(const QVariantMap &from, const QVariantMap &to) const;
    QString nameHtml(const QVariantMap &room) const;
    QString previewHtml(const QVariantMap &room) const;
    // made anew for every row, one role told
    void rerender(bool name);

    QVariantList m_rooms;
    QHash<int, QByteArray> m_roles;
    // the map key of each role, and the role of each key
    QHash<int, QString> m_keys;
    QHash<QString, int> m_roleOf;
    int m_nameHtmlRole, m_previewHtmlRole;
    // by room id
    QHash<QString, Html> m_html;
    // until QML sets them: the usual text sizes at a scale of 1
    int m_nameEmojiSize = 42;
    int m_previewEmojiSize = 27;
    bool m_previewMultiLine = false;
};

#endif
