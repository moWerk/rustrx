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
#ifndef HANDOVER_H
#define HANDOVER_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// What a window-less process (the stand-in, or one woken for a push) does
// with a request to show something: it keeps the request, writes it to a
// file for a new app process and leaves. The app reads that file at start
// and QML takes each request once. Also the background mode's flag file and
// the user units. No signals: Rustrx's D-Bus slots emit their own.
class Handover
{
public:
    bool standIn() const { return m_standIn; }
    void setStandIn(bool on) { m_standIn = on; }
    bool pushWake() const { return m_pushWake; }
    void setPushWake(bool on) { m_pushWake = on; }

    void setPendingRoom(const QString &roomId) { m_pendingRoom = roomId; }
    void setPendingCall(const QString &roomId) { m_pendingCall = roomId; }
    void setPendingKnocks(bool on) { m_pendingKnocks = on; }
    void setPendingUri(const QString &uri) { m_pendingUri = uri; }
    void setPendingShare(const QStringList &files, const QString &text);
    // a classic call ringing in the stand-in (Voip::ringing()), for the app
    void setPendingVoip(const QVariantMap &invite) { m_pendingVoip = invite; }
    QVariantMap takePendingVoip();
    QString takePendingRoom();
    QString takePendingCall();
    bool takePendingKnocks();
    QString takePendingUri();
    QVariantMap takePendingShare();

    // the stand-in: pass what was asked for to a new app process and leave;
    // false in the app, which shows it itself
    bool handOver();
    // what the stand-in passed on: the room, link or share to show
    void takeHandover();

    // the file the stand-in's unit looks for (in the sandbox's own config folder)
    static QString backgroundFlagPath();
    bool background() const;
    // writes or removes the flag; the caller checks for a change first
    void setBackground(bool on);
    // a user unit: the stand-in (harbour-rustrx.service) or the app for it to
    // hand over to (harbour-rustrx-open.service)
    static bool startUnit(const QString &unit);

private:
    bool m_standIn = false;
    bool m_pushWake = false;
    QString m_pendingRoom;
    QString m_pendingCall;
    bool m_pendingKnocks = false;
    QString m_pendingUri;
    QVariantMap m_pendingShare;
    QVariantMap m_pendingVoip;
};

#endif
