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
#include "KeyboardSlide.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

static const char kService[] = "com.nokia.mce";

KeyboardSlide::KeyboardSlide(QObject *parent) : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QLatin1String(kService), QStringLiteral("/com/nokia/mce/signal"), QStringLiteral("com.nokia.mce.signal"),
                QStringLiteral("keyboard_slide_state_ind"), this, SLOT(onSlide(QString)));
    const QDBusMessage ask = QDBusMessage::createMethodCall(QLatin1String(kService), QStringLiteral("/com/nokia/mce/request"),
                                                            QStringLiteral("com.nokia.mce.request"),
                                                            QStringLiteral("keyboard_slide_state_req"));
    auto *watch = new QDBusPendingCallWatcher(bus.asyncCall(ask), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isValid())
            onSlide(reply.value());
        w->deleteLater();
    });
}

void KeyboardSlide::onSlide(const QString &state)
{
    const bool open = state == QLatin1String("open");
    if (open == m_open)
        return;
    m_open = open;
    emit openChanged();
}
