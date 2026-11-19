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
// on MCE's in-call behaviour (the display kept on, the lock screen giving
// way to the call); "none" ends it. MCE blanks the screen at the ear only
// when the audio policy's route is the earpiece (tklock.c: audio_route ==
// AUDIO_ROUTE_HANDSET), and an app's call never gets that route: Rustrx
// sets the earpiece beside the policy. So Rustrx blanks it itself, with
// MCE's display requests, while the call is on the earpiece and the
// proximity sensor is covered, so a cheek does not mute or hang up. A ringing call stays "none" (the ringtone needs it). MCE ties the state to this process's
// bus connection and drops it when the process goes, a crash included.
class CallState : public QObject
{
    Q_OBJECT
public:
    explicit CallState(QObject *parent = nullptr) : QObject(parent) {}
    ~CallState() override;

    // tests: the screen is dark at the ear now
    bool blanked() const { return m_blanked; }

public slots:
    // the call's state from Voip: idle, incoming, outgoing, connecting, connected
    void follow(const QString &voipState);
    void setSpeaker(bool on);
    // the proximity sensor (QML) is covered
    void setNear(bool near);

private:
    void request(const QString &state);
    void rethinkBlank();
    QString m_sent = QStringLiteral("none");
    bool m_speaker = false;
    bool m_near = false;
    bool m_blanked = false;
};

#endif
