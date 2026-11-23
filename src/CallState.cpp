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
#include "CallState.h"
#include "Keys.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

namespace {
const char kKeepAliveSession[] = "rustrx-call";
}

CallState::CallState(QObject *parent) : QObject(parent)
{
    m_renew.setInterval(Keys::kCallKeepAliveRenewMs);
    connect(&m_renew, &QTimer::timeout, this, [this]() { keepAlive("req_cpu_keepalive_start"); });
}

CallState::~CallState()
{
    m_near = false;
    rethinkBlank();
    request(QStringLiteral("none"));
    holdCpu(false);
}

void CallState::holdCpu(bool on)
{
    if (on == m_cpuHeld)
        return;
    m_cpuHeld = on;
    qInfo() << "call: cpu keepalive" << (on ? "on" : "off");
    if (!on) {
        m_renew.stop();
        keepAlive("req_cpu_keepalive_stop");
        return;
    }
    // the period first: MCE's answer to it sets the session to end in
    // 2 s, and after a start that cut the start's 75 s short
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
                                                      QStringLiteral("com.nokia.mce.request"),
                                                      QStringLiteral("req_cpu_keepalive_period"));
    msg << QString::fromLatin1(kKeepAliveSession);
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<int> reply = *w;
        if (reply.isValid() && reply.value() > 0 && m_cpuHeld)
            m_renew.setInterval(reply.value() * 1000);
    });
    keepAlive("req_cpu_keepalive_start");
    m_renew.start();
}

void CallState::keepAlive(const char *method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
                                                      QStringLiteral("com.nokia.mce.request"), QString::fromLatin1(method));
    msg << QString::fromLatin1(kKeepAliveSession);
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    const QString name = QString::fromLatin1(method);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [name](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        // refused (the sandbox, no MCE): the call then relies on PulseAudio's
        if (w->isError())
            qWarning().noquote() << "call: cpu keepalive" << name << "failed:" << w->error().message();
    });
}

void CallState::setSpeaker(bool on)
{
    m_speaker = on;
    rethinkBlank();
}

void CallState::setNear(bool near)
{
    m_near = near;
    rethinkBlank();
}

void CallState::rethinkBlank()
{
    const bool blank = m_sent == QLatin1String("active") && m_near && !m_speaker;
    if (blank == m_blanked)
        return;
    m_blanked = blank;
    qInfo() << "call screen:" << (blank ? "dark at the ear" : "on");
    emit blankedChanged();
}

void CallState::follow(const QString &voipState)
{
    // not while it rings: ngfd plays the VoIP ringtone only with no call
    // state (voip_ringtone.ini: call_state.mode == none), and with
    // "ringing" set first a ring plays as the short in-call tones
    if (voipState == QLatin1String("idle") || voipState == QLatin1String("incoming"))
        request(QStringLiteral("none"));
    else
        request(QStringLiteral("active"));
    rethinkBlank();
}

void CallState::request(const QString &state)
{
    if (state == m_sent)
        return;
    m_sent = state;
    qInfo().noquote() << "call state for MCE:" << state;
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/request"),
                                                      QStringLiteral("com.nokia.mce.request"),
                                                      QStringLiteral("req_call_state_change"));
    msg << state << QStringLiteral("normal");
    QDBusConnection::systemBus().asyncCall(msg);
}
