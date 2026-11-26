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
#include "CallKeepAlive.h"

#include <QtTest>

#include <gst/gst.h>

// MCE's keepalive as the test sees it: each hold and release
class Held : public CallKeepAlive
{
public:
    QList<bool> calls;
    bool on = false;
    void holdCpu(bool hold) override
    {
        calls << hold;
        on = hold;
    }
};

// What the call sends, packet by packet: each RTP packet's timestamp
// (bytes 4-7 of its header), from the payloader on; and the microphone
// thread held up now and then, as a busy or sleepy phone does
struct Uplink
{
    QMutex lock;
    QList<quint32> rtp;
    int stalls = 0;
    int buffers = 0;

    static GstPadProbeReturn onRtp(GstPad *, GstPadProbeInfo *info, gpointer self)
    {
        GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
        guint8 head[8];
        if (buf && gst_buffer_extract(buf, 0, head, 8) == 8) {
            Uplink *u = static_cast<Uplink *>(self);
            QMutexLocker l(&u->lock);
            u->rtp << (quint32(head[4]) << 24 | quint32(head[5]) << 16 | quint32(head[6]) << 8 | quint32(head[7]));
        }
        return GST_PAD_PROBE_OK;
    }
    // every third captured buffer waits 15 to 75 ms before the echo
    // canceller gets it (well under the 200 ms queue)
    static GstPadProbeReturn onCapture(GstPad *, GstPadProbeInfo *, gpointer self)
    {
        Uplink *u = static_cast<Uplink *>(self);
        if (++u->buffers % 3 == 0) {
            g_usleep(gulong(15000 + (u->buffers * 7919) % 60000));
            ++u->stalls;
        }
        return GST_PAD_PROBE_OK;
    }
    void attach(GstElement *pipe)
    {
        GstElement *capture = gst_bin_get_by_name(GST_BIN(pipe), "capture");
        GstPad *in = gst_element_get_static_pad(capture, "sink");
        gst_pad_add_probe(in, GST_PAD_PROBE_TYPE_BUFFER, &Uplink::onCapture, this, nullptr);
        gst_object_unref(in);
        gst_object_unref(capture);
        GstIterator *it = gst_bin_iterate_recurse(GST_BIN(pipe));
        GValue item = G_VALUE_INIT;
        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement *e = GST_ELEMENT(g_value_get_object(&item));
            GstElementFactory *f = gst_element_get_factory(e);
            if (f && QByteArray(GST_OBJECT_NAME(f)) == "rtpopuspay") {
                GstPad *out = gst_element_get_static_pad(e, "src");
                gst_pad_add_probe(out, GST_PAD_PROBE_TYPE_BUFFER, &Uplink::onRtp, this, nullptr);
                gst_object_unref(out);
            }
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
    }
};

class Wire : public QObject
{
    Q_OBJECT
public:
    Voip a, b;
    QStringList sent;   // "a:m.call.invite", ...
    bool deliver = true;
    int answerDelayMs = 0;   // the answer's way to the caller (federation lag)

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
        const int delay = type == QLatin1String("m.call.answer") ? qMax(5, answerDelayMs) : 5;
        QTimer::singleShot(delay, to, [to, ev]() { to->handleEvent(ev); });
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

    // An answer that takes a while to reach the caller (23 s from tchncs.de
    // to matrix.org, the author's first test of 0.6): the answerer's ICE
    // gives up after some 8 s without the caller's replies, and the call
    // must still connect once the answer is there.
    // Planted-bug: ICE "failed" ends the call at once.
    void aLateAnswerStillConnects()
    {
        Wire w;
        w.answerDelayMs = 12000;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("connected"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(w.a.status().value("remote_level_db").toDouble() > -40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() > -40, 10000);
        w.a.hangup();
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
    }

    // The caller waits a little past its invite's lifetime: an answer sent
    // in time that comes late (7-10 s between matrix.org and tchncs.de, the
    // caller had ended the call 134 ms before it came) still connects; with
    // no answer at all the call ends after the grace, as invite_timeout.
    // Planted-bug: the invite timer is the bare lifetime (ends at 2 s).
    void anAnswerJustInTimeStillConnects()
    {
        Wire w;
        w.a.setInviteLifetime(2000);
        w.answerDelayMs = 5000;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 20000);
        QVERIFY(w.a.endReason().isEmpty());
        w.a.hangup();
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);

