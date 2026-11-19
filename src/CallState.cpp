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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>

CallState::~CallState()
{
    request(QStringLiteral("none"));
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
