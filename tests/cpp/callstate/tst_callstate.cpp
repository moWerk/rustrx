// SPDX-License-Identifier: GPL-3.0-or-later
// When Rustrx darkens the screen during a call: on the earpiece, at the
// ear, while the call is set up or runs. (The requests go to the system
// bus's MCE; on a machine without it they go nowhere.)
#include "CallState.h"

#include <QtTest>

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
};

QTEST_GUILESS_MAIN(TestCallState)
#include "tst_callstate.moc"
