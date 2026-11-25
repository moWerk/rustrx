// SPDX-License-Identifier: GPL-3.0-or-later
// src/QuitSignals.h: a SIGTERM, SIGINT or SIGHUP ends the process through
// its normal quit (the quit handler, then aboutToQuit to its end, exit 0),
// not at once. The test runs itself as a child process (RUSTRX_QUIT_CHILD)
// that stands in for Rustrx: a delayed quit as Rustrx::quit has it, and an
// aboutToQuit that takes a while, as the core's shutdown does.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include "QuitSignals.h"

#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <cstdio>
#include <signal.h>

static int child(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray mode = qgetenv("RUSTRX_QUIT_CHILD");
    QuitSignals::install();
    // a signal before the event loop runs waits for it
    if (mode == "early")
        ::raise(SIGTERM);
    QuitSignals::watch(&app, [](int sig) {
        std::printf("quit %d\n", sig);
        std::fflush(stdout);
        QTimer::singleShot(300, qApp, &QCoreApplication::quit);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        std::printf("aboutToQuit\n");
        std::fflush(stdout);
        QThread::msleep(200);
        std::printf("released\n");
        std::fflush(stdout);
    });
    std::printf("ready\n");
    std::fflush(stdout);
    // never ends by itself: only a signal ends it
    return app.exec();
}

class TestQuitSignals : public QObject
{
    Q_OBJECT

    // the child's output once it has ended; signals sent after "ready"
    QString run(const QByteArray &mode, const QList<int> &sent, int *code, QProcess::ExitStatus *status)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("RUSTRX_QUIT_CHILD"), QString::fromLatin1(mode));
        p.setProcessEnvironment(env);
        p.start(QCoreApplication::applicationFilePath(), QStringList());
        if (!p.waitForStarted(5000))
            return QStringLiteral("not started");
        QByteArray out;
        while (!out.contains("ready\n") && p.waitForReadyRead(5000))
            out += p.readAllStandardOutput();
        for (int sig : sent) {
            ::kill(pid_t(p.processId()), sig);
            QThread::msleep(50);
        }
        if (!p.waitForFinished(5000)) {
            p.kill();
            p.waitForFinished(1000);
            return QStringLiteral("hung");
        }
        out += p.readAllStandardOutput();
        *code = p.exitCode();
        *status = p.exitStatus();
        return QString::fromLatin1(out);
    }

private slots:
    // Planted-bug: no handler installed (the default action kills the
    // process at once, aboutToQuit never runs)
    void signalRunsTheNormalQuit_data()
    {
        QTest::addColumn<int>("sig");
        QTest::newRow("SIGTERM") << int(SIGTERM);
        QTest::newRow("SIGINT") << int(SIGINT);
        QTest::newRow("SIGHUP") << int(SIGHUP);
    }
    void signalRunsTheNormalQuit()
    {
        QFETCH(int, sig);
        int code = -1;
        QProcess::ExitStatus status = QProcess::CrashExit;
        const QString out = run("plain", { sig }, &code, &status);
        QCOMPARE(status, QProcess::NormalExit);
        QCOMPARE(code, 0);
        QCOMPARE(out, QStringLiteral("ready\nquit %1\naboutToQuit\nreleased\n").arg(sig));
    }

    // Planted-bug: every signal starts the quit again (two quit lines)
    void laterSignalsAreDropped()
    {
        int code = -1;
        QProcess::ExitStatus status = QProcess::CrashExit;
        const QString out = run("plain", { SIGTERM, SIGTERM, SIGINT }, &code, &status);
        QCOMPARE(status, QProcess::NormalExit);
        QCOMPARE(code, 0);
        QCOMPARE(out, QStringLiteral("ready\nquit %1\naboutToQuit\nreleased\n").arg(SIGTERM));
    }

    // Planted-bug: no handler installed (a signal before the event loop
    // kills at once)
    void signalBeforeTheLoopWaitsForIt()
    {
        int code = -1;
        QProcess::ExitStatus status = QProcess::CrashExit;
        const QString out = run("early", {}, &code, &status);
        QCOMPARE(status, QProcess::NormalExit);
        QCOMPARE(code, 0);
        QCOMPARE(out, QStringLiteral("ready\nquit %1\naboutToQuit\nreleased\n").arg(SIGTERM));
    }
};

int main(int argc, char **argv)
{
    // the child is no test: QtTest's own crash handlers stay out of it
    if (!qEnvironmentVariableIsEmpty("RUSTRX_QUIT_CHILD"))
        return child(argc, argv);
    QCoreApplication app(argc, argv);
    TestQuitSignals t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_quitsignals.moc"
