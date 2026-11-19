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
#include "Twemoji.h"

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
                            "reactions", "reply_to", "thread_root", "thread", "mime", "size", "w", "h", "has_thumb",
                            "filename", "duration", "geo_uri", "media_path", "thumb_path", "read_by",
                            "caption", "shield", "answers", "max_selections", "ended", "upload_done", "upload_total",
                            "emotes", "body_html", "caption_html", "thread_html" };
    int role = Qt::UserRole + 1;
    for (const char *n : names) {
        m_roles.insert(role, QByteArray(n));
        m_keys.insert(role, QString::fromLatin1(n));
        m_roleOf.insert(QString::fromLatin1(n), role);
        ++role;
    }
    m_htmlRole = m_roleOf.value(QStringLiteral("html"));
    m_reactionsRole = m_roleOf.value(QStringLiteral("reactions"));
    m_replyRole = m_roleOf.value(QStringLiteral("reply_to"));
    m_emotesRole = m_roleOf.value(QStringLiteral("emotes"));
    m_mediaRole = m_roleOf.value(QStringLiteral("media_path"));
    m_thumbRole = m_roleOf.value(QStringLiteral("thumb_path"));
    m_bodyHtmlRole = m_roleOf.value(QStringLiteral("body_html"));
    m_captionHtmlRole = m_roleOf.value(QStringLiteral("caption_html"));
    m_threadHtmlRole = m_roleOf.value(QStringLiteral("thread_html"));
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
    if (role == m_mediaRole)
        return m_media.value(item.value(QStringLiteral("event_id")).toString());
    if (role == m_thumbRole)
        return m_thumbs.value(item.value(QStringLiteral("event_id")).toString());
    if (role == m_htmlRole)
        return cachedHtml(item);
    if (role == m_reactionsRole)
        return cachedReactions(item);
    if (role == m_emotesRole)
        return cachedEmotes(item);
    if (role == m_replyRole)
        return cachedReply(item);
    if (role == m_bodyHtmlRole)
        return cachedBody(item);
    if (role == m_captionHtmlRole)
        return cachedCaption(item);
    if (role == m_threadHtmlRole)
        return cachedThread(item);
    return item.value(m_keys.value(role));
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return m_roles;
}

// QML reads a role at every binding that names it (Qt does not keep the
// values), so the rendering is done once per row and kept by the row's id
TimelineModel::Rendered &TimelineModel::rendered(const QVariantMap &item) const
{
    return m_rendered[item.value(QStringLiteral("id")).toString()];
}

QString TimelineModel::cachedHtml(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasHtml) {
        r.html = renderHtml(item.value(QStringLiteral("html")).toString());
        r.hasHtml = true;
    }
    return r.html;
}

QVariant TimelineModel::cachedReply(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasReply) {
        r.reply = renderReply(item.value(QStringLiteral("reply_to")));
        r.hasReply = true;
    }
    return r.reply;
}

QVariantList TimelineModel::cachedReactions(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasReactions) {
        r.reactions = renderReactions(item.value(QStringLiteral("reactions")).toList());
        r.hasReactions = true;
    }
    return r.reactions;
}

QVariantList TimelineModel::cachedEmotes(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasEmotes) {
        r.emotes = emotesOf(item);
        r.hasEmotes = true;
    }
    return r.emotes;
}

// A plain body as StyledText: an emote as "name text", text and notices
// with their addresses linked, both with Twemoji; other types have none
QString TimelineModel::cachedBody(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasBody) {
        QString type = item.value(QStringLiteral("type")).toString();
        if (type.isEmpty())
            type = QStringLiteral("text");
        const QString body = item.value(QStringLiteral("body")).toString();
        if (type == QLatin1String("emote")) {
            QString name = item.value(QStringLiteral("sender_name")).toString();
            if (name.isEmpty())
                name = item.value(QStringLiteral("sender")).toString();
            r.body = Twemoji::inHtml(StyledText::escaped(name + QLatin1Char(' ') + body), m_emoteSize);
        } else if (type == QLatin1String("text") || type == QLatin1String("notice")) {
            r.body = Twemoji::inHtml(StyledText::linkified(body), m_emoteSize);
        }
        r.hasBody = true;
    }
    return r.body;
}

// the text sent with a picture, a video or a file, its addresses linked
QString TimelineModel::cachedCaption(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasCaption) {
        r.caption = StyledText::linkified(item.value(QStringLiteral("caption")).toString());
        r.hasCaption = true;
    }
    return r.caption;
}

