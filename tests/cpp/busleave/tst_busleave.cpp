// SPDX-License-Identifier: GPL-3.0-or-later
// src/BusLeave.h: a window-less Rustrx on its way out answers every call
// that reached it, on a private bus. The owner's event loop has stopped
// (the core's shutdown), callers keep calling, the name goes, the object
// is deleted: no caller may get UnknownObject or no answer, and what was
// asked before the name went is answered while it is still held (so the
// stand-in's handover file is written before the app takes the name).
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include "BusLeave.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <QtTest>

#include <atomic>

static const char kName[] = "net.mowerk.test.owner";

// stands in for Rustrx's openUrl
class Owner : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.test")
public:
    QDBusConnection bus = QDBusConnection(QString());
    int calls = 0;
    int whileOwned = 0;
public slots:
    Q_SCRIPTABLE void openUrl(const QStringList &)
    {
        ++calls;
        if (bus.interface()->isServiceRegistered(QString::fromLatin1(kName)).value()
                && bus.interface()->serviceOwner(QString::fromLatin1(kName)).value() == bus.baseService())
            ++whileOwned;
    }
};

// lipstick: calls one after another without waiting for the answers
class Caller : public QThread
{
public:
    QString address;
    QString connection;
    std::atomic<bool> stop{ false };
    QList<QDBusPendingCall> pending;
    int limit = 1000000;
protected:
    void run() override
    {
        QDBusConnection bus = QDBusConnection::connectToBus(address, connection);
        const QDBusMessage m = QDBusMessage::createMethodCall(QString::fromLatin1(kName), QStringLiteral("/o"),
                QStringLiteral("net.mowerk.test"), QStringLiteral("openUrl")) << QStringList();
        while (!stop && pending.size() < limit) {
            pending << bus.asyncCall(m, 10000);
            QThread::usleep(200);
        }
    }
};

class TestBusLeave : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QString m_address;

    struct Outcome { int answered = 0, gone = 0, lost = 0; QStringList errors; };
    static Outcome outcome(const QList<QDBusPendingCall> &pending)
    {
        Outcome o;
        for (QDBusPendingCall p : pending) {
            p.waitForFinished();
            const QDBusMessage r = p.reply();
            if (r.type() == QDBusMessage::ReplyMessage) {
                ++o.answered;
            } else if (r.errorName() == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
                       || r.errorName() == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")) {
                // after the name went: the next owner's, or the app's start
                ++o.gone;
            } else {
                ++o.lost;
                if (!o.errors.contains(r.errorName()))
                    o.errors << r.errorName();
            }
        }
        return o;
    }

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
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }

    // Planted-bug: bus.unregisterService(name) alone, as before (every call
    // queued during the shutdown answered UnknownObject when the object went)
    void queuedCallsAreAnsweredBeforeTheNameGoes()
    {
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, QStringLiteral("owner1"));
        Owner *owner = new Owner;
        owner->bus = bus;
        QVERIFY(bus.registerObject(QStringLiteral("/o"), owner, QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerService(QString::fromLatin1(kName)));
        Caller caller;
        caller.address = m_address;
        caller.connection = QStringLiteral("caller1");
        caller.limit = 20;
        caller.start();
        QVERIFY(caller.wait(5000));
        // the event loop has stopped: the calls wait in the queue
        QThread::msleep(300);
        BusLeave::releaseName(bus, QString::fromLatin1(kName), { owner });
        delete owner;
        const Outcome o = outcome(caller.pending);
        QCOMPARE(o.lost, 0);
        QCOMPARE(o.answered, 20);
        QDBusConnection::disconnectFromBus(QStringLiteral("owner1"));
        QDBusConnection::disconnectFromBus(QStringLiteral("caller1"));
    }

    // the same, and every one of them while the name was still held
    // Planted-bug: answerQueued only after the name went (whileOwned 0)
    void everyQueuedCallSeesTheNameStillHeld()
    {
        QDBusConnection bus = QDBusConnection::connectToBus(m_address, QStringLiteral("owner2"));
        Owner *owner = new Owner;
        owner->bus = bus;
        QVERIFY(bus.registerObject(QStringLiteral("/o"), owner, QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerService(QString::fromLatin1(kName)));
        Caller caller;
        caller.address = m_address;
        caller.connection = QStringLiteral("caller2");
        caller.limit = 10;
        caller.start();
        QVERIFY(caller.wait(5000));
        QThread::msleep(300);
        BusLeave::releaseName(bus, QString::fromLatin1(kName), { owner });
        QCOMPARE(owner->whileOwned, 10);
        delete owner;
        QCOMPARE(outcome(caller.pending).lost, 0);
        QDBusConnection::disconnectFromBus(QStringLiteral("owner2"));
        QDBusConnection::disconnectFromBus(QStringLiteral("caller2"));
    }

    // Planted-bug: answerQueued only before unregisterService (a call that
    // came during ReleaseName was dropped with the object)
    void aCallerThatNeverStopsLosesNothing()
    {
        for (int round = 0; round < 30; ++round) {
            const QString o = QStringLiteral("owner3-%1").arg(round), c = QStringLiteral("caller3-%1").arg(round);
            QDBusConnection bus = QDBusConnection::connectToBus(m_address, o);
            Owner *owner = new Owner;
            owner->bus = bus;
            QVERIFY(bus.registerObject(QStringLiteral("/o"), owner, QDBusConnection::ExportScriptableSlots));
            QVERIFY(bus.registerService(QString::fromLatin1(kName)));
            Caller caller;
            caller.address = m_address;
            caller.connection = c;
            caller.start();
            // the loop runs a while, then stops for the shutdown
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 20)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(20);
            BusLeave::releaseName(bus, QString::fromLatin1(kName), { owner });
            delete owner;
            QThread::msleep(10);
            caller.stop = true;
            QVERIFY(caller.wait(5000));
            const Outcome out = outcome(caller.pending);
            if (out.lost)
                qWarning() << "round" << round << out.errors;
            QCOMPARE(out.lost, 0);
            QVERIFY(out.answered > 0);
            QDBusConnection::disconnectFromBus(o);
            QDBusConnection::disconnectFromBus(c);
        }
    }
};

QTEST_GUILESS_MAIN(TestBusLeave)
#include "tst_busleave.moc"
