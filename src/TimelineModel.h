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
#include <QSet>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>

// The open room's timeline, newest first (row 0), for a list laid out
// bottom to top. Rows are the core's item objects; media paths are kept
// across snapshots because the core only reports them once.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // the height custom emoji are drawn at in a message, in pixels
    Q_PROPERTY(int emoteSize READ emoteSize WRITE setEmoteSize NOTIFY emoteSizeChanged)

public:
    explicit TimelineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVariantList &items);
    void setMediaPath(const QString &eventId, bool thumb, const QString &path);
    // a custom emoji's picture arrived in the cache
    void setEmotePath(const QString &mxc, const QString &path);
    // the cache was emptied: ask again when shown
    void clearEmotes();
    int emoteSize() const { return m_emoteSize; }
    void setEmoteSize(int size);
    void clear();
    Q_INVOKABLE QVariantMap get(int row) const;
    // the cached full file for an event, or an empty string
    Q_INVOKABLE QString mediaPath(const QString &eventId) const { return m_media.value(eventId); }

signals:
    void countChanged();
    void mediaChanged(const QString &eventId);
    void emoteSizeChanged();
    // a custom emoji (mxc address) is shown that is not in the cache yet
    void emoteWanted(const QString &mxc);

private:
    QVariantMap withMedia(QVariantMap item) const;
    QString renderHtml(const QString &html) const;
    QVariantList renderReactions(const QVariantList &reactions) const;
    void askEmotes(const QVariantMap &item);
    bool usesEmote(const QVariantMap &item, const QString &mxc) const;
    QVariantList emotesOf(const QVariantMap &item) const;

    QVariantList m_items;
    QHash<QString, QString> m_media;
    QHash<QString, QString> m_thumbs;
    QHash<int, QByteArray> m_roles;
    struct Emote { QString path; QSize size; };
    QHash<QString, Emote> m_emotes;
    QSet<QString> m_emotesAsked;
    int m_emoteSize = 32;
};

#endif