// a thread's latest reply in one line, "sender: text", or nothing
QString TimelineModel::cachedThread(const QVariantMap &item) const
{
    Rendered &r = rendered(item);
    if (!r.hasThread) {
        const QVariantMap t = item.value(QStringLiteral("thread")).toMap();
        const QString sender = t.value(QStringLiteral("latest_sender_name")).toString();
        if (!sender.isEmpty())
            r.thread = StyledText::escaped(sender) + QLatin1String(": ")
                       + Twemoji::inHtml(StyledText::escaped(StyledText::oneLine(t.value(QStringLiteral("latest_body")).toString())),
                                         m_smallEmojiSize);
        r.hasThread = true;
    }
    return r.thread;
}

void TimelineModel::forget(const QVariant &item)
{
    m_rendered.remove(item.toMap().value(QStringLiteral("id")).toString());
}

// the roles whose values differ between two versions of a row, with the
// ones made from them; no roles (all of them) when a key without a role
// changed
QVector<int> TimelineModel::changedRoles(const QVariantMap &from, const QVariantMap &to) const
{
    QVector<int> roles;
    bool unknown = false;
    auto add = [&](int role) {
        if (!roles.contains(role))
            roles << role;
    };
    auto note = [&](const QString &key) {
        const int role = m_roleOf.value(key, -1);
        if (role < 0) {
            unknown = true;
            return;
        }
        add(role);
        if (role == m_htmlRole || role == m_reactionsRole)
            add(m_emotesRole);
        if (key == QLatin1String("type") || key == QLatin1String("body")
                || key == QLatin1String("sender_name") || key == QLatin1String("sender"))
            add(m_bodyHtmlRole);
        if (key == QLatin1String("caption"))
            add(m_captionHtmlRole);
        if (key == QLatin1String("thread"))
            add(m_threadHtmlRole);
        // the media are looked up by the event's id
        if (key == QLatin1String("event_id")) {
            add(m_mediaRole);
            add(m_thumbRole);
        }
    };
    for (auto it = from.constBegin(); it != from.constEnd(); ++it)
        if (it.value() != to.value(it.key()))
            note(it.key());
    for (auto it = to.constBegin(); it != to.constEnd(); ++it)
        if (!from.contains(it.key()))
            note(it.key());
    return unknown ? QVector<int>() : roles;
}

QVariantMap TimelineModel::withMedia(QVariantMap item) const
{
    const QString id = item.value("event_id").toString();
    item.insert("media_path", m_media.value(id));
    item.insert("thumb_path", m_thumbs.value(id));
    if (item.contains("html"))
        item.insert("html", cachedHtml(item));
    if (item.contains("reactions"))
        item.insert("reactions", cachedReactions(item));
    if (item.contains("reply_to"))
        item.insert("reply_to", cachedReply(item));
    return item;
}

