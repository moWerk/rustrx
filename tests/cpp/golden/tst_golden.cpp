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

    // every case of a kind (and message type, for bodies): the number
    // checked; a difference fails with the first few shown
    void check(const QString &kind, const QString &type = QString())
    {
        int checked = 0, differ = 0;
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            const QVariantMap in = c.value("in").toObject().toVariantMap();
            if (c.value("kind").toString() != kind || (!type.isEmpty() && in.value("type").toString() != type))
                continue;
            ++checked;
            const QString want = c.value("out").toString().replace(QStringLiteral("@DIR@"), kDir);
            const QString got = now(kind, in);
            if (got != want && ++differ <= 3)
                qWarning("%s\n  input: %s\n  old:   %s\n  now:   %s", qPrintable(kind),
                         QJsonDocument(c.value("in").toObject()).toJson(QJsonDocument::Compact).constData(),
                         qPrintable(want.left(300)), qPrintable(got.left(300)));
        }
        qInfo("%s%s: %d cases", qPrintable(kind), qPrintable(type.isEmpty() ? type : QLatin1Char(' ') + type), checked);
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
        rooms.setPreviewEmojiSize(kPreview);
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
};

QTEST_GUILESS_MAIN(TestGolden)
#include "tst_golden.moc"
