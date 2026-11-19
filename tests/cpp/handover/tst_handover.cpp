// SPDX-License-Identifier: GPL-3.0-or-later
// src/Handover.cpp: the stand-in's handover file is taken up only when
// every field is as the stand-in writes it (audit S2). Anyone who can
// plant the file (before 9d530c1, through a link in Downloads) must not
// get a call answered in a room of their choosing or a file of Rustrx's
// own (the session) shared.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include "Handover.h"

static const char kShared[] = "/home/u/.cache/net.mowerk/harbour-rustrx/shared";

class TestHandover : public QObject
{
    Q_OBJECT

    // a file as Handover::handOver() writes it, with a ringing call as
    // Voip::ringing() makes it (Answer pressed in the notification)
    static QJsonObject good()
    {
        const QJsonObject voip{ { "room_id", "!dm:example.org" }, { "call_id", "1700000000000abc" },
                                { "party_id", "PARTY" }, { "version", "1" }, { "offer", "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n" },
                                { "peer_name", "Alice" }, { "expires_at", double(QDateTime::currentMSecsSinceEpoch() + 50000) },
                                { "answer", true } };
        const QJsonObject share{ { "files", QJsonArray{ QString::fromLatin1(kShared) + "/photo.jpg" } }, { "text", "look" } };
        return { { "room", "!room:example.org" }, { "call", "" }, { "knocks", false },
                 { "uri", "matrix:roomid/room:example.org?via=example.org" }, { "share", share }, { "voip", voip } };
    }
    static QJsonObject with(QJsonObject o, const QString &key, const QJsonValue &v)
    {
        o.insert(key, v);
        return o;
    }
    static QJsonObject withVoip(const QString &key, const QJsonValue &v)
    {
        QJsonObject o = good();
        o.insert("voip", with(o.value("voip").toObject(), key, v));
        return o;
    }
    static QJsonObject withShare(const QString &key, const QJsonValue &v)
    {
        QJsonObject o = good();
        o.insert("share", with(o.value("share").toObject(), key, v));
        return o;
    }
    static bool accepts(Handover &h, const QJsonObject &o)
    {
        return h.accept(QJsonDocument(o).toJson(QJsonDocument::Compact), QString::fromLatin1(kShared));
    }

private slots:
    void whatTheStandInWritesIsTaken()
    {
        Handover h;
        QVERIFY(accepts(h, good()));
        QCOMPARE(h.takePendingRoom(), QStringLiteral("!room:example.org"));
        QCOMPARE(h.takePendingUri(), QStringLiteral("matrix:roomid/room:example.org?via=example.org"));
        QCOMPARE(h.takePendingShare().value("files").toStringList(), QStringList{ QString::fromLatin1(kShared) + "/photo.jpg" });
        const QVariantMap voip = h.takePendingVoip();
        QCOMPARE(voip.value("call_id").toString(), QStringLiteral("1700000000000abc"));
        QCOMPARE(voip.value("answer").toBool(), true);

        // nothing asked for but a room; a version 0 call without a party;
        // a room id of room version 12 (no server part)
        Handover quiet;
        QVERIFY(accepts(quiet, { { "room", "!AbC_12-xyz" }, { "call", "" }, { "knocks", true }, { "uri", "" },
                                 { "share", QJsonObject() }, { "voip", QJsonObject() } }));
        QCOMPARE(quiet.takePendingRoom(), QStringLiteral("!AbC_12-xyz"));
        QVERIFY(quiet.takePendingKnocks());
        QJsonObject v0 = good();
        QJsonObject call = v0.value("voip").toObject();
        call.remove("party_id");
        call.insert("version", 0);
        v0.insert("voip", call);
        QVERIFY(accepts(quiet, v0));
        QVERIFY(accepts(quiet, with(good(), "uri", "https://matrix.to/#/#room:example.org")));
    }