// A message's HTML as Text.StyledText (Qt 5.6) takes it. The spec's tags
// that StyledText knows pass, without attributes (with one it would not
// know them either); the others become the nearest it can draw, or only
// their text: em is italic, a quote italic in its own paragraph, a line a
// row of dashes, table cells a " | " row, colours (data-mx-color, #rrggbb)
// a font colour. A spoiler's text is covered: a row of full blocks as long
// as each of its lines, each a link to rustrx:spoiler that the room shows it on (shown: the
// text as it is, when the reader tapped it).
// Links keep only the spec's schemes (and matrix:). Custom emoji (an <img>
// with an mxc:// address) become the cached picture at a little more than
// the text's height, their name until it is there; every other picture
// becomes its name, so a message cannot make the phone fetch from any
// address. A reply's quoted fallback (<mx-reply>) goes.
QString TimelineModel::renderHtml(const QString &html, int emojiHeight, bool spoilersShown) const
{
    if (emojiHeight <= 0)
        emojiHeight = m_emoteSize;
    if (html.isEmpty())
        return html;
    static const QRegularExpression reply(QStringLiteral("<mx-reply>.*</mx-reply>"),
                                          QRegularExpression::CaseInsensitiveOption
                                          | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tagRe(QStringLiteral("<(/?)([a-zA-Z][a-zA-Z0-9-]*)([^>]*)>"));
    static const QRegularExpression colourRe(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    static const QRegularExpression schemeRe(QStringLiteral("^(https?|ftp|mailto|magnet|matrix):"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression namedEntity(QStringLiteral("&[a-zA-Z]+;"));
    static const QRegularExpression entityRe(QStringLiteral("&#(x[0-9a-fA-F]+|[0-9]+);|&apos;"));
    static const QSet<QString> plain = {
        QStringLiteral("b"), QStringLiteral("strong"), QStringLiteral("i"), QStringLiteral("u"),
        QStringLiteral("p"), QStringLiteral("pre"), QStringLiteral("ul"), QStringLiteral("ol"),
        QStringLiteral("li"), QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6") };

    QString in = html;
    in.remove(reply);

    // the text between tags: numeric entities decoded (StyledText knows
    // only the named few), then escaped again where needed. A bare "<" or
    // ">" here is text, not a tag (tagRe took those): StyledText would
    // still read "< img src=...>" as a picture, so it is escaped.
    auto text = [&](const QStringRef &t) {
        QString seg = t.toString();
        seg.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        seg.replace(QLatin1Char('>'), QLatin1String("&gt;"));
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
    int covered = 0;       // the characters of its line so far, one block each
    bool coveredLine = false;   // a line of it has been covered already
    QString out;
    auto emit_ = [&](const QString &s) { if (!hidden) out += s; };
    // text between tags, its emoji as Twemoji pictures
    auto emitText = [&](const QString &s) {
        if (!hidden)
            out += Twemoji::inHtml(s, emojiHeight);
        else
            covered += QString(s).replace(namedEntity, QStringLiteral("x")).toUcs4().size();
    };
    // the blocks for a spoiler's line so far
    auto cover = [&]() {
        out += QStringLiteral("<a href=\"%1\">%2</a>").arg(QLatin1String(kSpoilerLink), QString(covered, QChar(0x2588)));
        covered = 0;
        coveredLine = true;
    };
    auto closeTop = [&]() {
        const Open o = stack.takeLast();
        if (o.hides && --hidden == 0) {
            // an empty spoiler still shows that there is one
            if (covered > 0 || !coveredLine)
                covered = qMax(covered, 1), cover();
            coveredLine = false;
        }
        emit_(o.close);
    };

    int last = 0;
    QRegularExpressionMatchIterator it = tagRe.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        emitText(text(in.midRef(last, m.capturedStart() - last)));
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
        if (name == QLatin1String("br")) {
            // inside a spoiler: its line so far as blocks, then the break
            if (hidden && covered > 0)
                cover();
            out += QStringLiteral("<br>");
            continue;
        }
        if (name == QLatin1String("hr")) { emit_(QStringLiteral("<br>―――――<br>")); continue; }
        if (name == QLatin1String("img")) {
            if (hidden) { ++covered; continue; }   // a picture: one block
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
                open = QStringLiteral("<a href=\"%1\">").arg(StyledText::hrefSafe(href));
                o.close = QStringLiteral("</a>");
            }
        } else if (name == QLatin1String("span") || name == QLatin1String("font")) {
            if (!spoilersShown && tag.contains(QLatin1String("data-mx-spoiler"), Qt::CaseInsensitive)) {
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
    emitText(text(in.midRef(last)));
    while (!stack.isEmpty())
        closeTop();
    return out;
}

// A reaction with a custom emoji has its mxc address as the key (MSC4027):
// it gets "emote", the cached picture, when there is one. Any other gets
// "label", its key as StyledText with Twemoji, cut when long.
QVariantList TimelineModel::renderReactions(const QVariantList &reactions) const
{
    QVariantList out = reactions;
    for (QVariant &v : out) {
        QVariantMap r = v.toMap();
        const QString key = r.value("key").toString();
        if (key.startsWith(QLatin1String("mxc://"))) {
            const auto e = m_emotes.constFind(key);
            r.insert("emote", e != m_emotes.constEnd() ? QStringLiteral("file://") + e->path : QString());
        } else {
            const QString shown = key.size() > Keys::kReactionKeyMax ? key.left(Keys::kReactionKeyMax - 1) + QChar(0x2026) : key;
            r.insert("label", Twemoji::inHtml(StyledText::escaped(shown), m_smallEmojiSize));
        }
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
        // the quote's text is smaller
        r.insert("html", renderHtml(r.value("html").toString(), qRound(m_emoteSize * 0.75)));
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
        out << QVariantMap{ { "mxc", url }, { "shortcode", name.trimmed() },
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
    const QVector<int> roles { m_htmlRole, m_replyRole, m_reactionsRole, m_emotesRole };
    for (int i = 0; i < m_items.size(); ++i) {
        if (usesEmote(m_items.at(i).toMap(), mxc)) {
            forget(m_items.at(i));
            emit dataChanged(index(i), index(i), roles);
        }
    }
}

void TimelineModel::clearEmotes()
{
    m_emotes.clear();
    m_emotesAsked.clear();
    // rendered rows hold the pictures' paths
    m_rendered.clear();
    m_askAll = true;
}

void TimelineModel::setEmoteSize(int size)
{
    if (size == m_emoteSize || size <= 0)
        return;
    m_emoteSize = size;
    m_rendered.clear();
    emit emoteSizeChanged();
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), { m_htmlRole, m_replyRole, m_bodyHtmlRole });
}

void TimelineModel::setSmallEmojiSize(int size)
{
    if (size == m_smallEmojiSize || size <= 0)
        return;
    m_smallEmojiSize = size;
    for (Rendered &r : m_rendered)
        r.hasReactions = r.hasThread = false;
    emit smallEmojiSizeChanged();
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), { m_reactionsRole, m_threadHtmlRole });
}

int TimelineModel::rowOfEvent(const QString &eventId) const
{
    if (eventId.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).toMap().value(QStringLiteral("event_id")).toString() == eventId)
            return i;
    return -1;
}

int TimelineModel::rowOfKind(const QString &kind) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).toMap().value(QStringLiteral("kind")).toString() == kind)
            return i;
    return -1;
}

int TimelineModel::rowOwnRead() const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).toMap().value(QStringLiteral("own_read")).toBool())
            return i;
    return -1;
}

