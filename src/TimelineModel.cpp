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

#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

namespace {

const QRegularExpression &imgTag()
{
    static const QRegularExpression re(QStringLiteral("<img\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

// an attribute's value in a tag, quoted either way
QString attribute(const QString &tag, const QString &name)
{
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')").arg(name),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(tag);
    if (!m.hasMatch())
        return QString();
    return m.capturedStart(1) >= 0 ? m.captured(1) : m.captured(2);
}

}

TimelineModel::TimelineModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const char *names[] = { "id", "kind", "event_id", "txn_id", "sender", "sender_name", "is_own", "ts",
                            "editable", "highlight", "send_state", "type", "body", "html", "edited",
                            "reactions", "reply_to", "thread_root", "mime", "size", "w", "h", "has_thumb",
                            "filename", "duration", "geo_uri", "media_path", "thumb_path", "read_by",
                            "caption", "shield", "answers", "max_selections", "ended", "upload_done", "upload_total",
                            "emotes" };
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
    if (name == "html")
        return renderHtml(item.value("html").toString());
    if (name == "reactions")
        return renderReactions(item.value("reactions").toList());
    if (name == "emotes")
        return emotesOf(item);
    if (name == "reply_to")
        return renderReply(item.value("reply_to"));
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
    if (item.contains("html"))
        item.insert("html", renderHtml(item.value("html").toString()));
    if (item.contains("reactions"))
        item.insert("reactions", renderReactions(item.value("reactions").toList()));
    if (item.contains("reply_to"))
        item.insert("reply_to", renderReply(item.value("reply_to")));
    return item;
}

// A message's HTML as Text.StyledText (Qt 5.6) takes it. The spec's tags
// that StyledText knows pass, without attributes (with one it would not
// know them either); the others become the nearest it can draw, or only
// their text: em is italic, a quote italic in its own paragraph, a line a
// row of dashes, table cells a " | " row, colours (data-mx-color, #rrggbb)
// a font colour. A spoiler is not shown at all, only that there is one.
// Links keep only the spec's schemes (and matrix:). Custom emoji (an <img>
// with an mxc:// address) become the cached picture at a little more than
// the text's height, their name until it is there; every other picture
// becomes its name, so a message cannot make the phone fetch from any
// address. A reply's quoted fallback (<mx-reply>) goes.
QString TimelineModel::renderHtml(const QString &html) const
{
    if (html.isEmpty())
        return html;
    static const QRegularExpression reply(QStringLiteral("<mx-reply>.*</mx-reply>"),
                                          QRegularExpression::CaseInsensitiveOption
                                          | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tagRe(QStringLiteral("<(/?)([a-zA-Z][a-zA-Z0-9-]*)([^>]*)>"));
    static const QRegularExpression colourRe(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    static const QRegularExpression schemeRe(QStringLiteral("^(https?|ftp|mailto|magnet|matrix):"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression entityRe(QStringLiteral("&#(x[0-9a-fA-F]+|[0-9]+);|&apos;"));
    static const QSet<QString> plain = {
        QStringLiteral("b"), QStringLiteral("strong"), QStringLiteral("i"), QStringLiteral("u"),
        QStringLiteral("p"), QStringLiteral("pre"), QStringLiteral("ul"), QStringLiteral("ol"),
        QStringLiteral("li"), QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6") };

    QString in = html;
    in.remove(reply);

    // the text between tags: numeric entities decoded (StyledText knows
    // only the named few), then escaped again where needed
    auto text = [&](const QStringRef &t) {
        QString seg = t.toString();
        if (!seg.contains(QLatin1Char('&')))
            return seg;
        QString res;
        int at = 0;
        QRegularExpressionMatchIterator e = entityRe.globalMatch(seg);
        while (e.hasNext()) {
            const QRegularExpressionMatch m = e.next();
            res += seg.midRef(at, m.capturedStart() - at);
            at = m.capturedEnd();
            uint code = '\'';
            if (m.capturedLength(1)) {
                const QString n = m.captured(1);
                code = n.startsWith(QLatin1Char('x')) ? n.mid(1).toUInt(nullptr, 16) : n.toUInt();
            }
            if (code == '<') res += QLatin1String("&lt;");
            else if (code == '>') res += QLatin1String("&gt;");
            else if (code == '&') res += QLatin1String("&amp;");
            else if (code > 0 && code <= 0x10FFFF) res += QString::fromUcs4(&code, 1);
        }
        res += seg.midRef(at);
        return res;
    };

    struct Open { QString name; QString close; bool hides; };
    QVector<Open> stack;
    int hidden = 0;        // inside a spoiler: its text is not shown
    QString out;
    auto emit_ = [&](const QString &s) { if (!hidden) out += s; };
    auto closeTop = [&]() {
        const Open o = stack.takeLast();
        if (o.hides) --hidden;
        emit_(o.close);
    };

    int last = 0;
    QRegularExpressionMatchIterator it = tagRe.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        emit_(text(in.midRef(last, m.capturedStart() - last)));
        last = m.capturedEnd();
        const bool closing = m.capturedLength(1) > 0;
        const QString name = m.captured(2).toLower();
        const QString tag = m.captured();

        if (closing) {
            int i = stack.size() - 1;
            while (i >= 0 && stack.at(i).name != name)
                --i;
            if (i < 0)
                continue;   // a close without its open: dropped
            while (stack.size() > i)
                closeTop();
            continue;
        }

        // the ones without content
        if (name == QLatin1String("br")) { emit_(QStringLiteral("<br>")); continue; }
        if (name == QLatin1String("hr")) { emit_(QStringLiteral("<br>―――――<br>")); continue; }
        if (name == QLatin1String("img")) {
            const QString src = attribute(tag, QStringLiteral("src"));
            const auto e = m_emotes.constFind(src);
            if (src.startsWith(QLatin1String("mxc://")) && e != m_emotes.constEnd()) {
                const int h = m_emoteSize;
                const int w = e->size.height() > 0 ? qRound(h * qreal(e->size.width()) / e->size.height()) : h;
                emit_(QStringLiteral("<img src=\"file://%1\" width=\"%2\" height=\"%3\" align=\"middle\">")
                          .arg(e->path.toHtmlEscaped()).arg(w).arg(h));
            } else {
                QString alt = attribute(tag, QStringLiteral("alt"));
                if (alt.isEmpty())
                    alt = attribute(tag, QStringLiteral("title"));
                emit_(alt.replace(QLatin1Char('<'), QLatin1String("&lt;")));
            }
            continue;
        }

        Open o { name, QString(), false };
        QString open;
        if (plain.contains(name)) {
            // the tags themselves, bare
            open = QLatin1Char('<') + name + QLatin1Char('>');
            o.close = QStringLiteral("</") + name + QLatin1Char('>');
        } else if (name == QLatin1String("em")) {
            open = QStringLiteral("<i>"); o.close = QStringLiteral("</i>");
        } else if (name == QLatin1String("blockquote")) {
            open = QStringLiteral("<p><i>"); o.close = QStringLiteral("</i></p>");
        } else if (name == QLatin1String("a")) {
            const QString href = attribute(tag, QStringLiteral("href"));
            if (schemeRe.match(href).hasMatch()) {
                open = QStringLiteral("<a href=\"%1\">").arg(QString(href).replace(QLatin1Char('"'), QLatin1String("%22")));
                o.close = QStringLiteral("</a>");
            }
        } else if (name == QLatin1String("span") || name == QLatin1String("font")) {
            if (tag.contains(QLatin1String("data-mx-spoiler"), Qt::CaseInsensitive)) {
                //: in place of a hidden part of a message
                //% "(spoiler)"
                emit_(QStringLiteral("<i>") + qtTrId("rustrx-la-spoiler") + QStringLiteral("</i>"));
                o.hides = true;
                ++hidden;
            } else {
                QString colour = attribute(tag, QStringLiteral("data-mx-color"));
                if (colour.isEmpty() && name == QLatin1String("font"))
                    colour = attribute(tag, QStringLiteral("color"));
                if (colourRe.match(colour).hasMatch()) {
                    open = QStringLiteral("<font color=\"%1\">").arg(colour);
                    o.close = QStringLiteral("</font>");
                }
            }
        } else if (name == QLatin1String("td") || name == QLatin1String("th")) {
            o.close = QStringLiteral(" | ");
        } else if (name == QLatin1String("tr") || name == QLatin1String("div")) {
            o.close = QStringLiteral("<br>");
        } else if (name == QLatin1String("caption") || name == QLatin1String("summary")) {
            open = QStringLiteral("<b>"); o.close = QStringLiteral("</b><br>");
        }
        // anything else (code, del, s, sup, sub, table, details, unknown):
        // its text only
        emit_(open);
        stack.append(o);
    }
    emit_(text(in.midRef(last)));
    while (!stack.isEmpty())
        closeTop();
    return out;
}

// A reaction with a custom emoji has its mxc address as the key (MSC4027):
// it gets "emote", the cached picture, when there is one.
QVariantList TimelineModel::renderReactions(const QVariantList &reactions) const
{
    QVariantList out = reactions;
    for (QVariant &v : out) {
        QVariantMap r = v.toMap();
        const QString key = r.value("key").toString();
        if (!key.startsWith(QLatin1String("mxc://")))
            continue;
        const auto e = m_emotes.constFind(key);
        r.insert("emote", e != m_emotes.constEnd() ? QStringLiteral("file://") + e->path : QString());
        v = r;
    }
    return out;
}

// the quoted original of a reply: its HTML rendered like a message's
QVariant TimelineModel::renderReply(const QVariant &reply) const
{
    if (!reply.isValid())
        return reply;
    QVariantMap r = reply.toMap();
    if (r.contains("html"))
        r.insert("html", renderHtml(r.value("html").toString()));
    return r;
}

// the custom emoji an item shows that are not cached: asked for once
void TimelineModel::askEmotes(const QVariantMap &item)
{
    QStringList wanted;
    const QString html = item.value("html").toString()
                         + item.value("reply_to").toMap().value("html").toString();
    if (html.contains(QLatin1String("mxc://"))) {
        QRegularExpressionMatchIterator it = imgTag().globalMatch(html);
        while (it.hasNext()) {
            const QString src = attribute(it.next().captured(), QStringLiteral("src"));
            if (src.startsWith(QLatin1String("mxc://")))
                wanted << src;
        }
    }
    for (const QVariant &v : item.value("reactions").toList()) {
        const QString key = v.toMap().value("key").toString();
        if (key.startsWith(QLatin1String("mxc://")))
            wanted << key;
    }
    for (const QString &mxc : wanted) {
        if (m_emotes.contains(mxc) || m_emotesAsked.contains(mxc))
            continue;
        m_emotesAsked.insert(mxc);
        emit emoteWanted(mxc);
    }
}

// the custom emoji in a message and its reactions, once each, with their
// shortcodes: "Add to my emoji" offers them
QVariantList TimelineModel::emotesOf(const QVariantMap &item) const
{
    QVariantList out;
    QSet<QString> seen;
    auto add = [&](const QString &url, QString name) {
        if (!url.startsWith(QLatin1String("mxc://")) || seen.contains(url))
            return;
        seen.insert(url);
        name.remove(QLatin1Char(':'));
        // a reaction carries no shortcode here: the picture's id gives one
        if (name.trimmed().isEmpty())
            name = url.section(QLatin1Char('/'), -1).left(12);
        out << QVariantMap{ { "url", url }, { "shortcode", name.trimmed() },
                            { "path", m_emotes.contains(url) ? QStringLiteral("file://") + m_emotes.value(url).path : QString() } };
    };
    const QString html = item.value("html").toString();
    if (html.contains(QLatin1String("mxc://"))) {
        QRegularExpressionMatchIterator it = imgTag().globalMatch(html);
        while (it.hasNext()) {
            const QString tag = it.next().captured();
            QString name = attribute(tag, QStringLiteral("alt"));
            if (name.isEmpty())
                name = attribute(tag, QStringLiteral("title"));
            add(attribute(tag, QStringLiteral("src")), name);
        }
    }
    for (const QVariant &v : item.value("reactions").toList()) {
        const QVariantMap r = v.toMap();
        add(r.value("key").toString(), r.value("shortcode").toString());
    }
    return out;
}

bool TimelineModel::usesEmote(const QVariantMap &item, const QString &mxc) const
{
    if (item.value("html").toString().contains(mxc)
            || item.value("reply_to").toMap().value("html").toString().contains(mxc))
        return true;
    for (const QVariant &v : item.value("reactions").toList())
        if (v.toMap().value("key").toString() == mxc)
            return true;
    return false;
}

void TimelineModel::setEmotePath(const QString &mxc, const QString &path)
{
    m_emotes.insert(mxc, Emote { path, QImageReader(path).size() });
    for (int i = 0; i < m_items.size(); ++i)
        if (usesEmote(m_items.at(i).toMap(), mxc))
            emit dataChanged(index(i), index(i));
}

void TimelineModel::clearEmotes()
{
    m_emotes.clear();
    m_emotesAsked.clear();
}

void TimelineModel::setEmoteSize(int size)
{
    if (size == m_emoteSize || size <= 0)
        return;
    m_emoteSize = size;
    emit emoteSizeChanged();
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
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
    for (const QVariant &v : target) {
        wanted.insert(idOf(v));
        askEmotes(v.toMap());
    }

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
