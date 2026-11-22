// SPDX-License-Identifier: GPL-3.0-or-later
// The plain texts as the old JavaScript rendered them, against the models
// that render them now. 84311d4 (message body, caption, thread line),
// 2754eef (room name and preview) and ba68435 (reaction labels) moved the
// rendering from QML to C++ and promised the same bytes; plain-texts.json
// holds the old output for some 4300 texts (gen/make-golden.sh made it by
// running the old JavaScript), and each case here goes through the model
// the app uses, role and all.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "timeline.h"
#include "RoomModel.h"
#include "Twemoji.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {

const QString kDir = QStringLiteral(RUSTRX_ROOT "/qml/twemoji");
// the sizes gen.cpp gave the old JavaScript
const int kEmote = 40, kSmall = 27, kName = 42, kPreview = 33;

}

class TestGolden : public QObject
{
    Q_OBJECT

    QJsonArray cases;
    Timeline t;
    RoomModel rooms;
    int roomId = 0;

    QString viaRoom(QVariantMap room, bool multiLine, const char *role)
    {
        rooms.setPreviewMultiLine(multiLine);
        room.insert(QStringLiteral("room_id"), QString::number(++roomId));
        rooms.setRooms({ room });
        return rooms.data(rooms.index(0), rooms.roleNames().key(role)).toString();
    }

    // what the model makes of a case's input now
    QString now(const QString &kind, const QVariantMap &in)
    {
        if (kind == QLatin1String("body"))
            return t.row(in, "body_html").toString();
        if (kind == QLatin1String("caption"))
            return t.row(in, "caption_html").toString();
        if (kind == QLatin1String("thread"))
            return t.row(in, "thread_html").toString();
        if (kind == QLatin1String("label"))
            return t.row(in, "reactions").toList().value(0).toMap().value("label").toString();
        if (kind == QLatin1String("name"))
            return viaRoom(in, false, "name_html");
        if (kind == QLatin1String("preview"))
            return viaRoom(in, false, "preview_html");
        if (kind == QLatin1String("preview_multi"))
            return viaRoom(in, true, "preview_html");
        return QStringLiteral("unknown kind");
    }

    // The one intended change since the old JavaScript (audit I3): a quote
    // inside a plain address is percent-encoded in its href, so it cannot
    // end the value and start a second href. The visible text is as before.
    static QString withI3(QString old)
    {
        // in these plain texts ">" is always escaped, so the first "\">"
        // ends the href, quotes in it or not
        static const QRegularExpression link(QStringLiteral("<a href=\"(.*?)\">"));
        QRegularExpressionMatch m;
        int from = 0;
        while ((m = link.match(old, from)).hasMatch()) {
            const QString fixed = QStringLiteral("<a href=\"") + StyledText::hrefSafe(m.captured(1)) + QStringLiteral("\">");
            old.replace(m.capturedStart(), m.capturedLength(), fixed);
            from = m.capturedStart() + fixed.size();
        }
        return old;
    }