QString TimelineModel::seenEventId(int row) const
{
    // newest first: older rows have higher indexes
    for (int i = qMax(row, 0); i < m_items.size(); ++i) {
        const QVariantMap m = m_items.at(i).toMap();
        const QString eventId = m.value(QStringLiteral("event_id")).toString();
        if (m.value(QStringLiteral("kind")).toString() == QLatin1String("event") && !eventId.isEmpty())
            return eventId;
    }
    return QString();
}

QString TimelineModel::spoilersShown(int row) const
{
    if (row < 0 || row >= m_items.size())
        return QString();
    const QVariantMap item = m_items.at(row).toMap();
    m_spoilersOpen.insert(item.value(QStringLiteral("event_id")).toString());
    return renderHtml(item.value(QStringLiteral("html")).toString(), 0, true);
}

bool TimelineModel::spoilersOpen(int row) const
{
    return row >= 0 && row < m_items.size()
            && m_spoilersOpen.contains(m_items.at(row).toMap().value(QStringLiteral("event_id")).toString());
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
    // the custom emoji are looked for in new and changed rows only (all of
    // them once after the emoji cache was emptied)
    const bool askAll = m_askAll;
    m_askAll = false;
    QVector<int> ask;

    // 1. rows that are gone, from the end so the indices stay valid
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!wanted.contains(idOf(m_items.at(i)))) {
            forget(m_items.at(i));
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
        }
    }

    // a row with new content: rendered anew, and only its changed roles told
    auto update = [&](int i) {
        if (m_items.at(i) == target.at(i))
            return;
        const QVector<int> roles = changedRoles(m_items.at(i).toMap(), target.at(i).toMap());
        forget(m_items.at(i));
        m_items[i] = target.at(i);
        if (!askAll)
            ask << i;
        emit dataChanged(index(i), index(i), roles);
    };

    // 2. walk the target order: keep, move up, or insert
    for (int i = 0; i < target.size(); ++i) {
        const QString id = idOf(target.at(i));
        if (i < m_items.size() && idOf(m_items.at(i)) == id) {
            update(i);
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
            update(i);
        } else {
            forget(target.at(i));
            beginInsertRows(QModelIndex(), i, i);
            m_items.insert(i, target.at(i));
            endInsertRows();
            if (!askAll)
                ask << i;
        }
    }
    if (askAll) {
        for (const QVariant &v : target)
            askEmotes(v.toMap());
    } else {
        for (int i : ask)
            askEmotes(target.at(i).toMap());
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
            emit dataChanged(index(i), index(i), { m_mediaRole, m_thumbRole });
            break;
        }
    }
}

void TimelineModel::clear()
{
    m_rendered.clear();
    m_spoilersOpen.clear();
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}
