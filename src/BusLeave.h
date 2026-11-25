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
#ifndef BUSLEAVE_H
#define BUSLEAVE_H

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QEvent>
#include <QList>
#include <QObject>

// The end of a window-less process that holds the bus name. A call that
// reached it after its event loop stopped (the core's shutdown, up to
// seconds) waits in the queue, and Qt answers it with UnknownObject when
// its object goes: lipstick's open from the launcher was lost that way
// (the stand-in leaving with nobody logged in, the Prawn 2026-11-25). So
// the calls are answered here, once before the name goes, so that what they
// hand over is in the file before the next process takes the name, and
// once after: ReleaseName's reply comes after every call the bus sent here
// before it, so then all of them are queued. A call after that goes to the
// next owner, or starts the app.
namespace BusLeave {

inline void answerQueued(const QList<QObject *> &targets)
{
    for (QObject *o : targets)
        if (o)
            QCoreApplication::sendPostedEvents(o, QEvent::MetaCall);
}

inline void releaseName(QDBusConnection &bus, const QString &name, const QList<QObject *> &targets)
{
    answerQueued(targets);
    bus.unregisterService(name);
    answerQueued(targets);
}

} // namespace BusLeave

#endif