    // The second intended change (0.4.8): a bare domain ("example.org",
    // "www.x.zz") is linked, where the old JavaScript left it text. Cases
    // with one are set aside here, found by the test's own pattern (the
    // unit tests in tst_twemoji hold the rule itself), and counted.
    static bool hasBareDomain(const QString &text)
    {
        static const QRegularExpression bare(QStringLiteral(
            "(^|[\\s(\\[>\"'])(www\\.[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+"
            "|[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)*\\.(com|org|net|edu|gov|io|dev|app|info|me|co|tv|ai|gg|fm|im|is|ly|to"
            "|xyz|online|site|tech|social|chat|eu|de|at|ch|uk|fr|nl|be|it|es|pt|se|no|fi|dk|pl|cz|sk|hu|ro|gr|ie"
            "|lu|ru|ua|us|ca|au|nz|jp|cn|kr|in|br|mx|ar))(?![A-Za-z0-9-])"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        return bare.match(text).hasMatch();
    }

    // The third intended change (0.6.0): a preview keeps its emoji as text
    // (the label drew the pictures out of place). Made pictures again here,
    // a preview must be the old output exactly, so nothing else changed.
    static QString previewAsBefore(const QString &kind, const QString &now)
    {
        return kind.startsWith(QLatin1String("preview")) ? Twemoji::inHtml(now, kPreview) : now;
    }

    // every case of a kind (and message type, for bodies): the number
    // checked; a difference fails with the first few shown
    void check(const QString &kind, const QString &type = QString())
    {
        int checked = 0, differ = 0, setAside = 0;
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            const QVariantMap in = c.value("in").toObject().toVariantMap();
            if (c.value("kind").toString() != kind || (!type.isEmpty() && in.value("type").toString() != type))
                continue;
            bool aside = false;
            for (const QVariant &v : in)
                aside = aside || (v.type() == QVariant::String && hasBareDomain(v.toString()))
                        || (v.type() == QVariant::Map && hasBareDomain(QStringList(v.toMap().value("latest_body").toString()).join(QString())));
            if (aside) {
                ++setAside;
                continue;
            }
            ++checked;
            const QString want = withI3(c.value("out").toString().replace(QStringLiteral("@DIR@"), kDir));
            const QString got = previewAsBefore(kind, now(kind, in));
            if (got != want && ++differ <= 3)
                qWarning("%s\n  input: %s\n  old:   %s\n  now:   %s", qPrintable(kind),
                         QJsonDocument(c.value("in").toObject()).toJson(QJsonDocument::Compact).constData(),
                         qPrintable(want.left(300)), qPrintable(got.left(300)));
        }
        qInfo("%s%s: %d cases, %d set aside (bare domains)", qPrintable(kind),
              qPrintable(type.isEmpty() ? type : QLatin1Char(' ') + type), checked, setAside);
        QVERIFY(checked > 100);
        QCOMPARE(differ, 0);
    }

private slots:
    void initTestCase()
    {
        Twemoji::init(kDir);
        t.model.setEmoteSize(kEmote);
        t.model.setSmallEmojiSize(kSmall);
        rooms.setNameEmojiSize(kName);
        QFile f(QStringLiteral(RUSTRX_ROOT "/tests/cpp/golden/plain-texts.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError error;
        cases = QJsonDocument::fromJson(f.readAll(), &error).array();
        QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));
        QVERIFY(cases.size() > 4000);
    }

    // Planted-bug: a text body's Twemoji are drawn at the small size
    // (m_smallEmojiSize instead of m_emoteSize).
    void textBodies() { check(QStringLiteral("body"), QStringLiteral("text")); }
    // Planted-bug: cachedBody() does not know "notice" (renders nothing).
    void noticeBodies() { check(QStringLiteral("body"), QStringLiteral("notice")); }
    // Planted-bug: an emote's name and text are joined without the space.
    void emoteBodies() { check(QStringLiteral("body"), QStringLiteral("emote")); }
    // Planted-bug: the caption is escaped, not linkified.
    void captions() { check(QStringLiteral("caption")); }
    // Planted-bug: the thread line's separator is ":" without the space.
    void threadLines() { check(QStringLiteral("thread")); }
    // Planted-bug: a reaction key is cut at kReactionKeyMax characters
    // instead of one fewer before the ellipsis.
    void reactionLabels() { check(QStringLiteral("label")); }
    // Planted-bug: a room name has its ">" escaped too (escaped(name)
    // without "false"), which the JavaScript did not do.
    void roomNames() { check(QStringLiteral("name")); }
    // Planted-bug: isSpace() forgets U+3000, which JavaScript's \s has.
    void previews() { check(QStringLiteral("preview")); }
    // Planted-bug: a multi-line preview's "\n" become "<br/>".
    void multiLinePreviews() { check(QStringLiteral("preview_multi")); }
    // Planted-bug: the preview's emoji made Twemoji pictures again.
    void previewEmojiStayText()
    {
        const QString party = QString::fromUtf8("Your prawn is done \xF0\x9F\x8E\x89 I copied all");
        for (bool multi : { false, true }) {
            const QString html = viaRoom({ { "name", "dodoradio" }, { "preview", party } }, multi, "preview_html");
            QVERIFY2(!html.contains(QLatin1String("<img")), qPrintable(html));
            QCOMPARE(html, party);
        }
    }
};

QTEST_GUILESS_MAIN(TestGolden)
#include "tst_golden.moc"
