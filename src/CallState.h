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

#include "CallKeepAlive.h"

#include <QObject>
#include <QString>
#include <QTimer>

// The call as MCE knows it, the way the phone app tells it: "active" turns
// on MCE's in-call behaviour (the display kept on, the lock screen giving
// way to the call); "none" ends it. MCE blanks the screen at the ear only
// when the audio policy's route is the earpiece (tklock.c: audio_route ==
// AUDIO_ROUTE_HANDSET), and an app's call never gets that route: Rustrx
// sets the earpiece beside the policy. So Rustrx darkens the call page
// itself while the call is on the earpiece and the proximity sensor is
// covered, and takes every touch, so a cheek does not mute or hang up.
// Not with MCE's display off request: that is a screen off like the power
// key's, the lock screen and the device lock follow (the author's test). A ringing call stays "none" (the ringtone needs it). MCE ties the state to this process's
// bus connection and drops it when the process goes, a crash included.
//
// And the call's CPU keepalive (MCE's req_cpu_keepalive_start, renewed
// each period MCE names, as nemo-keepalive and PulseAudio's
// module-keepalive do): MCE lets the phone suspend 60 s into an active
// call, and only module-keepalive held it up during a call so far, which a
// port's PulseAudio may not load. MCE also ends it when the process goes,
// or 75 s after the last renewal.
class CallState : public QObject, public CallKeepAlive
{
    Q_OBJECT
public:
    explicit CallState(QObject *parent = nullptr);
    ~CallState() override;

    // tests: the screen is dark at the ear now
    bool blanked() const { return m_blanked; }
    bool cpuHeld() const { return m_cpuHeld; }
    // the lock screen is up (MCE's tklock mode); watchLock() follows it
    bool screenLocked() const { return m_locked; }
    void watchLock();

    void holdCpu(bool on) override;

signals:
    void blankedChanged();
    void screenLockedChanged();

public slots:
    // the call's state from Voip: idle, incoming, outgoing, connecting, connected
    void follow(const QString &voipState);
    void setSpeaker(bool on);
    // the proximity sensor (QML) is covered
    void setNear(bool near);
    // MCE's tklock mode ("locked", "silent-locked-dim", "unlocked", ...)
    void setLockMode(const QString &mode);

private:
    void request(const QString &state);
    void keepAlive(const char *method);
    void rethinkBlank();
    QString m_sent = QStringLiteral("none");
    bool m_speaker = false;
    bool m_near = false;
    bool m_blanked = false;
    bool m_cpuHeld = false;
    bool m_locked = false;
    QTimer m_renew;
};

#endif
