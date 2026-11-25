/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QtQml>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <malloc.h>

#include "BusLeave.h"
#include "GstGuard.h"
#include "Handover.h"
#include "LocalOnly.h"
#include "NotifyGate.h"
#include "QuitSignals.h"
#include "Rustrx.h"
#include "UnifiedPush.h"
#include "Voip.h"
#include "DebugLog.h"
#include "FrameStats.h"
#include "Perf.h"
#include "AvatarProvider.h"
#include "KeyboardSlide.h"
#include "Twemoji.h"
#include "PrivateSettings.h"
#include "DconfOld.h"
#include <QSettings>
#include "RoomModel.h"
#include "TimelineModel.h"
#include "VoiceRecorder.h"
#include "Keys.h"
#include "rustrx_core.h"

static const QString kService = QString::fromLatin1(Keys::kBusName);

// the earlier process leaves (the stand-in after a handover, the app after a
// close in background mode): its store is free once its bus name is gone
static bool waitForNameToGo(QDBusConnection &bus)
{
    QElapsedTimer t;
    t.start();
    while (bus.interface()->isServiceRegistered(kService)) {
        if (t.elapsed() > Keys::kNameGoneTimeoutMs)
            return false;
        QThread::msleep(Keys::kNameGonePollMs);
    }
    return true;
}

