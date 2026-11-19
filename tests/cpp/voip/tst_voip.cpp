// SPDX-License-Identifier: GPL-3.0-or-later
// src/Voip.cpp: two engines call each other through a stand-in for the
// core (each one's "voip_send" arrives at the other as a "voip" event,
// and back at itself as its own echo), with GStreamer's webrtcbin, ICE on
// this machine, DTLS, SRTP and Opus for real. A test tone goes in on both
// sides; the far end's level and packet counts show it came through.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "Voip.h"

#include <QtTest>

class Wire : public QObject
{
    Q_OBJECT
public:
    Voip a, b;
    QStringList sent;   // "a:m.call.invite", ...
    bool deliver = true;

    Wire()
    {
        a.setOwnUserId(QStringLiteral("@a:example.org"));
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        a.setTestTone(true);
        b.setTestTone(true);
        connect(&a, &Voip::command, this, [this](const QVariantMap &c) { route(&a, &b, "@a:example.org", "a", c); });
        connect(&b, &Voip::command, this, [this](const QVariantMap &c) { route(&b, &a, "@b:example.org", "b", c); });
    }

    // what the core would do with the command, a moment later
    void route(Voip *from, Voip *to, const char *sender, const char *name, const QVariantMap &c)
    {
        if (c.value("cmd") == QLatin1String("turn_server")) {
            QTimer::singleShot(10, from, [from]() { from->handleTurn({ { "uris", QVariantList() } }); });
            return;
        }
        if (c.value("cmd") != QLatin1String("voip_send"))
            return;
        const QString type = c.value("type").toString();
        sent << QString::fromLatin1(name) + QLatin1Char(':') + type;
        // the core's "voip_sent", slow for an invite or answer (a busy
        // server), so candidates would overtake them if they did not wait
        const bool slow = type == QLatin1String("m.call.invite") || type == QLatin1String("m.call.answer");
        const QVariantMap ack{ { "type", type }, { "call_id", c.value("content").toMap().value("call_id") } };
        QTimer::singleShot(slow ? 600 : 5, from, [this, from, name, ack]() {
            sent << QString::fromLatin1(name) + QLatin1String(":sent:") + ack.value("type").toString();
            from->handleSent(ack);
        });
        QVariantMap ev{ { "ev", "voip" }, { "room_id", c.value("room_id") }, { "type", c.value("type") },
                        { "sender", QString::fromLatin1(sender) }, { "sender_name", QString::fromLatin1(name) },
                        { "content", c.value("content") }, { "age_ms", 0 }, { "room_name", "DM" } };
        if (!deliver)
            return;
        QVariantMap echo = ev;
        echo.insert("own", true);
        ev.insert("own", false);
        QTimer::singleShot(5, to, [to, ev]() { to->handleEvent(ev); });
        QTimer::singleShot(5, from, [from, echo]() { from->handleEvent(echo); });
    }
};

