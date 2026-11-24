// SPDX-License-Identifier: GPL-3.0-or-later
// Who may use a notification's actions (NotifyGate.h, audit 0.8.4 IPC1):
// on a private bus, a call from another connection is refused, one from
// the owner of org.freedesktop.Notifications taken; the list of posted
// notifications and the data folder's secret.
#include "NotifyGate.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

// stands in for Rustrx: an exported slot behind the gate
class Target : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.test")
public:
    int taken = 0;
    bool direct() { return act(); }
public slots:
    Q_SCRIPTABLE bool act()
    {
        if (!NotifyGate::fromServer(*this, "test"))
            return false;
        ++taken;
        return true;
    }
};

class TestNotifyGate : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QString m_address;
    QTemporaryDir m_dir;

    // a call to the target from the connection `from`, the event loop running
    // (the target answers on this thread)
    QVariant callFrom(QDBusConnection from)
    {
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("net.mowerk.test.target"), QStringLiteral("/t"),
                                                        QStringLiteral("net.mowerk.test"), QStringLiteral("act"));
        QDBusPendingCall p = from.asyncCall(m, 5000);
        QElapsedTimer t;
        t.start();
        while (!p.isFinished() && t.elapsed() < 6000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QDBusMessage r = p.reply();
        if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty())
            return QVariant();
        return r.arguments().first();
    }
    QString file(const char *name) const { return m_dir.path() + QLatin1Char('/') + QLatin1String(name); }

