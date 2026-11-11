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
#include <QEvent>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QThread>

#include "Rustrx.h"
#include "RoomModel.h"
#include "TimelineModel.h"
#include "VoiceRecorder.h"

static const QString kService = QStringLiteral("net.mowerk.harbour-rustrx");

// the earlier process leaves (the stand-in after a handover, the app after a
// close in background mode): its store is free once its bus name is gone
static bool waitForNameToGo(QDBusConnection &bus)
{
    QElapsedTimer t;
    t.start();
    while (bus.interface()->isServiceRegistered(kService)) {
        if (t.elapsed() > 10000)
            return false;
        QThread::msleep(100);
    }
    return true;
}

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("net.mowerk"));
    app->setApplicationName(QStringLiteral("harbour-rustrx"));

    qmlRegisterUncreatableType<RoomModel>("Rustrx", 1, 0, "RoomModel", "owned by rustrx");
    qmlRegisterUncreatableType<TimelineModel>("Rustrx", 1, 0, "TimelineModel", "owned by rustrx");

    // the stand-in: started by the user unit, with the session or when the
    // app is closed in background mode; it never has a window
    const bool standIn = qgetenv("RUSTRX_BACKGROUND") == "1";
    if (standIn && !QFile::exists(Rustrx::backgroundFlagPath()))
        return 0;

    // One process at a time, and the bus name is the lock (taken before
    // the store is opened): a second launch asks the running one to show its
    // window and leaves, unless that one is the stand-in, which leaves
    // instead; the stand-in itself waits for a closing app to be gone.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(kService)) {
        bool takeOver = standIn;
        if (!standIn) {
            QDBusMessage call = QDBusMessage::createMethodCall(kService, QStringLiteral("/net/mowerk/harbour_rustrx"),
                    QStringLiteral("net.mowerk.harbour_rustrx"), QStringLiteral("activate"));
            const QDBusMessage reply = bus.call(call, QDBus::Block, 3000);
            // no answer: the stand-in was leaving already
            takeOver = reply.type() == QDBusMessage::ErrorMessage
                    || (!reply.arguments().isEmpty() && reply.arguments().first().toBool());
        }
        if (!takeOver || !waitForNameToGo(bus) || !bus.registerService(kService))
            return 0;
    }

    // ~/.local/share/harbour-rustrx/harbour-rustrx: session, store, media
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Rustrx rustrx(dataDir);
    rustrx.setStandIn(standIn);
    if (!standIn)
        rustrx.takeHandover();
    // before the window, so it outlives the QML that uses it
    VoiceRecorder voice;

    // notifications and the wake job, for the life of the process
    QQmlEngine serviceEngine;
    serviceEngine.rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    QQmlComponent serviceComponent(&serviceEngine, QUrl::fromLocalFile(
            SailfishApp::pathTo(QStringLiteral("qml/Service.qml")).toLocalFile()));
    QScopedPointer<QObject> service(serviceComponent.create());
    if (!service)
        qWarning() << "Service.qml:" << serviceComponent.errors();

    if (standIn)
        return app->exec();

    // the window lives as long as the process (see Service.qml)
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    view->rootContext()->setContextProperty(QStringLiteral("voice"), &voice);
    view->rootContext()->setContextProperty(QStringLiteral("service"), service.data());
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    view->setSource(SailfishApp::pathToMainQml());
    QObject::connect(&rustrx, &Rustrx::activateRequested, view.data(), [&view]() {
        view->showFullScreen();
        view->raise();
        view->requestActivate();
    });
    QObject::connect(&rustrx, &Rustrx::closeRequested, view.data(), &QWindow::close);
    view->show();
    const int rc = app->exec();
    // closed in background mode: the stand-in takes over (it waits for this
    // process to be gone)
    if (rustrx.background())
        Rustrx::startUnit(QStringLiteral("harbour-rustrx.service"));
    return rc;
}
