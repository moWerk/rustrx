// SPDX-License-Identifier: GPL-3.0-or-later
// The push connector's choices (UnifiedPush.cpp, audit 0.8.4 IPC2 and
// IPC3), on a private bus as the session bus: the distributor by its exact
// name, kept once chosen (across a logout too), another one only offered;
// and the connector's name held by an app that cannot prove it is a
// Rustrx: push paused, on disk for the stand-in, until the name is free.
#include "UnifiedPush.h"
#include "Keys.h"
#include "NameProof.h"

#include <QDBusConnection>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdio>

// holds a name and answers every proof wrongly
class WrongProof : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.harbour_rustrx.Proof")
public slots:
    Q_SCRIPTABLE QString proveName(const QString &, const QString &) { return QString(64, QLatin1Char('a')); }
};

class TestUnifiedPush : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QTemporaryDir m_dir;

    QVariantMap state(const QString &dir) const
    {
        QFile f(dir + QLatin1Char('/') + QLatin1String(Keys::kPushStateFile));
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().toVariantMap() : QVariantMap();
    }
    QString dir(const char *name) const
    {
        const QString d = m_dir.path() + QLatin1Char('/') + QLatin1String(name);
        QDir().mkpath(d);
        return d;
    }

private slots:
    void initTestCase()
    {
        m_daemon.start(QStringLiteral("dbus-daemon"), { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address") });
        if (!m_daemon.waitForStarted(3000))
            QSKIP("no dbus-daemon on this host");
        QTRY_VERIFY_WITH_TIMEOUT(m_daemon.canReadLine(), 5000);
        const QByteArray address = m_daemon.readLine().trimmed();
        QVERIFY(!address.isEmpty());
        // before the first sessionBus(): UnifiedPush's bus is this one
        qputenv("DBUS_SESSION_BUS_ADDRESS", address);
        QVERIFY(QDBusConnection::sessionBus().isConnected());
    }
    void cleanupTestCase()
    {
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }

    // Planted-bug: the suffix match back (endsWith "Foghorn"), or the pin
    // ignored, or a new distributor taken without asking
    void choice()
    {
        const QString foghorn = QStringLiteral("org.unifiedpush.Distributor.Foghorn");
        const QString fake = QStringLiteral("org.unifiedpush.Distributor.XFoghorn");
        const QString other = QStringLiteral("org.unifiedpush.Distributor.Other");
        QString offer;
        QCOMPARE(UnifiedPush::chooseDistributor({ fake, foghorn }, QString(), &offer), foghorn);
        QCOMPARE(UnifiedPush::chooseDistributor({ foghorn, fake }, QString(), &offer), foghorn);
        QCOMPARE(offer, QString());
        // no Foghorn: the first by name, never one by its suffix
        QCOMPARE(UnifiedPush::chooseDistributor({ other, fake }, QString()), other);
        QCOMPARE(UnifiedPush::chooseDistributor({ QStringLiteral("org.unifiedpush.Distributor."), QStringLiteral("org.example.Foghorn") }, QString()),
                 QString());
        // the one chosen before stays, Foghorn there or not
        QCOMPARE(UnifiedPush::chooseDistributor({ foghorn, other }, other, &offer), other);
        QCOMPARE(offer, QString());
        // gone: none, and the new one offered
        QCOMPARE(UnifiedPush::chooseDistributor({ fake, foghorn }, other, &offer), QString());
        QCOMPARE(offer, foghorn);
        QCOMPARE(UnifiedPush::chooseDistributor({}, other, &offer), QString());
        QCOMPARE(offer, QString());
    }

    // Planted-bug: loggedOut() forgets the pin (a new distributor after a
    // logout would be taken unasked)
    void pinnedOnceAndKept()
    {
        QDBusConnection distributor = QDBusConnection::connectToBus(QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS")),
                                                                    QStringLiteral("distributor"));
        const QString other = QStringLiteral("org.unifiedpush.Distributor.Other");
        QVERIFY(distributor.registerService(other));
        const QString d = dir("pin");
        {
            UnifiedPush up(d);
            QTRY_VERIFY_WITH_TIMEOUT(up.lookedUp(), 5000);
            QCOMPARE(up.distributor(), other);
            QCOMPARE(up.pinned(), other);
            QCOMPARE(state(d).value("distributor").toString(), other);
            up.loggedOut();
            QCOMPARE(state(d).value("distributor").toString(), other);
        }
        // Other gone, Foghorn there: not taken, offered
        QVERIFY(distributor.unregisterService(other));
        QVERIFY(distributor.registerService(QStringLiteral("org.unifiedpush.Distributor.Foghorn")));
        {
            UnifiedPush up(d);
            QTRY_VERIFY_WITH_TIMEOUT(up.lookedUp(), 5000);
            QCOMPARE(up.distributor(), QString());
            QVERIFY(!up.available());
            QCOMPARE(up.offered(), QStringLiteral("org.unifiedpush.Distributor.Foghorn"));
            // only the name offered is taken
            up.useDistributor(QStringLiteral("org.unifiedpush.Distributor.Fake"));
            QCOMPARE(up.distributor(), QString());
            up.useDistributor(QStringLiteral("org.unifiedpush.Distributor.Foghorn"));
            QCOMPARE(up.distributor(), QStringLiteral("org.unifiedpush.Distributor.Foghorn"));
            QCOMPARE(up.offered(), QString());
            QCOMPARE(state(d).value("distributor").toString(), QStringLiteral("org.unifiedpush.Distributor.Foghorn"));
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("distributor"));
    }

    // Planted-bug: exportConnector only logs a taken name (the squatter
    // gets the pushes and Rustrx still says active, IPC2)
    void squattedConnectorPausesPush()
    {
        QDBusConnection squatter = QDBusConnection::connectToBus(QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS")),
                                                                 QStringLiteral("squatter"));
        WrongProof wrong;
        QVERIFY(squatter.registerObject(QString::fromLatin1(Keys::kProofPath), &wrong, QDBusConnection::ExportScriptableSlots));
        QVERIFY(squatter.registerService(QString::fromLatin1(UnifiedPush::kConnectorName)));
        const QString d = dir("squat");
        // as if push had been working
        QFile f(d + QLatin1Char('/') + QLatin1String(Keys::kPushStateFile));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"enabled\":true,\"pusherSet\":true,\"endpoint\":\"https://push.example.org/x\",\"token\":\"t\"}");
        f.close();
        QVERIFY(UnifiedPush::activeOnDisk(d));
        {
            UnifiedPush up(d);
            QVERIFY(!up.exportConnector());
            QTRY_VERIFY_WITH_TIMEOUT(up.blocked(), 5000);
            QVERIFY(!up.active());
            QVERIFY(UnifiedPush::blockedOnDisk(d));
            QVERIFY(!UnifiedPush::activeOnDisk(d));
            // the block outlives a logout, as the stand-in needs it
            up.loggedOut();
            QVERIFY(UnifiedPush::blockedOnDisk(d));
        }
        // the name free again: taken, the block gone
        QVERIFY(squatter.unregisterService(QString::fromLatin1(UnifiedPush::kConnectorName)));
        {
            UnifiedPush up(d);
            QVERIFY(up.exportConnector());
            QVERIFY(!up.blocked());
            QVERIFY(!UnifiedPush::blockedOnDisk(d));
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("squatter"));
    }

    // Push off: the connector's name is not looked into, and a block left
    // from an earlier look goes. Planted-bug: takeConnector checks with push off.
    void pushOffChecksNothing()
    {
        QDBusConnection squatter = QDBusConnection::connectToBus(QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS")),
                                                                 QStringLiteral("squatter2"));
        // the test before left the name with this process
        QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(UnifiedPush::kConnectorName));
        WrongProof wrong;
        QVERIFY(squatter.registerObject(QString::fromLatin1(Keys::kProofPath), &wrong, QDBusConnection::ExportScriptableSlots));
        QVERIFY(squatter.registerService(QString::fromLatin1(UnifiedPush::kConnectorName)));
        const QString d = dir("off");
        QFile f(d + QLatin1Char('/') + QLatin1String(Keys::kPushStateFile));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"blocked\":true,\"enabled\":false}");
        f.close();
        {
            UnifiedPush up(d);
            QVERIFY(!up.exportConnector());
            QTest::qWait(500);
            QVERIFY(!up.blocked());
            QVERIFY(!UnifiedPush::blockedOnDisk(d));
            // switched on: now it is looked into
            up.setEnabled(true);
            QTRY_VERIFY_WITH_TIMEOUT(up.blocked(), 5000);
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("squatter2"));
    }

    // A free name the bus does not let Rustrx own (the C2 without a
    // distributor: no UnifiedPush permission, ServiceUnknown) is no squat,
    // and an old block from that goes. On a bus of its own that refuses
    // the name, in a child process (UnifiedPush's bus is the session bus).
    // Planted-bug: a free name that cannot be had counts as squatted.
    void aNameThatCannotBeOwnedIsNoSquat()
    {
        QTemporaryDir conf;
        QFile c(conf.path() + QStringLiteral("/bus.conf"));
        QVERIFY(c.open(QIODevice::WriteOnly));
        c.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
                "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n<busconfig><type>session</type>"
                "<listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/><allow own=\"*\"/>"
                "<deny own=\"org.unifiedpush.Connector.rustrx\"/></policy></busconfig>\n");
        c.close();
        QProcess daemon;
        daemon.start(QStringLiteral("dbus-daemon"), { QStringLiteral("--config-file=") + c.fileName(), QStringLiteral("--nofork"),
                                                     QStringLiteral("--print-address") });
        QVERIFY(daemon.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(daemon.canReadLine(), 5000);
        const QString address = QString::fromLatin1(daemon.readLine().trimmed());
        const QString d = dir("unowned");
        QFile f(d + QLatin1Char('/') + QLatin1String(Keys::kPushStateFile));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"blocked\":true,\"enabled\":true}");
        f.close();
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), address);
        env.insert(QStringLiteral("UNIFIEDPUSH_CHILD"), d);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("blocked 0"));
        QVERIFY(!UnifiedPush::blockedOnDisk(d));
        daemon.terminate();
        daemon.waitForFinished(3000);
    }
};

// aNameThatCannotBeOwnedIsNoSquat's child: the connector exported on a
// bus that refuses its name, its verdict once the look is over
static int child(const QString &dir)
{
    UnifiedPush up(dir);
    up.exportConnector();
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    std::printf("blocked %d\n", up.blocked() ? 1 : 0);
    std::fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray dir = qgetenv("UNIFIEDPUSH_CHILD");
    if (!dir.isEmpty())
        return child(QString::fromLocal8Bit(dir));
    TestUnifiedPush t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_unifiedpush.moc"
