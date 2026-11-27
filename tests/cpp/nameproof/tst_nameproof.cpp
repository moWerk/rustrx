// SPDX-License-Identifier: GPL-3.0-or-later
// Whether the holder of one of Rustrx's bus names is a Rustrx (NameProof.h,
// audit 0.8.4 IPC2 and IPC6), on a private bus: a holder with the data
// folder's secret proves it, one without (no answer, a wrong one) does
// not, a Rustrx answers only for a name it holds itself, a free name is
// told as free, and the note "once per boot".
#include "NameProof.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

Q_DECLARE_METATYPE(NameProof::Holder)

class TestNameProof : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QString m_address;
    QTemporaryDir m_dir;
    int m_conn = 0;

    QString file(const char *name) const { return m_dir.path() + QLatin1Char('/') + QLatin1String(name); }
    QDBusConnection connect()
    {
        return QDBusConnection::connectToBus(m_address, QStringLiteral("c%1").arg(++m_conn));
    }
    NameProof::Holder check(QDBusConnection asker, const QString &name, const QString &secret, int waitMs,
                            bool untilFree = false, qint64 *ms = nullptr)
    {
        NameProof::Check c(asker, name, secret, waitMs, untilFree);
        QSignalSpy done(&c, &NameProof::Check::done);
        QElapsedTimer t;
        t.start();
        c.start();
        if (!done.wait(waitMs + 5000))
            return NameProof::Holder::Free;
        if (ms)
            *ms = t.elapsed();
        return done.first().first().value<NameProof::Holder>();
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<NameProof::Holder>();
        m_daemon.start(QStringLiteral("dbus-daemon"), { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address") });
        if (!m_daemon.waitForStarted(3000))
            QSKIP("no dbus-daemon on this host");
        QTRY_VERIFY_WITH_TIMEOUT(m_daemon.canReadLine(), 5000);
        m_address = QString::fromLatin1(m_daemon.readLine().trimmed());
        QVERIFY(!m_address.isEmpty());
    }
    void cleanupTestCase()
    {
        for (int i = 1; i <= m_conn; i++)
            QDBusConnection::disconnectFromBus(QStringLiteral("c%1").arg(i));
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }

    // Planted-bug: answer() leaves the owner or the name out (a relayed
    // answer from another Rustrx would pass)
    void answerIsBound()
    {
        const QString s = NotifyGate::secret(file("bound-secret"));
        const QString a = NameProof::answer(s, QStringLiteral("n"), QStringLiteral(":1.5"), QStringLiteral("x"));
        QCOMPARE(a.size(), 64);
        QVERIFY(a != NameProof::answer(s, QStringLiteral("n"), QStringLiteral(":1.6"), QStringLiteral("x")));
        QVERIFY(a != NameProof::answer(s, QStringLiteral("m"), QStringLiteral(":1.5"), QStringLiteral("x")));
        QVERIFY(a != NameProof::answer(s, QStringLiteral("n"), QStringLiteral(":1.5"), QStringLiteral("y")));
        QVERIFY(a != NameProof::answer(s + QLatin1Char('0'), QStringLiteral("n"), QStringLiteral(":1.5"), QStringLiteral("x")));
        QCOMPARE(NameProof::answer(QString(), QStringLiteral("n"), QStringLiteral(":1.5"), QStringLiteral("x")), QString());
        // the stand-in's leave token: bound to its unique name, not the secret itself
        QVERIFY(NameProof::leaveToken(s, QStringLiteral(":1.5")) != NameProof::leaveToken(s, QStringLiteral(":1.6")));
        QVERIFY(NameProof::leaveToken(s, QStringLiteral(":1.5")) != s);
        QCOMPARE(NameProof::nonce().size(), 64);
        QVERIFY(NameProof::nonce() != NameProof::nonce());
    }

    // Planted-bug: Check takes any answer (or none) as proof
    void aRustrxProvesItAndASquatterDoesNot()
    {
        const QString name = QStringLiteral("net.mowerk.test.one");
        const QString secret = NotifyGate::secret(file("one-secret"));
        QDBusConnection holder = connect();
        QDBusConnection asker = connect();
        NameProof::Responder responder(file("one-secret"), { name });
        QVERIFY(holder.registerObject(QString::fromLatin1(Keys::kProofPath), &responder, QDBusConnection::ExportScriptableSlots));
        QVERIFY(holder.registerService(name));
        QCOMPARE(check(asker, name, secret, 3000), NameProof::Holder::Rustrx);
        // the asker holding it itself
        QCOMPARE(check(holder, name, secret, 3000), NameProof::Holder::Rustrx);

        // another secret: a wrong proof, told at once
        const QString other = NotifyGate::secret(file("other-secret"));
        qint64 ms = 0;
        QCOMPARE(check(asker, name, other, 3000, false, &ms), NameProof::Holder::Other);
        QVERIFY(ms < 2500);

        // a squatter with no proof object: none within the wait
        const QString squatted = QStringLiteral("net.mowerk.test.squatted");
        QDBusConnection squatter = connect();
        QVERIFY(squatter.registerService(squatted));
        QCOMPARE(check(asker, squatted, secret, 600), NameProof::Holder::Other);
    }

    // Planted-bug: proveName answers for a name the process does not hold
    // (a squatter could ask the real Rustrx and pass the answer on)
    void aRustrxAnswersOnlyForItsOwnName()
    {
        const QString mine = QStringLiteral("net.mowerk.test.mine");
        const QString theirs = QStringLiteral("net.mowerk.test.theirs");
        QDBusConnection rustrx = connect();
        QDBusConnection squatter = connect();
        QDBusConnection asker = connect();
        NameProof::Responder responder(file("own-secret"), { mine, theirs });
        QVERIFY(rustrx.registerObject(QString::fromLatin1(Keys::kProofPath), &responder, QDBusConnection::ExportScriptableSlots));
        QVERIFY(rustrx.registerService(mine));
        QVERIFY(squatter.registerService(theirs));
        QDBusMessage ask = QDBusMessage::createMethodCall(rustrx.baseService(), QString::fromLatin1(Keys::kProofPath),
                                                          QString::fromLatin1(Keys::kProofInterface), QStringLiteral("proveName"));
        ask << theirs << NameProof::nonce();
        QDBusPendingCall p = asker.asyncCall(ask, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(p.isFinished(), 4000);
        QCOMPARE(p.reply().arguments().value(0).toString(), QString());
        // a short nonce gets nothing either
        QDBusMessage shortNonce = QDBusMessage::createMethodCall(rustrx.baseService(), QString::fromLatin1(Keys::kProofPath),
                                                                 QString::fromLatin1(Keys::kProofInterface), QStringLiteral("proveName"));
        shortNonce << mine << QStringLiteral("abc");
        QDBusPendingCall q = asker.asyncCall(shortNonce, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(q.isFinished(), 4000);
        QCOMPARE(q.reply().arguments().value(0).toString(), QString());
        // a name outside its list neither, held or not
        QVERIFY(rustrx.registerService(QStringLiteral("net.mowerk.test.unlisted")));
        QDBusMessage unlisted = QDBusMessage::createMethodCall(rustrx.baseService(), QString::fromLatin1(Keys::kProofPath),
                                                               QString::fromLatin1(Keys::kProofInterface), QStringLiteral("proveName"));
        unlisted << QStringLiteral("net.mowerk.test.unlisted") << NameProof::nonce();
        QDBusPendingCall r = asker.asyncCall(unlisted, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(r.isFinished(), 4000);
        QCOMPARE(r.reply().arguments().value(0).toString(), QString());
    }

    // Planted-bug: untilFree returns at the first proof (the leaving
    // Rustrx's name is never taken over)
    void aFreeNameAndOneLetGo()
    {
        const QString name = QStringLiteral("net.mowerk.test.leaving");
        const QString secret = NotifyGate::secret(file("leave-secret"));
        QDBusConnection asker = connect();
        QCOMPARE(check(asker, name, secret, 1000), NameProof::Holder::Free);
        QDBusConnection leaving = connect();
        NameProof::Responder responder(file("leave-secret"), { name });
        QVERIFY(leaving.registerObject(QString::fromLatin1(Keys::kProofPath), &responder, QDBusConnection::ExportScriptableSlots));
        QVERIFY(leaving.registerService(name));
        QTimer::singleShot(400, [&leaving, name]() { leaving.unregisterService(name); });
        QCOMPARE(check(asker, name, secret, 4000, true), NameProof::Holder::Free);
        // still held at the end of the wait: a Rustrx
        QVERIFY(leaving.registerService(name));
        QCOMPARE(check(asker, name, secret, 800, true), NameProof::Holder::Rustrx);
    }

    // Planted-bug: firstThisBoot always true (a notification at every push)
    void oncePerBoot()
    {
        const QString note = file("name-taken-boot");
        const QString boot = file("boot_id");
        QFile b(boot);
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("aaaa-1\n");
        b.close();
        QVERIFY(NameProof::firstThisBoot(note, boot));
        QVERIFY(!NameProof::firstThisBoot(note, boot));
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(note).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 0777), 0600);
        QVERIFY(b.open(QIODevice::WriteOnly | QIODevice::Truncate));
        b.write("bbbb-2\n");
        b.close();
        QVERIFY(NameProof::firstThisBoot(note, boot));
        QVERIFY(!NameProof::firstThisBoot(note, boot));
        // no boot id: once a day
        const QString note2 = file("name-taken-noboot");
        QVERIFY(NameProof::firstThisBoot(note2, file("none")));
        QVERIFY(!NameProof::firstThisBoot(note2, file("none")));
    }
};

QTEST_GUILESS_MAIN(TestNameProof)
#include "tst_nameproof.moc"
