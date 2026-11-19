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
#ifndef RUSTRX_CALLSTATE_H
#define RUSTRX_CALLSTATE_H

#include <QObject>
#include <QString>

// The call as MCE knows it, the way the phone app tells it: "active" turns
// on MCE's in-call behaviour, above all blanking the screen while the phone
// is at the ear (the proximity sensor), so a cheek does not mute or hang
// up; "none" ends it. A ringing call stays "none" (the ringtone needs it). MCE ties the state to this process's
// bus connection and drops it when the process goes, a crash included.
class CallState : public QObject
{
    Q_OBJECT
public:
    explicit CallState(QObject *parent = nullptr) : QObject(parent) {}
    ~CallState() override;

public slots:
    // the call's state from Voip: idle, incoming, outgoing, connecting, connected
    void follow(const QString &voipState);

private:
    void request(const QString &state);
    QString m_sent = QStringLiteral("none");
};

#endif