int main(int argc, char *argv[])
{
    Perf::clock().start();
    // before any thread: few malloc arenas (Keys.h)
    mallopt(M_ARENA_MAX, Keys::kMallocArenas);
    // no playlist demuxers, for whatever starts GStreamer (audit UC4)
    GstGuard::setEnvironment();

    // the stand-in: started by the user unit, with the session or when the
    // app is closed in background mode. It never has a window and does not
    // even connect to the display: the home screen counts any connected
    // process as the running app, and a launcher tap then waits for its
    // window (a spinning tile).
    const bool standIn = qgetenv(Keys::kEnvBackground) == "1";
    // started by the push distributor through D-Bus activation of the
    // connector name, for one push: no window either, and gone soon after
    const bool pushWake = qgetenv(Keys::kEnvPushWake) == "1";
    if (standIn || pushWake)
        qputenv("QT_QPA_PLATFORM", "minimal");
    // Mesa's shader cache: in the sandbox ~/.cache is a fresh tmpfs, so it
    // was empty at every start and the first frame compiled every shader
    // again (520-610 ms on the Prawn's render thread, 222-289 ms with this).
    // The app's own cache folder (CacheLocation) is kept; before the
    // display is opened, and only where nobody chose one. hybris ignores it.
    else if (qEnvironmentVariableIsEmpty("MESA_SHADER_CACHE_DIR")) {
        const QByteArray xdg = qgetenv("XDG_CACHE_HOME");
        const QString cache = (xdg.isEmpty() ? QDir::homePath() + QLatin1String("/.cache") : QString::fromLocal8Bit(xdg))
                + QLatin1String("/net.mowerk/harbour-rustrx/mesa-shaders");
        if (QDir().mkpath(cache))
            qputenv("MESA_SHADER_CACHE_DIR", QFile::encodeName(cache));
    }

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("net.mowerk"));
    app->setApplicationName(QStringLiteral("harbour-rustrx"));
    // the debug log (Settings): the app and the stand-in each their own file
    DebugLog::install(QLatin1String(standIn ? Keys::kLogBackground : pushWake ? Keys::kLogPush : Keys::kLogApp));
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // the core reads the same variable for its own perf: lines
    if (qgetenv(Keys::kEnvPerf) == "1" || QFile::exists(dataDir + QLatin1Char('/') + QLatin1String(Keys::kPerfFlag))) {
        QLoggingCategory::setFilterRules(QStringLiteral("rustrx.perf.debug=true"));
        qputenv(Keys::kEnvPerf, "1");
    }
    Perf::mark("main", 0);
    Perf::mark("app");
    DebugLog::setEnabled(QSettings(Rustrx::settingsFile(), QSettings::IniFormat).value(QLatin1String(Keys::kDebugLog), Keys::kDebugLogDefault).toBool());
    // perf: twemoji when its names are in (a worker thread; after the log is set up)
    Twemoji::start(SailfishApp::pathTo(QStringLiteral("qml/twemoji")).toLocalFile());

    qmlRegisterUncreatableType<RoomModel>("Rustrx", 1, 0, "RoomModel", "owned by rustrx");
    qmlRegisterUncreatableType<TimelineModel>("Rustrx", 1, 0, "TimelineModel", "owned by rustrx");
    qmlRegisterUncreatableType<UnifiedPush>("Rustrx", 1, 0, "UnifiedPush", "owned by rustrx");
    qmlRegisterUncreatableType<Voip>("Rustrx", 1, 0, "Voip", "owned by rustrx");
    // the settings other apps must not reach (components/Settings.qml)
    qmlRegisterType<PrivateSettings>("Rustrx", 1, 0, "PrivateSettings");
    // the tuned numbers (import Rustrx 1.0): one registration serves the
    // window's engine and Service.qml's
    qmlRegisterSingletonType(QUrl::fromLocalFile(SailfishApp::pathTo(QStringLiteral("qml/singletons/Tunables.qml")).toLocalFile()),
                             "Rustrx", 1, 0, "Tunables");

    // the stand-in is the fallback: not wanted while push works
    if (standIn && (!QFile::exists(Rustrx::backgroundFlagPath()) || UnifiedPush::activeOnDisk(dataDir)))
        return 0;
    // nor logged out: it took the name for a moment, and a launcher tap in
    // that moment reached a process on its way out (the Prawn, 2026-11-25)
    if (standIn && !Handover::sessionOnDisk(dataDir)) {
        qInfo() << "stand-in: no account logged in, not started";
        return 0;
    }
    if (pushWake) {
        // push switched off, or someone (the app, the stand-in) has the
        // connector name already and takes the push itself
        if (!UnifiedPush::enabledOnDisk(dataDir))
            return 0;
        if (QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(UnifiedPush::kConnectorName)))
            return 0;
    }

    // One process at a time, and the bus name is the lock (taken before
    // the store is opened): a second launch asks the running one to show its
    // window and leaves, unless that one is the stand-in, which leaves
    // instead; the stand-in itself waits for a closing app to be gone.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(kService)) {
        // a woken process does not wait for anyone: the running one has the push
        if (pushWake)
            return 0;
        bool takeOver = standIn;
        if (!standIn) {
            // the stand-in leaves only for the data folder's secret (IPC1 d)
            QDBusMessage call = QDBusMessage::createMethodCall(kService, QString::fromLatin1(Keys::kObjectPath),
                    QString::fromLatin1(Keys::kInterface), QStringLiteral("activateWith"));
            call << NotifyGate::secret(dataDir + QLatin1Char('/') + QLatin1String(NotifyGate::kSecretFile));
            QDBusMessage plain = QDBusMessage::createMethodCall(kService, QString::fromLatin1(Keys::kObjectPath),
                    QString::fromLatin1(Keys::kInterface), QStringLiteral("activate"));
            // gone in the meantime: the name is free for this one (a call
            // that may start a process would start a third Rustrx)
            call.setAutoStartService(false);
            plain.setAutoStartService(false);
            // one that is still starting (it waits for sailfish-secrets,
            // say) has the name but not its object yet: asked again
            QElapsedTimer asked;
            asked.start();
            QDBusMessage reply;
            for (;;) {
                reply = bus.call(call, QDBus::Block, Keys::kActivateTimeoutMs);
                // a Rustrx from before 0.8.4 (a stand-in still running after
                // the update) knows only activate
                if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
                    const QDBusMessage old = bus.call(plain, QDBus::Block, Keys::kActivateTimeoutMs);
                    if (old.type() == QDBusMessage::ReplyMessage)
                        reply = old;
                }
                const QString error = reply.errorName();
                const bool starting = error == QLatin1String("org.freedesktop.DBus.Error.UnknownObject")
                        || error == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")
                        || error == QLatin1String("org.freedesktop.DBus.Error.UnknownInterface");
                if (!starting || asked.elapsed() > Keys::kActivateRetryMs)
                    break;
                QThread::msleep(Keys::kNameGonePollMs);
            }
            // no answer: the stand-in was leaving already
            takeOver = reply.type() == QDBusMessage::ErrorMessage
                    || (!reply.arguments().isEmpty() && reply.arguments().first().toBool());
        }
        if (!takeOver)
            return 0;
        if (!waitForNameToGo(bus) || !bus.registerService(kService)) {
            qWarning() << "startup: the running Rustrx did not make way within"
                       << Keys::kNameGoneTimeoutMs << "ms; not started";
            // a stand-in that finds the app in front is not needed, which is
            // no failure: its unit restarts on failure, and did so every
            // minute for as long as the app ran (dodoradio's log, 98 times
            // in two hours). The app starts the stand-in when it closes.
            return standIn ? 0 : 1;
        }
    }

    Perf::mark("bus");
    // from here a SIGTERM (the unit stopped, the session's end), SIGINT or
    // SIGHUP waits for the event loop and its normal quit below
    QuitSignals::install();
    // the private settings, for both engines: by the one process that holds
    // the name (whichever runs first after the update moves them out of dconf)
    PrivateStore privateSettings(Rustrx::settingsFile());
    {
        DconfOld dconf;
        privateSettings.migrate(dconf);
    }
    // ~/.local/share/harbour-rustrx/harbour-rustrx: session, store, media
    Rustrx rustrx(dataDir);
    Perf::mark("rustrx");
    // at the end the core stops first, then the name (the store's lock)
    // goes, so the next Rustrx never opens a store this one still writes
    const bool windowless = standIn || pushWake;
    QObject::connect(app.data(), &QCoreApplication::aboutToQuit, [&bus, &rustrx, windowless]() {
        // a call is hung up first, while the core can still send
        rustrx.voip()->endForQuit(Keys::kVoipQuitHangupMs);
        rustrx_shutdown(Keys::kShutdownMs);
        if (!windowless) {
            bus.unregisterService(kService);
            return;
        }
        // what was asked meanwhile is handed over to the app, not lost
        rustrx.setLeaving();
        BusLeave::releaseName(bus, kService, rustrx.busObjects());
    });
    // as the Quit button does: the stand-in's line closed, then the order above
    QuitSignals::watch(app.data(), [&rustrx](int sig) {
        qInfo() << "quit: signal" << sig << "received, quitting";
        rustrx.quit();
    });
    rustrx.setStandIn(standIn);
    rustrx.setPushWake(pushWake);
    // the connector (pushes reach whichever Rustrx process runs)
    rustrx.firstPush()->exportConnector();
    if (!standIn)
        rustrx.takeHandover();
    Perf::mark("connector");
    // before the window, so it outlives the QML that uses it
    VoiceRecorder voice;

    // notifications and the wake job, for the life of the process
    QQmlEngine serviceEngine;
    LocalOnly localOnly;
    serviceEngine.setNetworkAccessManagerFactory(&localOnly);
    serviceEngine.rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    QQmlComponent serviceComponent(&serviceEngine, QUrl::fromLocalFile(
            SailfishApp::pathTo(QStringLiteral("qml/Service.qml")).toLocalFile()));
    QScopedPointer<QObject> service(serviceComponent.create());
    if (!service)
        qWarning() << "Service.qml:" << serviceComponent.errors();
    Perf::mark("service");
    // the notifications an earlier process left: a blocking call to the home
    // screen, which is busy with the launch. The window-less processes take
    // them now, the app after its first frame (or at its first notification)
    if (service && (standIn || pushWake))
        QMetaObject::invokeMethod(service.data(), "takeOverOld");

    if (pushWake) {
        // gone 30 s after the last push, after 2 minutes at the most
        QTimer idle;
        idle.setSingleShot(true);
        idle.setInterval(Keys::kPushIdleMs);
        QObject::connect(&idle, &QTimer::timeout, app.data(), &QCoreApplication::quit);
        QObject::connect(&rustrx, &Rustrx::pushed, &idle, [&idle]() { idle.start(); });
        // woken by a notification's button instead: as long again for it
        QObject::connect(&rustrx, &Rustrx::markReadRequested, &idle, [&idle]() { idle.start(); });
        QObject::connect(&rustrx, &Rustrx::replyRequested, &idle, [&idle]() { idle.start(); });
        QObject::connect(&rustrx, &Rustrx::reactRequested, &idle, [&idle]() { idle.start(); });
        // a reply's outcome, so its "not sent" notification is still posted
        QObject::connect(&rustrx, &Rustrx::replySent, &idle, [&idle]() { idle.start(); });
        QObject::connect(&rustrx, &Rustrx::replyFailed, &idle, [&idle]() { idle.start(); });
        idle.start();
        QTimer::singleShot(Keys::kPushMaxLifeMs, app.data(), &QCoreApplication::quit);
        return app->exec();
    }
    if (standIn)
        return app->exec();

    // made before the window, so it outlives the window that reads it
    KeyboardSlide keyboardSlide;
    // the window lives as long as the process (see Service.qml)
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    Perf::mark("view");
    // the first frame, the main page's with a saved session, else StartPage's
    // (render thread, so direct and once)
    static QAtomicInt framed;
    QObject *serviceObject = service.data();
    QObject::connect(view.data(), &QQuickWindow::frameSwapped, view.data(), [serviceObject]() {
        if (!framed.load() && framed.testAndSetRelaxed(0, 1)) {
            Perf::mark("frame");
            if (serviceObject)
                QMetaObject::invokeMethod(serviceObject, "takeOverOld", Qt::QueuedConnection);
        }
    }, Qt::DirectConnection);
    view->engine()->setNetworkAccessManagerFactory(&localOnly);
    view->rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    view->rootContext()->setContextProperty(QStringLiteral("voice"), &voice);
    view->rootContext()->setContextProperty(QStringLiteral("keyboardSlide"), &keyboardSlide);
    view->rootContext()->setContextProperty(QStringLiteral("service"), service.data());
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    view->engine()->addImageProvider(QStringLiteral("rxavatar"), new AvatarProvider);
    view->setSource(SailfishApp::pathToMainQml());
    Perf::mark("qml");
    // frame timing in the log, for comparing builds: RUSTRX_FRAMES=1
    if (qgetenv("RUSTRX_FRAMES") == "1")
        new FrameStats(view.data());
    QObject::connect(&rustrx, &Rustrx::activateRequested, view.data(), [&view]() {
        view->showFullScreen();
        view->raise();
        view->requestActivate();
    });
    QObject::connect(&rustrx, &Rustrx::closeRequested, view.data(), &QWindow::close);
    view->show();
    Perf::mark("show");
    const int rc = app->exec();
    // closed in background mode: the stand-in takes over (it waits for this
    // process to be gone); not when a signal ended it (the session's end)
    if (!QuitSignals::received() && rustrx.background() && !UnifiedPush::activeOnDisk(dataDir) && Handover::sessionOnDisk(dataDir))
        Rustrx::startUnit(QString::fromLatin1(Keys::kStandInUnit));
    return rc;
}
