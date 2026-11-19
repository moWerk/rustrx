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
#include <QVector>
#include "Keys.h"

// The open room's timeline, newest first (row 0), for a list laid out
// bottom to top. Rows are the core's item objects; media paths are kept
// across snapshots because the core only reports them once. body_html,
// caption_html and thread_html are a row's plain texts as StyledText, and
// each reaction carries its key as one ("label"), all made once per row.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // the height custom emoji are drawn at in a message, in pixels
    Q_PROPERTY(int emoteSize READ emoteSize WRITE setEmoteSize NOTIFY emoteSizeChanged)
    // the height of Twemoji in the small lines (reactions, a thread's latest reply)
    Q_PROPERTY(int smallEmojiSize READ smallEmojiSize WRITE setSmallEmojiSize NOTIFY smallEmojiSizeChanged)

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
    int smallEmojiSize() const { return m_smallEmojiSize; }
    void setSmallEmojiSize(int size);
    void clear();
    Q_INVOKABLE QVariantMap get(int row) const;
    // the row of an event, or -1 while it is not loaded (a reply's original)
    Q_INVOKABLE int rowOfEvent(const QString &eventId) const;
    // the first row of a kind ("readmarker"), or -1; without get()'s copy
    // of every row (the room page asks on each change of the count)
    Q_INVOKABLE int rowOfKind(const QString &kind) const;
    // the message a reader has read up to when the row was on screen: the
    // row's own, or the next older one (past dates, the unread line and
    // messages still being sent); empty when there is none
    Q_INVOKABLE QString seenEventId(int row) const;
    // a row's message with its spoilers' text shown (they are covered in
    // the html role), for when the reader taps one
    Q_INVOKABLE QString spoilersShown(int row) const;
    // the reader opened this row's spoilers: they stay open when its
    // delegate comes back after scrolling (until another room opens)
    Q_INVOKABLE bool spoilersOpen(int row) const;
    // the link a covered spoiler is
    static constexpr const char *kSpoilerLink = "rustrx:spoiler";
    // the cached full file for an event, or an empty string
    Q_INVOKABLE QString mediaPath(const QString &eventId) const { return m_media.value(eventId); }

signals:
    void countChanged();
    void mediaChanged(const QString &eventId);
    void emoteSizeChanged();
    void smallEmojiSizeChanged();
    // a custom emoji (mxc address) is shown that is not in the cache yet
    void emoteWanted(const QString &mxc);

private:
    // a row's rendered parts, made when first asked for and kept until the
    // row or what it shows changes
    struct Rendered {
        bool hasHtml = false, hasReply = false, hasReactions = false, hasEmotes = false;
        bool hasBody = false, hasCaption = false, hasThread = false;
        QString html;
        QString body, caption, thread;
        QVariant reply;
        QVariantList reactions;
        QVariantList emotes;
    };
    Rendered &rendered(const QVariantMap &item) const;
    QString cachedHtml(const QVariantMap &item) const;
    QVariant cachedReply(const QVariantMap &item) const;
    QVariantList cachedReactions(const QVariantMap &item) const;
    QVariantList cachedEmotes(const QVariantMap &item) const;
    QString cachedBody(const QVariantMap &item) const;
    QString cachedCaption(const QVariantMap &item) const;
    QString cachedThread(const QVariantMap &item) const;
    void forget(const QVariant &item);
    QVector<int> changedRoles(const QVariantMap &from, const QVariantMap &to) const;
    QVariantMap withMedia(QVariantMap item) const;
    QString renderHtml(const QString &html, int emojiHeight = 0, bool spoilersShown = false) const;
    QVariantList renderReactions(const QVariantList &reactions) const;
    void askEmotes(const QVariantMap &item);
    bool usesEmote(const QVariantMap &item, const QString &mxc) const;
    QVariantList emotesOf(const QVariantMap &item) const;
    QVariant renderReply(const QVariant &reply) const;

    QVariantList m_items;
    QHash<QString, QString> m_media;
    QHash<QString, QString> m_thumbs;
    QHash<int, QByteArray> m_roles;
    // the map key of each role, and the role of each key
    QHash<int, QString> m_keys;
    QHash<QString, int> m_roleOf;
    int m_htmlRole, m_reactionsRole, m_replyRole, m_emotesRole, m_mediaRole, m_thumbRole;
    int m_bodyHtmlRole, m_captionHtmlRole, m_threadHtmlRole;
    mutable QHash<QString, Rendered> m_rendered;
    mutable QSet<QString> m_spoilersOpen;   // event ids whose spoilers the reader opened
    // the emoji cache was emptied: the next snapshot asks for every row's
    bool m_askAll = false;
    struct Emote { QString path; QSize size; };
    QHash<QString, Emote> m_emotes;
    QSet<QString> m_emotesAsked;
    int m_emoteSize = Keys::kDefaultEmoteSize;
    // until QML sets it: about the extra small text at the usual ratio
    int m_smallEmojiSize = Keys::kDefaultEmoteSize * 3 / 4;
};

#endif
