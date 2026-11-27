// SPDX-License-Identifier: GPL-3.0-or-later
// Who may use a notification's actions (NotifyGate.h, audit 0.8.4 IPC1):
// on a private bus, a call from another process of this user is taken for
// a notification that is up and refused for one that is not; the uid
// comparison on its own (another uid cannot be faked here); the list of
// posted notifications and the data folder's secret.
#include "NotifyGate.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdio>
#include <sys/stat.h>

// stands in for Rustrx: an exported slot behind the gate and, as Rustrx's
// answerIn, the check that the room rings
class Target : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.test")
public:
    NotifyGate::Posted posted;
    int taken = 0;
    // an Answer for a ring that is over: its room opened instead (IPC1c)
    int opened = 0;
    // as Rustrx::quit: the stand-in's line, up or not
    bool standIn = true;
    bool lineUp = false;
    int quits = 0;
    bool direct(const QString &roomId) { return answer(roomId); }
    bool directQuit() { return quit(); }
public slots:
    Q_SCRIPTABLE bool answer(const QString &roomId)
    {
        if (!NotifyGate::fromUser(*this, "test"))
            return false;
        if (NotifyGate::answerFor(calledFromDBus(), posted.rings(0, roomId)) == NotifyGate::Answer::OpenRoom) {
            ++opened;
            return false;
        }
        ++taken;
        return true;
    }
    Q_SCRIPTABLE bool quit()
    {
        if (!NotifyGate::mayQuit(*this, standIn, lineUp))
            return false;
        ++quits;
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
    QVariant callFrom(QDBusConnection from, const QString &roomId)
    {
        const bool quit = roomId.isEmpty();
        QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("net.mowerk.test.target"), QStringLiteral("/t"),
                                                        QStringLiteral("net.mowerk.test"), quit ? QStringLiteral("quit") : QStringLiteral("answer"));
        if (!quit)
            m << roomId;
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
        for (const char *n : { "target", "other" })
            QDBusConnection::disconnectFromBus(QLatin1String(n));
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }

    // a process of its own (this binary, NOTIFYGATE_CHILD), as lipstick's
    // mliteremoteaction: calls the target once for the room ("": Quit); its
    // answer, the call's result, is its one line
    QByteArray callFromChild(const QString &roomId)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NOTIFYGATE_CHILD"), roomId.isEmpty() ? QStringLiteral("quit") : roomId);
        env.insert(QStringLiteral("NOTIFYGATE_BUS"), m_address);
        p.setProcessEnvironment(env);
        p.start(QCoreApplication::applicationFilePath(), QStringList());
        // this event loop running: the target answers here
        QElapsedTimer t;
        t.start();
        while (!p.canReadLine() && t.elapsed() < 8000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            p.waitForReadyRead(20);
        }
        const QByteArray r = p.readLine().trimmed();
        p.waitForFinished(3000);
        return r;
    }

    void caller()
    {
        QDBusConnection target = QDBusConnection::connectToBus(m_address, QStringLiteral("target"));
        QDBusConnection other = QDBusConnection::connectToBus(m_address, QStringLiteral("other"));
        QVERIFY(target.isConnected() && other.isConnected());
        Target t;
        t.posted.setPath(file("caller.json"));
        t.posted.create();
        QVERIFY(target.registerObject(QStringLiteral("/t"), &t, QDBusConnection::ExportScriptableSlots));
        QVERIFY(target.registerService(QStringLiteral("net.mowerk.test.target")));
        const QString room = QStringLiteral("!c:x");

        // nothing rings: refused, from another process and from another
        // connection of this one
        // Planted-bug: the posted check dropped (any action of the user taken)
        QCOMPARE(callFromChild(room), QByteArray("false"));
        QCOMPARE(callFrom(other, room), QVariant(false));
        QCOMPARE(t.taken, 0);
        // an Answer with nothing ringing only opens the room (IPC1c)
        // Planted-bug: answerFor answers whatever is asked
        QCOMPARE(t.opened, 2);
        // the room rings: another process of this user is taken (lipstick's
        // mliteremoteaction), only for that room
        // Planted-bug: only the owner of org.freedesktop.Notifications taken
        t.posted.ringPosted(0, room, QStringLiteral("$call"), 60000);
        QCOMPARE(callFromChild(room), QByteArray("true"));
        QCOMPARE(t.taken, 1);
        QCOMPARE(callFromChild(QStringLiteral("!other:x")), QByteArray("false"));
        QCOMPARE(callFrom(other, room), QVariant(true));
        QCOMPARE(t.taken, 2);
        QCOMPARE(t.opened, 3);
        // the ring is over: not answered again, the room opened
        t.posted.ringEnded(0, room);
        QCOMPARE(callFromChild(room), QByteArray("false"));
        QCOMPARE(t.taken, 2);
        QCOMPARE(t.opened, 4);
        // from inside the process (QML): not a D-Bus call, taken
        QVERIFY(t.direct(room));
        QCOMPARE(t.taken, 3);
    }

    // Quit, the stand-in's line's button: only while that line is up
    // ("Show the background line" off: no line, and no app ends the stand-in)
    void quit()
    {
        QDBusConnection target = QDBusConnection::connectToBus(m_address, QStringLiteral("target"));
        QDBusConnection other = QDBusConnection::connectToBus(m_address, QStringLiteral("other"));
        QVERIFY(target.isConnected() && other.isConnected());
        Target t;
        QVERIFY(target.registerObject(QStringLiteral("/t"), &t, QDBusConnection::ExportScriptableSlots));
        QVERIFY(target.registerService(QStringLiteral("net.mowerk.test.target")));

        // the stand-in without its line: refused from another process and
        // from another connection
        // Planted-bug: the line's check dropped (Quit taken in the stand-in from any caller)
        QCOMPARE(callFromChild(QString()), QByteArray("false"));
        QCOMPARE(callFrom(other, QString()), QVariant(false));
        QCOMPARE(t.quits, 0);
        // the line up: taken (lipstick's mliteremoteaction)
        t.lineUp = true;
        QCOMPARE(callFromChild(QString()), QByteArray("true"));
        QCOMPARE(callFrom(other, QString()), QVariant(true));
        QCOMPARE(t.quits, 2);
        // swiped away or closed: refused again
        t.lineUp = false;
        QCOMPARE(callFromChild(QString()), QByteArray("false"));
        // not the stand-in, the flag set anyway: refused
        // Planted-bug: the stand-in's check dropped
        t.standIn = false;
        t.lineUp = true;
        QCOMPARE(callFrom(other, QString()), QVariant(false));
        QCOMPARE(t.quits, 2);
        // from inside the process (the app's Quit, the signals): taken
        t.lineUp = false;
        QVERIFY(t.directQuit());
        QCOMPARE(t.quits, 3);
    }

    // the uid the bus tells against this process's: another user, or no
    // answer from the bus, is refused
    // Planted-bug: the reply's validity ignored (an error's 0 is root's uid)
    // the stand-in's line: up from its post, and a moment after it closed
    // (lipstick closes it as Quit is tapped, before the tap's call comes)
    void standInLine()
    {
        NotifyGate::StandInLine l;
        QVERIFY(!l.up(1000));
        // closed without being up (the setting off): never up
        l.closed(1000);
        QVERIFY(!l.up(1000));
        l.posted();
        QVERIFY(l.up(2000));
        // Planted-bug: no grace after the close (the Quit button refused)
        l.closed(2000);
        QVERIFY(l.up(2000 + NotifyGate::kQuitGraceMs));
        // Planted-bug: the grace without end (Quit taken after a swipe forever)
        QVERIFY(!l.up(2001 + NotifyGate::kQuitGraceMs));
        // a second close does not renew it
        l.closed(2000 + NotifyGate::kQuitGraceMs);
        QVERIFY(!l.up(2001 + NotifyGate::kQuitGraceMs));
        // posted again: up
        l.posted();
        QVERIFY(l.up(100000));
    }

    void sameUser()
    {
        QVERIFY(NotifyGate::sameUser(true, 1000, 1000));
        QVERIFY(NotifyGate::sameUser(true, 0, 0));
        QVERIFY(!NotifyGate::sameUser(true, 1001, 1000));
        QVERIFY(!NotifyGate::sameUser(true, 0, 1000));
        QVERIFY(!NotifyGate::sameUser(false, 1000, 1000));
        QVERIFY(!NotifyGate::sameUser(false, 0, 0));
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
        // how long it still rings, for a process that takes the ring over
        // (Service.qml adoptRing). Planted-bug: the end time itself returned
        QVERIFY(q.ringLeft(1, QStringLiteral("!c:x")) > 55000 && q.ringLeft(1, QStringLiteral("!c:x")) <= 60000);
        QCOMPARE(q.ringLeft(0, QStringLiteral("!c:x")), qint64(0));
        p.ringEnded(1, QStringLiteral("!c:x"));
        QVERIFY(!q.rings(1, QStringLiteral("!c:x")));
        QCOMPARE(q.ringLeft(1, QStringLiteral("!c:x")), qint64(0));
        // one whose time is over (its process went without closing it)
        p.ringPosted(0, QStringLiteral("!d:x"), QString(), -1);
        QVERIFY(!q.rings(0, QStringLiteral("!d:x")));
        QVERIFY(!q.mayDecline(0, QStringLiteral("!d:x"), QString()));
        QCOMPARE(q.ringLeft(0, QStringLiteral("!d:x")), qint64(0));

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

// the child process of caller(): no test, no event loop needed (the
// call blocks, as mliteremoteaction's does; the target answers in the parent)
static int child(const QString &roomId)
{
    QDBusConnection bus = QDBusConnection::connectToBus(QString::fromLocal8Bit(qgetenv("NOTIFYGATE_BUS")), QStringLiteral("child"));
    if (!bus.isConnected())
        return 1;
    const bool quit = roomId == QLatin1String("quit");
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("net.mowerk.test.target"), QStringLiteral("/t"),
                                                    QStringLiteral("net.mowerk.test"), quit ? QStringLiteral("quit") : QStringLiteral("answer"));
    if (!quit)
        m << roomId;
    const QDBusMessage r = bus.call(m, QDBus::Block, 5000);
    const bool ok = r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty();
    std::printf("%s\n", !ok ? "error" : r.arguments().first().toBool() ? "true" : "false");
    std::fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray room = qgetenv("NOTIFYGATE_CHILD");
    if (!room.isEmpty())
        return child(QString::fromUtf8(room));
    TestNotifyGate t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_notifygate.moc"