        Wire n;
        n.a.setInviteLifetime(1000);
        QElapsedTimer t;
        t.start();
        n.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(n.a.state(), QStringLiteral("idle"), 1000 + Keys::kVoipAnswerGraceMs + 5000);
        QCOMPARE(n.a.endReason(), QStringLiteral("invite_timeout"));
        QVERIFY(t.elapsed() >= 1000 + Keys::kVoipAnswerGraceMs - 500);
    }

    // The microphone's frames keep their capture time through the echo
    // canceller: with its thread held up now and then, every Opus packet
    // still follows the one before by exactly 20 ms of RTP time (960 at
    // 48 kHz). With the frames stamped when they were pushed, the bursts
    // after each stall made the timestamps wobble: clicks at the far end.
    // Planted-bug: the appsrc stamps at push time (do-timestamp=true,
    // our timestamps dropped).
    void outgoingTimestampsAdvanceOneFrameAtATime()
    {
        Uplink up;   // outlives the pipeline's probes
        Wire w;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_VERIFY_WITH_TIMEOUT(w.a.pipeline(), 5000);
        up.attach(w.a.pipeline());
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(w.b.status().value("remote_level_db").toDouble() > -40, 10000);
        QTest::qWait(3000);
        w.a.hangup();
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
        QMutexLocker l(&up.lock);
        QVERIFY2(up.stalls > 20, qPrintable(QString::number(up.stalls)));
        QVERIFY2(up.rtp.size() > 120, qPrintable(QString::number(up.rtp.size())));
        // in the first second the queue before the encoder drops what
        // comes while the link is set up: whole 10 ms frames missing,
        // honestly shown (480); from then on not one
        QStringList off;
        for (int i = 1; i < up.rtp.size(); ++i) {
            const quint32 step = up.rtp.at(i) - up.rtp.at(i - 1);
            if (i < 50 ? step == 0 || step % 480 : step != 960)
                off << QStringLiteral("%1:%2").arg(i).arg(qint32(step));
        }
        QVERIFY2(off.isEmpty(), qPrintable(QStringLiteral("%1 of %2 steps off: ").arg(off.size()).arg(up.rtp.size())
                                           + QStringList(off.mid(0, 12)).join(QLatin1Char(' '))));
    }

    // The CPU is held from calling or ringing to the end, whichever way
    // it ends, and let go once.
    // Planted-bug: never let go. Planted-bug: held only while the call
    // has audio (not while it rings).
    void theCpuIsHeldForTheCall()
    {
        {
            // calling, connected, hung up here; and ringing, hung up there
            Held a, b;   // outlive the engines
            Wire w;
            w.a.setKeepAlive(&a);
            w.b.setKeepAlive(&b);
            w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
            QCOMPARE(a.calls, QList<bool>{ true });
            QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
            QCOMPARE(b.calls, QList<bool>{ true });
            w.b.answer();
            QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 15000);
            QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("connected"), 15000);
            QCOMPARE(a.calls, QList<bool>{ true });
            QCOMPARE(b.calls, QList<bool>{ true });
            w.a.hangup();
            QCOMPARE(a.calls, (QList<bool>{ true, false }));
            QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
            QCOMPARE(b.calls, (QList<bool>{ true, false }));
        }
        {
            // rejected while ringing; an answer that cannot be sent
            Held k;
            Voip b;
            b.setKeepAlive(&k);
            b.setOwnUserId(QStringLiteral("@b:example.org"));
            b.handleEvent(invite("c1", 60000, true));
            QVERIFY(k.on);
            b.reject();
            QCOMPARE(k.calls, (QList<bool>{ true, false }));
            b.handleEvent(invite("c2", 60000, true));
            b.answer();
            b.sendFailed();
            QCOMPARE(b.state(), QStringLiteral("idle"));
            QCOMPARE(k.calls, (QList<bool>{ true, false, true, false }));
        }
        {
            // gone mid-call (quitting while it rings never reaches idle)
            Held k;
            {
                Voip b;
                b.setKeepAlive(&k);
                b.setOwnUserId(QStringLiteral("@b:example.org"));
                b.handleEvent(invite("c1", 60000, true));
                QVERIFY(k.on);
            }
            QCOMPARE(k.calls, (QList<bool>{ true, false }));
        }
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
        QVERIFY(!b.declinedElsewhere());

        // declined on another device: the same end on the call page, but a
        // missed call for the notifications (Service.qml)
        // Planted-bug: an own reject is not told from an own answer
        const QVariantMap again{ { "type", "m.call.invite" }, { "room_id", "!dm" }, { "sender", "@a:example.org" }, { "own", false },
                                 { "age_ms", 0 }, { "is_dm", true },
                                 { "content", QVariantMap{ { "call_id", "c2" }, { "party_id", "pa" }, { "version", "1" }, { "lifetime", 60000 },
                                                           { "offer", QVariantMap{ { "type", "offer" }, { "sdp", "v=0" } } } } } };
        b.handleEvent(again);
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        QVERIFY(!b.declinedElsewhere());
        b.handleEvent({ { "type", "m.call.reject" }, { "room_id", "!dm" }, { "sender", "@b:example.org" }, { "own", true },
                        { "content", QVariantMap{ { "call_id", "c2" }, { "party_id", "other-device" }, { "version", "1" } } } });
        QCOMPARE(b.endReason(), QStringLiteral("answered_elsewhere"));
        QVERIFY(b.declinedElsewhere());
        // and a new call starts without it
        QVariantMap third = again;
        QVariantMap c3 = third.value("content").toMap();
        c3.insert("call_id", "c3");
        third.insert("content", c3);
        b.handleEvent(third);
        QVERIFY(!b.declinedElsewhere());
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
        // still bound to the caller after the handover
        // Planted-bug: takeRinging leaves the peer unbound
        app.handleEvent({ { "type", "m.call.hangup" }, { "room_id", "!dm" }, { "sender", "@c:example.org" }, { "own", false },
                          { "content", QVariantMap{ { "call_id", "c1" }, { "version", "1" } } } });
        QCOMPARE(app.state(), QStringLiteral("incoming"));

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
        // the ring is what is left of the invite's lifetime (the C2 rang a
        // flat minute for an invite that came 28 s old, and its answer at
        // 46 s came after the caller had given up)
        // Planted-bug: incoming carries the whole lifetime, not lifetime - age
        Voip e;
        e.setOwnUserId(QStringLiteral("@b:example.org"));
        QSignalSpy rings(&e, &Voip::incoming);
        QVariantMap old = invite("c4", 60000, true);
        old.insert("age_ms", 28000);
        e.handleEvent(old);
        QCOMPARE(rings.count(), 1);
        QCOMPARE(rings.at(0).at(3).toInt(), 32000);
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

    // The canceller's output keeps the capture's time: counted on in
    // samples whatever the thread's timing, a part frame carried over, and
    // only a real gap (a frame or more missing) starts the count again.
    // Planted-bug: the count follows every buffer's time (jitter goes
    // through). Planted-bug: the part frame left out of the count (each
    // frame 5 ms early). Planted-bug: a gap not marked.
    void cleanFramesKeepTheCaptureTime()
    {
        const qint64 ms = 1000000;
        const int n = AecFrames::frameBytes();   // 10 ms
        const auto none = [](const char *mic, const char *, char *out) { memcpy(out, mic, size_t(AecFrames::frameBytes())); };
        AecFrames::Clock c;
        QByteArray mic, far;
        // 15 ms at 1000 ms: one frame out, stamped 1000, 5 ms wait
        QVERIFY(!c.captured(1000 * ms, n * 3 / 2, mic));
        mic += QByteArray(n * 3 / 2, 'a');
        QByteArray out = AecFrames::pair(mic, far, n, none);
        QCOMPARE(out.size(), n);
        QCOMPARE(c.ptsOf(out, mic), 1000 * ms);
        QVERIFY(!c.discont);
        // the next 15 ms, handed over 6 ms late (the thread): 1010, 1020
        QVERIFY(!c.captured(1021 * ms, n * 3 / 2, mic));
        mic += QByteArray(n * 3 / 2, 'b');
        out = AecFrames::pair(mic, far, n, none);
        QCOMPARE(out.size(), 2 * n);
        QCOMPARE(c.ptsOf(out, mic), 1010 * ms);
        QVERIFY(mic.isEmpty());
        // and one stamped early by the same (never back in time)
        QVERIFY(!c.captured(1025 * ms, n, mic));
        mic += QByteArray(n, 'c');
        out = AecFrames::pair(mic, far, n, none);
        QCOMPARE(c.ptsOf(out, mic), 1030 * ms);
        // 5 ms, then 30 ms missing: the part frame goes, the count starts
        // at the new buffer, and it is marked
        QVERIFY(!c.captured(1040 * ms, n / 2, mic));
        mic += QByteArray(n / 2, 'd');
        out = AecFrames::pair(mic, far, n, none);
        QVERIFY(out.isEmpty());
        QVERIFY(c.captured(1075 * ms, n, mic));
        QVERIFY(mic.isEmpty());
        mic += QByteArray(n, 'e');
        out = AecFrames::pair(mic, far, n, none);
        QCOMPARE(out, QByteArray(n, 'e'));
        QCOMPARE(c.ptsOf(out, mic), 1075 * ms);
        QVERIFY(c.discont);
        // no time at all: counted on
        c.discont = false;
        QVERIFY(!c.captured(-1, n, mic));
        mic += QByteArray(n, 'f');
        out = AecFrames::pair(mic, far, n, none);
        QCOMPARE(c.ptsOf(out, mic), 1085 * ms);
        QVERIFY(!c.discont);
    }

    // NET-5: before the answer, only the person called counts.
    // Planted-bug: place() leaves the peer open (a third member's hangup,
    // reject or candidates for the ringing call are taken).
    void othersCannotEndACallBeforeTheAnswer()
    {
        Voip a;
        a.setOwnUserId(QStringLiteral("@a:example.org"));
        a.place(QStringLiteral("!r"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QCOMPARE(a.state(), QStringLiteral("outgoing"));
        const QString callId = a.status().value("call_id").toString();
        auto ev = [&callId](const char *type, const char *sender, QVariantMap extra = QVariantMap()) {
            extra.insert("call_id", callId);
            extra.insert("party_id", "pc");
            extra.insert("version", "1");
            return QVariantMap{ { "type", type }, { "room_id", "!r" }, { "sender", sender }, { "own", false },
                                { "content", extra } };
        };
        const QVariantList one{ QVariantMap{ { "candidate", "candidate:1 1 UDP 2015 198.51.100.9 40000 typ host" },
                                             { "sdpMLineIndex", 0 } } };
        a.handleEvent(ev("m.call.candidates", "@c:example.org", { { "candidates", one } }));
        QCOMPARE(a.status().value("remote_queued").toInt(), 0);
        a.handleEvent(ev("m.call.reject", "@c:example.org"));
        QCOMPARE(a.state(), QStringLiteral("outgoing"));
        a.handleEvent(ev("m.call.hangup", "@c:example.org"));
        QCOMPARE(a.state(), QStringLiteral("outgoing"));
        // the invitee's own still count
        a.handleEvent(ev("m.call.candidates", "@b:example.org", { { "candidates", one } }));
        QCOMPARE(a.status().value("remote_queued").toInt(), 1);
        a.handleEvent(ev("m.call.reject", "@b:example.org"));
        QCOMPARE(a.state(), QStringLiteral("idle"));
        QCOMPARE(a.endReason(), QStringLiteral("rejected"));
    }

    // NET-9. Planted-bug: no cap on the queued candidates.
    void remoteCandidatesAreCapped()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        b.handleEvent(invite("c1", 60000, true));
        QVariantList many;
        for (int i = 0; i < Voip::kMaxRemoteCandidates + 100; ++i)
            many << QVariantMap{ { "candidate", QStringLiteral("candidate:%1 1 UDP 2015 198.51.100.9 %2 typ host").arg(i).arg(40000 + i) },
                                 { "sdpMLineIndex", 0 } };
        b.handleEvent({ { "type", "m.call.candidates" }, { "room_id", "!r" }, { "sender", "@a:example.org" }, { "own", false },
                        { "content", QVariantMap{ { "call_id", "c1" }, { "party_id", "pa" }, { "candidates", many } } } });
        QCOMPARE(b.status().value("remote_queued").toInt(), Voip::kMaxRemoteCandidates);
        b.hangup();
    }

    // NET-9. Planted-bug: no size check on the far end's SDP (a huge one
    // goes to the parser and on).
    void aHugeRemoteSdpEndsTheCall()
    {
        Voip b;
        b.setOwnUserId(QStringLiteral("@b:example.org"));
        QVariantMap inv = invite("c1", 60000, true);
        QVariantMap content = inv.value("content").toMap();
        QString sdp = QStringLiteral("v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\ns=-\r\nt=0 0\r\n");
        while (sdp.size() <= Voip::kMaxRemoteSdp)
            sdp += QStringLiteral("a=x-padding:0123456789012345678901234567890123456789\r\n");
        content.insert("offer", QVariantMap{ { "type", "offer" }, { "sdp", sdp } });
        inv.insert("content", content);
        b.handleEvent(inv);
        QCOMPARE(b.state(), QStringLiteral("incoming"));
        b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(b.state(), QStringLiteral("idle"), 5000);
        QCOMPARE(b.endReason(), QStringLiteral("ice_failed"));
    }

    // NET-9: only an RTP Opus audio pad gets the player.
    // Planted-bug: isFarAudio takes any caps.
    void onlyOpusAudioIsPlayed()
    {
        gst_init(nullptr, nullptr);
        auto takes = [](const char *text) {
            GstCaps *caps = gst_caps_from_string(text);
            const bool ok = Voip::isFarAudio(caps);
            gst_caps_unref(caps);
            return ok;
        };
        QVERIFY(takes("application/x-rtp, media=(string)audio, encoding-name=(string)OPUS, clock-rate=(int)48000"));
        QVERIFY(takes("application/x-rtp, media=(string)audio, encoding-name=(string)opus"));
        QVERIFY(!takes("application/x-rtp, media=(string)video, encoding-name=(string)VP8"));
        QVERIFY(!takes("application/x-rtp, media=(string)audio, encoding-name=(string)PCMU"));
        QVERIFY(!takes("audio/x-opus"));
        QVERIFY(!Voip::isFarAudio(nullptr));
    }

    // NET-9, in a real call: a candidate for an m-line the description
    // does not have is dropped. Planted-bug: the index is taken as given.
    void candidatesForMissingMlinesAreDropped()
    {
        Wire w;
        w.a.place(QStringLiteral("!dm:example.org"), QStringLiteral("@b:example.org"), QStringLiteral("B"));
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("incoming"), 5000);
        w.b.answer();
        QTRY_COMPARE_WITH_TIMEOUT(w.a.state(), QStringLiteral("connected"), 15000);
        const QString callId = w.b.status().value("call_id").toString();
        const int before = w.b.status().value("remote_candidates").toInt();
        auto send = [&](int index) {
            const QVariantList c{ QVariantMap{ { "candidate", "candidate:9 1 UDP 2015 198.51.100.9 40009 typ host" },
                                               { "sdpMLineIndex", index } } };
            w.b.handleEvent({ { "type", "m.call.candidates" }, { "room_id", "!dm:example.org" }, { "sender", "@a:example.org" },
                              { "own", false }, { "content", QVariantMap{ { "call_id", callId }, { "candidates", c } } } });
        };
        send(7);
        QCOMPARE(w.b.status().value("remote_candidates").toInt(), before);
        send(0);
        QCOMPARE(w.b.status().value("remote_candidates").toInt(), before + 1);
        w.a.hangup();
        QTRY_COMPARE_WITH_TIMEOUT(w.b.state(), QStringLiteral("idle"), 3000);
    }

    // Planted-bug: the credentials go in unescaped (an "@" in the password
    // breaks the address).
    // Relay only: what goes out names no address of the phone's own.
    // Planted-bug: relayOnlySdp keeps every candidate line.
    void relayOnlyKeepsOnlyRelays()
    {
        QVERIFY(Voip::isRelayCandidate(QStringLiteral("candidate:1 1 UDP 5 203.0.113.7 50000 typ relay raddr 0.0.0.0 rport 0")));
        QVERIFY(!Voip::isRelayCandidate(QStringLiteral("candidate:2 1 UDP 2015 192.168.1.20 40000 typ host")));
        QVERIFY(!Voip::isRelayCandidate(QStringLiteral("candidate:3 1 UDP 1677 198.51.100.4 40000 typ srflx raddr 192.168.1.20 rport 40000")));
        const QString sdp = QStringLiteral("v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                                           "a=candidate:2 1 UDP 2015 192.168.1.20 40000 typ host\r\n"
                                           "a=candidate:1 1 UDP 5 203.0.113.7 50000 typ relay raddr 0.0.0.0 rport 0\r\n"
                                           "a=mid:audio0\r\n");
        const QString out = Voip::relayOnlySdp(sdp);
        QVERIFY(!out.contains(QStringLiteral("192.168.1.20")));
        QVERIFY(out.contains(QStringLiteral("typ relay")));
        QVERIFY(out.contains(QStringLiteral("a=mid:audio0\r\n")));
        QCOMPARE(Voip::mids(out), QStringList{ QStringLiteral("audio0") });
    }

    // The log names candidate types, never addresses.
    // Planted-bug: candidateType takes the token after the address.
    void candidateTypes()
    {
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:1 1 UDP 5 203.0.113.7 50000 typ relay raddr 0.0.0.0 rport 0")),
                 QStringLiteral("relay"));
        QCOMPARE(Voip::candidateType(QStringLiteral("a=candidate:2 1 UDP 2015 192.168.1.20 40000 typ host")), QStringLiteral("host"));
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:3 1 UDP 1677 198.51.100.4 40000 typ srflx raddr 192.168.1.20 rport 40000")),
                 QStringLiteral("srflx"));
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:4 1 TCP 1 2001:db8::1 9 typ prflx tcptype active")), QStringLiteral("prflx"));
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:5 1 UDP 1 192.0.2.1 9")), QString());
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:6 1 UDP 1 192.0.2.1 9 typ")), QString());
        // the far end's own word is not logged
        QCOMPARE(Voip::candidateType(QStringLiteral("candidate:7 1 UDP 1 192.0.2.1 9 typ 198.51.100.9")), QString());
    }

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
