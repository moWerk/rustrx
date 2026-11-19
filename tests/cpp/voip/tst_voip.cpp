// SPDX-License-Identifier: GPL-3.0-or-later
// src/Voip.cpp: two engines call each other through a stand-in for the
// core (each one's "voip_send" arrives at the other as a "voip" event,
// and back at itself as its own echo), with GStreamer's webrtcbin, ICE on
// this machine, DTLS, SRTP and Opus for real. A test tone goes in on both
// sides; the far end's level and packet counts show it came through.
//
// Also the echo canceller's frame pairing (src/AecFrames.h) and what a
// call leaves behind for the next one.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "Voip.h"
#include "AecFrames.h"

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
        // two different tones: with the same one on both sides, each echo
        // canceller learns the far end as its own echo and takes it out
        // after a few seconds (the audit measured -54 dB at 10 s)
        a.setTestTone(true, 440);
        b.setTestTone(true, 660);
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
                        { "content", c.value("content") }, { "age_ms", 0 }, { "room_name", "DM" }, { "is_dm", true } };
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
    // answer (the order check). Planted-bug: the same tone on both sides
    // (the late level check fails: the echo canceller takes it out).
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
        // and it lasts: still there after the echo cancellers had time to
        // adapt (a canceller eating the far end fails here)
        QTest::qWait(8000);
        QVERIFY2(w.a.status().value("remote_level_db").toDouble() > -40, qPrintable(w.a.status().value("remote_level_db").toString()));
        QVERIFY2(w.b.status().value("remote_level_db").toDouble() > -40, qPrintable(w.b.status().value("remote_level_db").toString()));
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
                        { "age_ms", 0 }, { "is_dm", true }, { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "pa" }, { "version", "1" },
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

    // A ring in the stand-in goes to the app with the handover file.
    // Planted-bug: the app takes an expired ring (it rings for nothing).
    void aRingingCallIsHandedOver()
    {
        Voip standIn;
        standIn.setOwnUserId(QStringLiteral("@b:example.org"));
        standIn.handleEvent({ { "type", "m.call.invite" }, { "room_id", "!dm" }, { "sender", "@a:example.org" }, { "own", false },
                              { "age_ms", 1000 }, { "sender_name", "A" }, { "is_dm", true },
                              { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "pa" }, { "version", "1" },
                                                        { "lifetime", 60000 },
                                                        { "offer", QVariantMap{ { "type", "offer" }, { "sdp", "v=0" } } } } } });
        const QVariantMap invite = standIn.ringing();
        QCOMPARE(invite.value("call_id").toString(), QStringLiteral("c1"));
        QVERIFY(invite.value("expires_at").toLongLong() > QDateTime::currentMSecsSinceEpoch() + 50000);

        Voip app;
        app.takeRinging(invite);
        QCOMPARE(app.state(), QStringLiteral("incoming"));
        QCOMPARE(app.peerName(), QStringLiteral("A"));
        QCOMPARE(app.roomId(), QStringLiteral("!dm"));

        Voip late;
        QVariantMap old = invite;
        old.insert("expires_at", QDateTime::currentMSecsSinceEpoch() - 1);
        late.takeRinging(old);
        QCOMPARE(late.state(), QStringLiteral("idle"));
        QVERIFY(Voip().ringing().isEmpty());
    }

    static QVariantMap invite(const char *callId, qint64 lifetime, bool dm, const char *invitee = nullptr,
                              const char *sender = "@a:example.org")
    {
        QVariantMap content{ { "call_id", callId }, { "party_id", "pa" }, { "version", "1" }, { "lifetime", lifetime },
                             { "offer", QVariantMap{ { "type", "offer" }, { "sdp", "v=0" } } } };
        if (invitee)
            content.insert("invitee", invitee);
        return { { "type", "m.call.invite" }, { "room_id", "!r" }, { "sender", sender }, { "own", false },
                 { "age_ms", 0 }, { "is_dm", dm }, { "content", content } };
    }

    // Planted-bug: no direct-chat check (anyone in a big room rings
    // everyone). Planted-bug: no cap on the sender's lifetime.
    void whoMayRingAndForHowLong()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        b.handleEvent(invite("c1", 60000, false));
        QCOMPARE(b.state(), QStringLiteral("idle"));
        b.handleEvent(invite("c2", 60000, false, "@b:example.org"));
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        Voip d;
        d.setOwnUserId(QStringLiteral("@b:example.org"));
        d.handleEvent(invite("c3", 9000000000000LL, true));
        QCOMPARE(d.state(), QStringLiteral("incoming"));
        // the cap is 2 min; Qt's coarse timers may report up to 5 % more
        // (an exact <= 120000 failed now and then); uncapped it is years
        QVERIFY(d.ringing().value("expires_at").toLongLong() < QDateTime::currentMSecsSinceEpoch() + 130000);
    }

    // Planted-bug: an invite while busy is dropped silently (the caller
    // rings on).
    void busyAnswersBusy()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        QSignalSpy sent(&b, &Voip::command);
        b.handleEvent(invite("c1", 60000, true));
        b.handleEvent(invite("c2", 60000, true, nullptr, "@c:example.org"));
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        QCOMPARE(sent.count(), 1);
        const QVariantMap cmd = sent.at(0).at(0).toMap();
        QCOMPARE(cmd.value("type").toString(), QStringLiteral("m.call.hangup"));
        QCOMPARE(cmd.value("content").toMap().value("call_id").toString(), QStringLiteral("c2"));
        QCOMPARE(cmd.value("content").toMap().value("reason").toString(), QStringLiteral("user_busy"));
    }

    // Planted-bug: no sender check (a third person in the room who saw
    // the call's id hangs it up).
    void othersInTheRoomCannotEndIt()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        b.handleEvent(invite("c1", 60000, true));
        b.handleEvent({ { "type", "m.call.hangup" }, { "room_id", "!r" }, { "sender", "@c:example.org" }, { "own", false },
                        { "content", QVariantMap{ { "call_id", "c1" }, { "version", "1" } } } });
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        b.handleEvent({ { "type", "m.call.hangup" }, { "room_id", "!r" }, { "sender", "@a:example.org" }, { "own", false },
                        { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "pa" }, { "version", "1" } } } });
        QCOMPARE(b.state(), QStringLiteral("idle"));
    }

    // Planted-bug: sendFailed() does nothing (an answer that could not be
    // sent stayed on "Connecting…" for good).
    void aCallThatCannotBeSentEnds()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        b.handleEvent(invite("c1", 60000, true));
        b.answer();
        QCOMPARE(b.state(), QStringLiteral("connecting"));
        b.sendFailed();
        QCOMPARE(b.state(), QStringLiteral("idle"));
        QCOMPARE(b.endReason(), QStringLiteral("send_failed"));
    }

    // Planted-bug: setMuted does not reach the microphone's volume (the
    // far end hears on). Planted-bug: unmuting leaves it muted.
    void muteSilencesTheFarEnd()
    {
        Wire w;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() > -40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.a.status().value("remote_level_db").toDouble() > -40, 10000);
        w.a.setMuted(true);
        QVERIFY(w.a.muted());
        // a's tone is gone at b (silence through Opus reads far below)
        QTRY_VERIFY2_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() < -70,
                                  qPrintable(w.b.status().value("remote_level_db").toString()), 5000);
        // and only a's microphone: b is still heard at a
        QVERIFY2(w.a.status().value("remote_level_db").toDouble() > -40, qPrintable(w.a.status().value("remote_level_db").toString()));
        w.a.setMuted(false);
        QTRY_VERIFY2_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() > -40,
                                  qPrintable(w.b.status().value("remote_level_db").toString()), 5000);
        w.a.hangup();
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
    }

    // Planted-bug: the test tone outlives the call (a later real call
    // sent 440 Hz and played nothing). Planted-bug: echo cancelling stays
    // off after a test turned it off. Planted-bug: the loudspeaker stays
    // on after the call (the next call starts there, and CallRoute is not
    // told). Planted-bug: mute stays on after the call.
    void eachCallStartsAfresh()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        QSignalSpy speaker(&b, &Voip::speakerChanged);
        b.setTestTone(true);
        b.setEchoCancelling(false);
        QCOMPARE(b.status().value("test_tone").toBool(), true);
        QCOMPARE(b.status().value("aec").toBool(), false);
        b.handleEvent(invite("c1", 60000, true));
        b.answer();
        b.setSpeaker(true);
        b.setMuted(true);
        QCOMPARE(b.state(), QStringLiteral("connecting"));
        b.hangup();
        QCOMPARE(b.state(), QStringLiteral("idle"));
        QCOMPARE(b.status().value("test_tone").toBool(), false);
        QCOMPARE(b.status().value("aec").toBool(), true);
        QCOMPARE(b.speaker(), false);
        QCOMPARE(b.muted(), false);
        QCOMPARE(speaker.count(), 2);
        QCOMPARE(speaker.last().at(0).toBool(), false);
        // the same when the other side ends it
        b.setSpeaker(true);   // between calls: only the setting
        b.setTestTone(true);
        b.handleEvent(invite("c2", 60000, true));
        b.answer();
        b.handleEvent({ { "type", "m.call.hangup" }, { "room_id", "!r" }, { "sender", "@a:example.org" }, { "own", false },
                        { "content", QVariantMap{ { "call_id", "c2" }, { "party_id", "pa" }, { "version", "1" } } } });
        QCOMPARE(b.state(), QStringLiteral("idle"));
        QCOMPARE(b.status().value("test_tone").toBool(), false);
        QCOMPARE(b.speaker(), false);
        QCOMPARE(speaker.last().at(0).toBool(), false);
    }

    // Planted-bug: the far end's queue not capped (after a burst from the
    // network the echo lies beyond what the canceller learns).
    // Planted-bug: the cap keeps the oldest part instead of the newest.
    void theFarEndWaitsAt120msAtMost()
    {
        QCOMPARE(AecFrames::frameBytes(), 320);       // 10 ms at 16 kHz, 16 bit
        QCOMPARE(AecFrames::farQueueBytes(), 3840);   // 120 ms
        QByteArray burst;
        for (int i = 0; i < 5000; ++i)
            burst.append(char(i % 251));
        QByteArray far;
        AecFrames::queueFar(far, burst.left(1000), AecFrames::farQueueBytes());
        QCOMPARE(far, burst.left(1000));
        AecFrames::queueFar(far, burst.mid(1000), AecFrames::farQueueBytes());
        QCOMPARE(far.size(), 3840);
        QCOMPARE(far, burst.right(3840));
        AecFrames::queueFar(far, QByteArray(100, 'x'), AecFrames::farQueueBytes());
        QCOMPARE(far.size(), 3840);
        QVERIFY(far.endsWith(QByteArray(100, 'x')));
    }

    // Planted-bug: a far frame used twice (not taken off the queue).
    // Planted-bug: the microphone waits for the far end (no silence when
    // none came: the call's own voice stalls). Planted-bug: a part frame
    // of the microphone dropped.
    void eachMicrophoneFrameTakesOneFarFrame()
    {
        const int n = AecFrames::frameBytes();
        QList<QPair<char, char>> seen;   // each frame: the microphone's first byte, the far end's
        bool farSilent = true;
        const auto process = [&](const char *mic, const char *far, char *out) {
            seen << qMakePair(mic[0], far[0]);
            if (far[0] == 0)
                farSilent = farSilent && QByteArray(far, n) == QByteArray(n, '\0');
            for (int i = 0; i < n; ++i)
                out[i] = mic[i];
        };
        QByteArray mic = QByteArray(n, 'a') + QByteArray(n, 'b') + QByteArray(n / 2, 'c');
        QByteArray far = QByteArray(n, '1') + QByteArray(100, '2');
        QByteArray out = AecFrames::pair(mic, far, n, process);
        QCOMPARE(seen, (QList<QPair<char, char>>{ qMakePair('a', '1'), qMakePair('b', '\0') }));
        QVERIFY(farSilent);
        QCOMPARE(out, QByteArray(n, 'a') + QByteArray(n, 'b'));
        QCOMPARE(mic, QByteArray(n / 2, 'c'));   // waits for the rest of its frame
        QCOMPARE(far, QByteArray(100, '2'));     // a part frame of the far end waits too
        mic += QByteArray(n / 2, 'c');
        far += QByteArray(n - 100, '2') + QByteArray(n, '3');
        seen.clear();
        out = AecFrames::pair(mic, far, n, process);
        QCOMPARE(seen, (QList<QPair<char, char>>{ qMakePair('c', '2') }));
        QCOMPARE(out, QByteArray(n, 'c'));
        QVERIFY(mic.isEmpty());
        QCOMPARE(far, QByteArray(n, '3'));
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
