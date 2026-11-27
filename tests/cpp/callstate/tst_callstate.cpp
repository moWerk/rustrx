// SPDX-License-Identifier: GPL-3.0-or-later
// When Rustrx darkens the screen during a call: on the earpiece, at the
// ear, while the call is set up or runs. (The requests go to the system
// bus's MCE; on a machine without it they go nowhere.)
#include "CallState.h"

#include <QtTest>
#include <QSignalSpy>

class TestCallState : public QObject
{
    Q_OBJECT
private slots:
    void darkAtTheEarDuringACall()
    {
        CallState s;
        s.follow(QStringLiteral("connected"));
        s.setNear(true);
        QVERIFY(s.blanked());
        s.setNear(false);
        QVERIFY(!s.blanked());
    }
    void neverWhileItRings()
    {
        CallState s;
        s.follow(QStringLiteral("incoming"));
        s.setNear(true);
        QVERIFY(!s.blanked());
        s.follow(QStringLiteral("connecting"));
        QVERIFY(s.blanked());
    }
    void neverOnTheSpeaker()
    {
        CallState s;
        s.follow(QStringLiteral("connected"));
        s.setNear(true);
        s.setSpeaker(true);
        QVERIFY(!s.blanked());
        s.setSpeaker(false);
        QVERIFY(s.blanked());
    }
    void onAgainWhenTheCallEnds()
    {
        CallState s;
        s.follow(QStringLiteral("outgoing"));
        s.setNear(true);
        QVERIFY(s.blanked());
        s.follow(QStringLiteral("idle"));
        QVERIFY(!s.blanked());
    }
    // MCE's tklock modes: every locked one counts, the unlocked ones not
    // (a ringing call's page goes over the lock screen only when it is up).
    // Planted-bug: count only "locked" (a dimmed lock screen let the ring
    // go to the window behind it)
    void lockScreenModes()
    {
        CallState s;
        QSignalSpy changed(&s, &CallState::screenLockedChanged);
        for (const char *m : { "locked", "silent-locked", "locked-dim", "locked-delay", "silent-locked-dim" }) {
            s.setLockMode(QStringLiteral("unlocked"));
            QVERIFY(!s.screenLocked());
            s.setLockMode(QString::fromLatin1(m));
            QVERIFY2(s.screenLocked(), m);
        }
        s.setLockMode(QStringLiteral("silent-unlocked"));
        QVERIFY(!s.screenLocked());
        const int n = changed.count();
        s.setLockMode(QStringLiteral("unlocked"));
        QCOMPARE(changed.count(), n);
    }
};

QTEST_GUILESS_MAIN(TestCallState)
#include "tst_callstate.moc"