private slots:
    void initTestCase()
    {
        m_daemon.start(QStringLiteral("dbus-daemon"), { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address") });
        if (!m_daemon.waitForStarted(3000))
            QSKIP("no dbus-daemon on this host");
        QTRY_VERIFY_WITH_TIMEOUT(m_daemon.canReadLine(), 5000);
        m_address = QString::fromLatin1(m_daemon.readLine().trimmed());
        QVERIFY(!m_address.isEmpty());
    }
    void cleanupTestCase()
    {
        for (const char *n : { "target", "other", "server" })
            QDBusConnection::disconnectFromBus(QLatin1String(n));
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }

    void caller()
    {
        QDBusConnection target = QDBusConnection::connectToBus(m_address, QStringLiteral("target"));
        QDBusConnection other = QDBusConnection::connectToBus(m_address, QStringLiteral("other"));
        QDBusConnection server = QDBusConnection::connectToBus(m_address, QStringLiteral("server"));
        QVERIFY(target.isConnected() && other.isConnected() && server.isConnected());
        Target t;
        QVERIFY(target.registerObject(QStringLiteral("/t"), &t, QDBusConnection::ExportScriptableSlots));
        QVERIFY(target.registerService(QStringLiteral("net.mowerk.test.target")));

        // nobody is the notification server yet: refused
        QCOMPARE(callFrom(other), QVariant(false));
        QVERIFY(server.registerService(QString::fromLatin1(NotifyGate::kServer)));
        // another process: refused; the server: taken
        QCOMPARE(callFrom(other), QVariant(false));
        QCOMPARE(t.taken, 0);
        QCOMPARE(callFrom(server), QVariant(true));
        QCOMPARE(t.taken, 1);
        // the name moves to someone else: the old owner is refused
        QVERIFY(server.unregisterService(QString::fromLatin1(NotifyGate::kServer)));
        QVERIFY(other.registerService(QString::fromLatin1(NotifyGate::kServer)));
        QCOMPARE(callFrom(server), QVariant(false));
        QCOMPARE(callFrom(other), QVariant(true));
        // from inside the process (QML): not a D-Bus call, taken
        QVERIFY(t.direct());
        QCOMPARE(t.taken, 3);
    }

    void posted()
    {
        NotifyGate::Posted p(file("notified.json"));
        p.create();
        QVERIFY(QFile::exists(file("notified.json")));
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(file("notified.json")).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 0777), 0600);
        QVERIFY(!p.hasRoom(0, QStringLiteral("!a:x")));
        p.roomPosted(0, QStringLiteral("!a:x"), QStringLiteral("$e1"), { QStringLiteral("👍"), QStringLiteral("😀") });
        // another process reads the same file
        NotifyGate::Posted q(file("notified.json"));
        QVERIFY(q.hasRoom(0, QStringLiteral("!a:x")));
        QVERIFY(!q.hasRoom(1, QStringLiteral("!a:x")));
        QVERIFY(q.mayReact(0, QStringLiteral("!a:x"), QStringLiteral("$e1"), QStringLiteral("👍")));
        QVERIFY(!q.mayReact(0, QStringLiteral("!a:x"), QStringLiteral("$e1"), QStringLiteral("💩")));
        QVERIFY(!q.mayReact(0, QStringLiteral("!a:x"), QStringLiteral("$other"), QStringLiteral("👍")));
        QVERIFY(!q.mayReact(0, QStringLiteral("!a:x"), QString(), QStringLiteral("👍")));
        // updated in place: the newer event's buttons only
        p.roomPosted(0, QStringLiteral("!a:x"), QStringLiteral("$e2"), {});
        QVERIFY(!q.mayReact(0, QStringLiteral("!a:x"), QStringLiteral("$e1"), QStringLiteral("👍")));
        QVERIFY(q.hasRoom(0, QStringLiteral("!a:x")));
        p.roomClosed(0, QStringLiteral("!a:x"));
        QVERIFY(!q.hasRoom(0, QStringLiteral("!a:x")));

        // rings: answered and declined while they ring, by their event
        p.ringPosted(1, QStringLiteral("!c:x"), QStringLiteral("$call"), 60000);
        QVERIFY(q.rings(1, QStringLiteral("!c:x")));
        QVERIFY(!q.rings(0, QStringLiteral("!c:x")));
        QVERIFY(q.mayDecline(1, QStringLiteral("!c:x"), QStringLiteral("$call")));
        QVERIFY(!q.mayDecline(1, QStringLiteral("!c:x"), QStringLiteral("$old")));
        p.ringEnded(1, QStringLiteral("!c:x"));
        QVERIFY(!q.rings(1, QStringLiteral("!c:x")));
        // one whose time is over (its process went without closing it)
        p.ringPosted(0, QStringLiteral("!d:x"), QString(), -1);
        QVERIFY(!q.rings(0, QStringLiteral("!d:x")));
        QVERIFY(!q.mayDecline(0, QStringLiteral("!d:x"), QString()));

        // bounded: the oldest lines go
        for (int i = 0; i < NotifyGate::kMaxRooms + 10; ++i)
            p.roomPosted(0, QStringLiteral("!r%1:x").arg(i), QString(), {});
        QVERIFY(!q.hasRoom(0, QStringLiteral("!r0:x")));
        QVERIFY(q.hasRoom(0, QStringLiteral("!r%1:x").arg(NotifyGate::kMaxRooms + 9)));

        // no file, a broken file: nothing is up
        NotifyGate::Posted none(file("missing.json"));
        QVERIFY(!none.hasRoom(0, QStringLiteral("!a:x")));
        QFile broken(file("broken.json"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{not json");
        broken.close();
        QVERIFY(!NotifyGate::Posted(file("broken.json")).hasRoom(0, QStringLiteral("!a:x")));
    }

    void secret()
    {
        const QString path = file("bus-secret");
        const QString s = NotifyGate::secret(path);
        QCOMPARE(s.size(), 64);
        // made once: the next process reads the same
        QCOMPARE(NotifyGate::secret(path), s);
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(path).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 0777), 0600);
        QVERIFY(NotifyGate::sameSecret(s, NotifyGate::secret(path)));
        QVERIFY(!NotifyGate::sameSecret(s, QString()));
        QVERIFY(!NotifyGate::sameSecret(QString(), QString()));
        QString wrong = s;
        wrong[10] = wrong.at(10) == QLatin1Char('a') ? QLatin1Char('b') : QLatin1Char('a');
        QVERIFY(!NotifyGate::sameSecret(s, wrong));
        QVERIFY(!NotifyGate::sameSecret(s, s.left(32)));
        // a short (half-written) one is replaced
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("abc");
        f.close();
        const QString again = NotifyGate::secret(path);
        QCOMPARE(again.size(), 64);
        QVERIFY(again != s);
        // a folder that cannot be written: none
        QCOMPARE(NotifyGate::secret(QStringLiteral("/proc/rustrx-none/bus-secret")), QString());
    }
};

QTEST_GUILESS_MAIN(TestNotifyGate)
#include "tst_notifygate.moc"