class TestVoip : public QObject
{
    Q_OBJECT

private slots:
    // Planted-bug: candidates that arrive before the remote description
    // are dropped instead of kept (remote_candidates stays 0).
    // Planted-bug: no select_answer from the caller (sent list check).
    // Planted-bug: candidates go out before the core sent the invite or
    // answer (the order check).
    void aCallCarriesAudioBothWays()
    {
        Wire w;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QCOMPARE(w.a.state(), QStringLiteral("outgoing"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        QCOMPARE(w.b.peerName(), QStringLiteral("a"));
        // the caller's candidates arrive before the answer, as they do
        // when a person takes a moment to pick up
        QTRY_VERIFY_WITH_TIMEOUT(w.sent.contains(QStringLiteral("a:m.call.candidates")), 5000);
        QTest::qWait(200);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("connected"), 15000);
        // the tone arrives on both sides, and packets flow both ways
        QTRY_VERIFY_WITH_TIMEOUT(w.a.status().value("remote_level_db").toDouble() > -40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() > -40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.a.status().value("packets_in").toInt() > 20, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.b.status().value("packets_in").toInt() > 20, 10000);
        QVERIFY(w.sent.indexOf(QStringLiteral("a:m.call.candidates")) > w.sent.indexOf(QStringLiteral("a:sent:m.call.invite")));
        QVERIFY(w.sent.indexOf(QStringLiteral("b:m.call.candidates")) > w.sent.indexOf(QStringLiteral("b:sent:m.call.answer")));
        QVERIFY(w.a.status().value("remote_candidates").toInt() > 0);
        QVERIFY(w.b.status().value("remote_candidates").toInt() > 0);
        QVERIFY(w.sent.contains(QStringLiteral("a:m.call.select_answer")));
        QVERIFY(w.sent.contains(QStringLiteral("a:m.call.candidates")));
        QVERIFY(w.sent.contains(QStringLiteral("b:m.call.candidates")));
        w.a.hangup();
        QCOMPARE(w.a.state(), QStringLiteral("idle"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
        QCOMPARE(w.b.endReason(), QStringLiteral("hangup"));
    }

    // Planted-bug: the caller ignores m.call.reject (it rings on).
    void aRejectEndsTheCaller()
    {
        Wire w;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.reject();
        QCOMPARE(w.b.state(), QStringLiteral("idle"));
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("idle"), 3000);
        QCOMPARE(w.a.endReason(), QStringLiteral("rejected"));
        QVERIFY(w.sent.contains(QStringLiteral("b:m.call.reject")));
    }

    // Planted-bug: our own answer from another device is taken as the
    // peer's (the phone rings on after we answered elsewhere).
    void answeredOnAnotherDevice()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        b.handleEvent({ { "type", "m.call.invite" }, { "room_id", "!dm" }, { "sender", "@a:example.org" }, { "own", false },
                        { "age_ms", 0 }, { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "pa" }, { "version", "1" },
                                                                   { "lifetime", 60000 },
                                                                   { "offer", QVariantMap{ { "type", "offer" }, { "sdp", "v=0" } } } } } });
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        b.handleEvent({ { "type", "m.call.answer" }, { "room_id", "!dm" }, { "sender", "@b:example.org" }, { "own", true },
                        { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "other-device" }, { "version", "1" } } } });
        QCOMPARE(b.state(), QStringLiteral("idle"));
        QCOMPARE(b.endReason(), QStringLiteral("answered_elsewhere"));
    }

    // Planted-bug: no age check (the initial sync's old invites ring).
    // Planted-bug: no invitee check (a call for someone else rings).
    void staleOrForeignInvitesStayQuiet()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        QVariantMap content{ { "call_id", "c1" }, { "party_id", "pa" }, { "version", "1" }, { "lifetime", 60000 },
                             { "offer", QVariantMap{ { "type", "offer" }, { "sdp", "v=0" } } } };
        b.handleEvent({ { "type", "m.call.invite" }, { "sender", "@a:example.org" }, { "age_ms", 60000 }, { "content", content } });
        QCOMPARE(b.state(), QStringLiteral("idle"));
        content.insert("invitee", "@c:example.org");
        b.handleEvent({ { "type", "m.call.invite" }, { "sender", "@a:example.org" }, { "age_ms", 0 }, { "content", content } });
        QCOMPARE(b.state(), QStringLiteral("idle"));
    }

    // Planted-bug: the credentials go in unescaped (an "@" in the password
    // breaks the address).
    void turnAddresses()
    {
        QCOMPARE(Voip::turnUrl("turn:turn.example.org:3478?transport=udp", "1700:@u:ex.org", "p@ss/w"),
                 QStringLiteral("turn://1700%3A%40u%3Aex.org:p%40ss%2Fw@turn.example.org:3478?transport=udp"));
        QCOMPARE(Voip::turnUrl("turns:t.example.org:443?transport=tcp", "u", "p"),
                 QStringLiteral("turns://u:p@t.example.org:443?transport=tcp"));
        QCOMPARE(Voip::turnUrl("stun:s.example.org", "u", "p"), QString());
        QCOMPARE(Voip::mids("v=0\r\nm=audio 9 x\r\na=mid:0\r\nm=video 9 x\r\na=mid:v1\r\n"), QStringList({ "0", "v1" }));
    }
};

QTEST_GUILESS_MAIN(TestVoip)
#include "tst_voip.moc"
