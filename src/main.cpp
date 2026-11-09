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

#include "Rustrx.h"
#include "RoomModel.h"
#include "TimelineModel.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(QStringLiteral("net.mowerk"));
    app->setApplicationName(QStringLiteral("harbour-rustrx"));

    qmlRegisterUncreatableType<RoomModel>("Rustrx", 1, 0, "RoomModel", "owned by rustrx");
    qmlRegisterUncreatableType<TimelineModel>("Rustrx", 1, 0, "TimelineModel", "owned by rustrx");

    // ~/.local/share/harbour-rustrx/harbour-rustrx: session, store, media
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    Rustrx rustrx(dataDir);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("rustrx"), &rustrx);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