    // Planted-bug: share paths not checked (a planted file shares the
    // session). Planted-bug: ids not checked (a call answered in a room
    // id that is not one). Planted-bug: the link not checked. Planted-bug:
    // no bound on the ring's end. Planted-bug: unknown keys pass.
    void anythingElseDropsTheFile_data()
    {
        QTest::addColumn<QJsonObject>("file");
        const QString shared = QString::fromLatin1(kShared);
        QTest::newRow("share: the session") << withShare("files", QJsonArray{ "/home/u/.config/net.mowerk/harbour-rustrx/session.json" });
        QTest::newRow("share: out with ..") << withShare("files", QJsonArray{ shared + "/../../../.config/net.mowerk/harbour-rustrx/session.json" });
        QTest::newRow("share: a subfolder") << withShare("files", QJsonArray{ shared + "/x/photo.jpg" });
        QTest::newRow("share: the folder") << withShare("files", QJsonArray{ shared });
        QTest::newRow("share: relative") << withShare("files", QJsonArray{ "photo.jpg" });
        QTest::newRow("share: a number") << withShare("files", QJsonArray{ 1 });
        QTest::newRow("share: files no list") << withShare("files", shared + "/photo.jpg");
        QTest::newRow("share: text no string") << withShare("text", QJsonArray());
        QTest::newRow("share: other key") << withShare("path", "/etc/passwd");
        QTest::newRow("room: no room id") << with(good(), "room", "@alice:example.org");
        QTest::newRow("room: a space") << with(good(), "room", "!a b:example.org");
        QTest::newRow("room: a number") << with(good(), "room", 1);
        QTest::newRow("call: just !") << with(good(), "call", "!");
        QTest::newRow("knocks: a string") << with(good(), "knocks", "yes");
        QTest::newRow("uri: the web") << with(good(), "uri", "https://evil.example/");
        QTest::newRow("uri: a file") << with(good(), "uri", "file:///home/u/.config/net.mowerk/harbour-rustrx/session.json");
        QTest::newRow("uri: too long") << with(good(), "uri", QStringLiteral("matrix:u/") + QString(5000, 'a'));
        QTest::newRow("voip: room is a user") << withVoip("room_id", "@alice:example.org");
        QTest::newRow("voip: call id empty") << withVoip("call_id", "");
        QTest::newRow("voip: call id with a newline") << withVoip("call_id", "abc\ndef");
        QTest::newRow("voip: call id too long") << withVoip("call_id", QString(256, 'a'));
        QTest::newRow("voip: party a number") << withVoip("party_id", 5);
        QTest::newRow("voip: version other") << withVoip("version", 2);
        QTest::newRow("voip: version long") << withVoip("version", QString(40, '1'));
        QTest::newRow("voip: offer a list") << withVoip("offer", QJsonArray());
        QTest::newRow("voip: offer huge") << withVoip("offer", QString((1 << 20) + 1, 'a'));
        QTest::newRow("voip: rings for a year") << withVoip("expires_at", double(QDateTime::currentMSecsSinceEpoch()) + 365.0 * 86400000);
        QTest::newRow("voip: no end") << withVoip("expires_at", "soon");
        QTest::newRow("voip: answer a string") << withVoip("answer", "yes");
        QTest::newRow("voip: other key") << withVoip("sdp_path", "/tmp/x");
        QTest::newRow("voip: a list") << with(good(), "voip", QJsonArray());
        QTest::newRow("other key") << with(good(), "session", "x");
        QJsonObject missing = good();
        missing.remove("knocks");
        QTest::newRow("a field missing") << missing;
    }
    void anythingElseDropsTheFile()
    {
        QFETCH(QJsonObject, file);
        Handover h;
        QVERIFY(!accepts(h, file));
        // all or nothing: not even the valid fields are taken
        QVERIFY(h.takePendingRoom().isEmpty());
        QVERIFY(h.takePendingShare().isEmpty());
        QVERIFY(h.takePendingVoip().isEmpty());
        QVERIFY(h.takePendingUri().isEmpty());
    }

    // Planted-bug: the size not checked before parsing.
    void notJsonOrTooLarge()
    {
        Handover h;
        QVERIFY(!h.accept("", QString::fromLatin1(kShared)));
        QVERIFY(!h.accept("[1,2]", QString::fromLatin1(kShared)));
        QVERIFY(!h.accept("{\"room\":", QString::fromLatin1(kShared)));
        QJsonObject big = withShare("text", QString(800000, 'a'));
        big.insert("voip", with(big.value("voip").toObject(), "offer", QString(800000, 'b')));
        big.insert("uri", "");
        const QByteArray json = QJsonDocument(with(big, "call", "")).toJson();
        QVERIFY(json.size() < (4 << 20));
        QVERIFY(h.accept(json, QString::fromLatin1(kShared)));
        QByteArray padded = json;
        padded.append(QByteArray((4 << 20) - json.size() + 1, ' '));   // valid JSON, over the cap
        QVERIFY(!h.accept(padded, QString::fromLatin1(kShared)));
    }
};

QTEST_GUILESS_MAIN(TestHandover)
#include "tst_handover.moc"
