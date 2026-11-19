// SPDX-License-Identifier: GPL-3.0-or-later
// src/CallRoutePlan.h: which card, sink and port a call takes and where the
// sink goes back to (audit T4). src/CallRoute.cpp: nothing waits on the
// calling thread (audit B7): against a PulseAudio that never answers (a
// socket nobody serves) the calls return at once, the route gives up
// after its time limit, and destroying it while a step hangs is bounded.
// Once more against this machine's own sound server, where there is one:
// the steps run through to the end (a desktop has no communication mode,
// so nothing is switched).
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QElapsedTimer>
#include <QLocalServer>
#include <QMutex>
#include <QTemporaryDir>
#include <QtTest>
#include "CallRoute.h"
#include "Keys.h"

using namespace CallRoutePlan;

static QMutex s_lock;
static QStringList s_log;
static QtMessageHandler s_previous = nullptr;

static void capture(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        QMutexLocker lock(&s_lock);
        s_log << message;
    }
    if (s_previous)
        s_previous(type, context, message);
}

static int logged(const QString &part)
{
    QMutexLocker lock(&s_lock);
    int n = 0;
    for (const QString &line : s_log)
        n += line.contains(part) ? 1 : 0;
    return n;
}

class TestCallRoute : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { s_previous = qInstallMessageHandler(capture); }
    // The mainline Pro1-X's card as pactl lists it (2026-11-19): the call
    // goes to the HiFi earpiece profile, never a Voice Call one, the
    // speaker button to HiFi's speaker, headphones stay headphones.
    // Planted-bug: ucmProfile matches "Voice Call (Earpiece, Mic)" first.
    void mainlineProfiles()
    {
        const CallRoutePlan::Card pro1x{ QStringLiteral("alsa_card.Pro1X"), QStringLiteral("HiFi (Mic, Speaker)"),
            { QStringLiteral("Voice Call (Earpiece, Mic)"), QStringLiteral("Voice Call (Headphones, Mic)"),
              QStringLiteral("Voice Call (Mic, Speaker)"), QStringLiteral("HiFi (Earpiece, Mic)"),
              QStringLiteral("HiFi (Headphones, Mic)"), QStringLiteral("HiFi (Mic, Speaker)"), QStringLiteral("off") } };
        const CallRoutePlan::Card droid{ QStringLiteral("droid_card.primary"), QStringLiteral("default"),
            { QStringLiteral("default"), QStringLiteral("communication") } };
        QCOMPARE(CallRoutePlan::ucmCard({ droid }), -1);
        QCOMPARE(CallRoutePlan::ucmCard({ droid, pro1x }), 1);
        QCOMPARE(CallRoutePlan::card({ pro1x }), -1);
        QCOMPARE(CallRoutePlan::ucmCallProfile(pro1x, pro1x.active, false), QStringLiteral("HiFi (Earpiece, Mic)"));
        QCOMPARE(CallRoutePlan::ucmCallProfile(pro1x, pro1x.active, true), QStringLiteral("HiFi (Mic, Speaker)"));
        QCOMPARE(CallRoutePlan::ucmCallProfile(pro1x, QStringLiteral("HiFi (Headphones, Mic)"), false),
                 QStringLiteral("HiFi (Headphones, Mic)"));
    }

    void init()
    {
        QMutexLocker lock(&s_lock);
        s_log.clear();
    }

    // Planted-bug: a card counts without its communication profile (a
    // desktop's card would be switched).
    void theCardWithACommunicationMode()
    {
        QCOMPARE(card({}), -1);
        QCOMPARE(card({ Card{ "alsa", "output:analog-stereo", { "output:analog-stereo", "off" } } }), -1);
        QCOMPARE(card({ Card{ "alsa", "a", { "a" } }, Card{ "droid_card.primary", "default", { "default", "communication" } } }), 1);
        // not by a part of the name
        QCOMPARE(card({ Card{ "x", "a", { "communication-hfp" } } }), -1);
    }

    // Planted-bug: the earpiece taken over a plugged headset. Planted-bug:
    // a headset port taken that is there but unplugged.
    void thePortTheCallIsHeardOn()
    {
        const QList<Sink> sinks{ Sink{ "hdmi", "hdmi-out", { "hdmi-out" }, { "hdmi-out" } },
                                 Sink{ "sink.primary", "output-speaker",
                                       { "output-speaker", "output-earpiece", "output-wired_headset" },
                                       { "output-speaker", "output-earpiece" } } };
        QCOMPARE(sink(sinks), 1);
        QCOMPARE(sink({ sinks.first() }), -1);
        QCOMPARE(headsetPort(sinks.at(1)), QString());   // unplugged
        Sink plugged = sinks.at(1);
        plugged.available << "output-wired_headset";
        QCOMPARE(headsetPort(plugged), QStringLiteral("output-wired_headset"));
        Sink phones = sinks.at(1);
        phones.available << "output-wired_headphone";
        QCOMPARE(headsetPort(phones), QStringLiteral("output-wired_headphone"));
        QCOMPARE(callPort(false, QString()), QStringLiteral("output-earpiece"));
        QCOMPARE(callPort(false, "output-wired_headset"), QStringLiteral("output-wired_headset"));
        QCOMPARE(callPort(true, "output-wired_headset"), QStringLiteral("output-speaker"));
        QCOMPARE(callPort(true, QString()), QStringLiteral("output-speaker"));
    }

    // Planted-bug: "parking" restored as it was (no output at all after
    // the call).
    void whereTheSinkGoesBack()
    {
        QCOMPARE(restorePort("output-parking"), QStringLiteral("output-speaker"));
        QCOMPARE(restorePort(QString()), QStringLiteral("output-speaker"));
        QCOMPARE(restorePort("output-wired_headset"), QStringLiteral("output-wired_headset"));
        QCOMPARE(restorePort("output-earpiece"), QStringLiteral("output-earpiece"));
    }

    // Planted-bug: no time limit (the route stays busy for good and a
    // speaker toggle never comes). Planted-bug: setActive waits for the
    // route (the GUI freezes for the time limit).
    void aHungPulseAudioHoldsNothingUp()
    {
        QTemporaryDir dir;
        QLocalServer silent;   // listens, never answers
        QVERIFY(silent.listen(dir.path() + "/native"));
        qputenv("PULSE_SERVER", ("unix:" + dir.path() + "/native").toUtf8());
        CallRoute route;
        QElapsedTimer t;
        t.start();
        route.setActive(true);
        route.setSpeaker(true);
        route.setSpeaker(false);
        QVERIFY2(t.elapsed() < 200, qPrintable(QString::number(t.elapsed())));
        QVERIFY(!route.idle());
        QTRY_VERIFY_WITH_TIMEOUT(route.idle(), Keys::kCallRouteTimeoutMs + 3000);
        QCOMPARE(logged("call route given up"), 1);
        QVERIFY(t.elapsed() >= Keys::kCallRouteTimeoutMs - 100);
        // the end of the call: nothing was switched, nothing to wait for
        t.restart();
        route.setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(route.idle(), 1000);
        QVERIFY(t.elapsed() < 1000);
        qunsetenv("PULSE_SERVER");
    }

    // Planted-bug: the destructor waits for the hung step (quitting
    // during a call hangs). Run under valgrind or ASan for the rest.
    void destroyedWhileAStepHangs()
    {
        QTemporaryDir dir;
        QLocalServer silent;
        QVERIFY(silent.listen(dir.path() + "/native"));
        qputenv("PULSE_SERVER", ("unix:" + dir.path() + "/native").toUtf8());
        QElapsedTimer t;
        {
            CallRoute route;
            route.setActive(true);
            QTest::qWait(100);
            t.start();
        }
        QVERIFY2(t.elapsed() < Keys::kCallRouteQuitWaitMs + 500, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(logged("still busy on quitting"), 1);
        // and right after creating it, before PulseAudio's thread ran
        t.restart();
        {
            CallRoute route;
            route.setSpeaker(true);
            route.setActive(true);
        }
        QVERIFY2(t.elapsed() < Keys::kCallRouteQuitWaitMs + 500, qPrintable(QString::number(t.elapsed())));
        qunsetenv("PULSE_SERVER");
    }

    // Planted-bug: the cards' list never reaches its end handler (the
    // route stays busy). Skipped where no sound server runs (CI).
    void thisMachinesSoundServer()
    {
        CallRoute route;
        route.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(route.idle(), Keys::kCallRouteTimeoutMs + 3000);
        if (logged("call route given up") && logged("at connecting"))
            QSKIP("no sound server here");
        QCOMPARE(logged("call route given up"), 0);
        QCOMPARE(logged("no communication mode") + logged("call route on"), 1);
        route.setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(route.idle(), Keys::kCallRouteTimeoutMs + 3000);
        QCOMPARE(logged("call route given up"), 0);
    }
};

QTEST_GUILESS_MAIN(TestCallRoute)
#include "tst_callroute.moc"
