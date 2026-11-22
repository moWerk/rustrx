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
// emoji as Twemoji pictures, made when a view first asks for a row's;
// section is computed from the state (the invitations' heading).
class RoomModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // the height of Twemoji in the name, in pixels; one for every list
    Q_PROPERTY(int nameEmojiSize READ nameEmojiSize WRITE setNameEmojiSize NOTIFY nameEmojiSizeChanged)
    // the preview shows more than one line: its breaks are kept
    Q_PROPERTY(bool previewMultiLine READ previewMultiLine WRITE setPreviewMultiLine NOTIFY previewMultiLineChanged)

public:
    explicit RoomModel(QObject *parent = nullptr);
    ~RoomModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Rows equal to the ones shown should be the same QVariant (RoomLists
    // passes the previous one on): they compare by pointer then.
    void setRooms(const QVariantList &rooms);
    Q_INVOKABLE QVariantMap get(int row) const;
    // a view, a Connections or a binding follows this list; without one,
    // setRooms takes the rows as they are, with no signals to work out
    bool watched() const;

    int nameEmojiSize() const;
    void setNameEmojiSize(int size);
    bool previewMultiLine() const { return m_previewMultiLine; }
    void setPreviewMultiLine(bool multi);

signals:
    void countChanged();
    void nameEmojiSizeChanged();
    void previewMultiLineChanged();

private:
    // what each was made from: made again when the text or the size differs
    struct Html {
        QString nameFrom, name, previewFrom, preview;
        int nameSize = -1;
        int previewMulti = -1;
    };
    QVector<int> changedRoles(const QVariantMap &from, const QVariantMap &to, bool *all) const;
    QString nameHtml(const QVariantMap &room) const;
    QString previewHtml(const QVariantMap &room) const;
    const Html &html(int row, bool name) const;
    // every shown row, one role told
    void rerender(bool name);
    void reorder(const QVariantList &rooms, const QVector<QString> &keys);

    QVariantList m_rooms;
    // each row's key (rowKey), beside it
    QVector<QString> m_rowKeys;
    QHash<int, QByteArray> m_roles;
    // the map key of each role, and the role of each key
    QHash<int, QString> m_keys;
    QHash<QString, int> m_roleOf;
    int m_nameHtmlRole, m_previewHtmlRole, m_sectionRole;
    // by row key, filled as views ask
    mutable QHash<QString, Html> m_html;
    bool m_previewMultiLine = false;
};

#endif
