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

#include "Rustrx.h"
#include "RoomModel.h"
#include "TimelineModel.h"
#include "VoiceRecorder.h"

// In background mode closing the app (from its cover or by the top swipe)
// hides the window instead of ending the process: the home screen drops the
// tile at once, notifications go on, and the launcher brings the window back.
class CloseToHidden : public QObject
{
public:
    explicit CloseToHidden(Rustrx *rustrx, QWindow *window) : m_rustrx(rustrx), m_window(window) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Close && m_rustrx->background()) {
            event->ignore();
            m_window->hide();
            emit m_rustrx->windowHidden();
            return true;
        }
        return QObject::eventFilter(obj, event);
    }
private:
    Rustrx *m_rustrx;
    QWindow *m_window;
};

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("net.mowerk"));
    app->setApplicationName(QStringLiteral("harbour-rustrx"));

    qmlRegisterUncreatableType<RoomModel>("Rustrx", 1, 0, "RoomModel", "owned by rustrx");
    qmlRegisterUncreatableType<TimelineModel>("Rustrx", 1, 0, "TimelineModel", "owned by rustrx");

    // One process: a second launch (the launcher, after the window was closed
    // in background mode) hands over to the running one and leaves.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface() && bus.interface()->isServiceRegistered(QStringLiteral("net.mowerk.harbour-rustrx"))) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("net.mowerk.harbour-rustrx"),
                QStringLiteral("/net/mowerk/harbour_rustrx"), QStringLiteral("net.mowerk.harbour_rustrx"),
                QStringLiteral("activate"));
        bus.call(call, QDBus::Block, 3000);
        return 0;
    }
    // started by the session: only when background mode is on, and without a window
    const bool hidden = qgetenv("RUSTRX_BACKGROUND") == "1";
    if (hidden && !QFile::exists(Rustrx::backgroundFlagPath()))
        return 0;

    // ~/.local/share/harbour-rustrx/harbour-rustrx: session, store, media
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Rustrx rustrx(dataDir);
    rustrx.setStartedHidden(hidden);
    QGuiApplication::setQuitOnLastWindowClosed(!rustrx.background());
    // before the view, so it outlives the QML that uses it
    VoiceRecorder voice;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    view->rootContext()->setContextProperty(QStringLiteral("voice"), &voice);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    view->setSource(SailfishApp::pathToMainQml());
    QObject::connect(&rustrx, &Rustrx::activateRequested, view.data(), [&view]() {
        view->showFullScreen();
        view->raise();
        view->requestActivate();
    });
    // every way back (the launcher raises the window itself, not through us)
    QObject::connect(view.data(), &QWindow::visibleChanged, &rustrx, [&rustrx](bool visible) {
        if (visible)
            emit rustrx.windowShown();
    });
    CloseToHidden closer(&rustrx, view.data());
    view->installEventFilter(&closer);
    QObject::connect(&rustrx, &Rustrx::hideRequested, view.data(), [&view, &rustrx]() {
        view->hide();
        emit rustrx.windowHidden();
    });
    // a session start opens minimised to the cover (QML), so the launcher can raise it
    view->show();
    return app->exec();
}
