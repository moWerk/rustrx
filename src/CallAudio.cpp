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
#include "CallAudio.h"

#include <QCoreApplication>
#include <QDebug>
#include <policy/audio-resource.h>
#include <policy/resource-set.h>

const char CallAudio::kStreamTag[] = "rustrx-call";

CallAudio::CallAudio(QObject *parent) : QObject(parent)
{
}

void CallAudio::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    if (!m_set) {
        m_set = new ResourcePolicy::ResourceSet(QStringLiteral("ipcall"), this);
        m_set->setAlwaysReply();
        auto *audio = new ResourcePolicy::AudioResource(QStringLiteral("ipcall"));
        audio->setProcessID(quint32(QCoreApplication::applicationPid()));
        audio->setStreamTag(QStringLiteral("media.name"), QLatin1String(kStreamTag));
        m_set->addResourceObject(audio);
        m_set->addResource(ResourcePolicy::AudioRecorderType);
        connect(m_set, &ResourcePolicy::ResourceSet::resourcesGranted, this, [](const QList<ResourcePolicy::ResourceType> &) {
            qInfo() << "voip: call audio granted";
        });
        connect(m_set, &ResourcePolicy::ResourceSet::resourcesDenied, this, []() { qInfo() << "voip: call audio denied"; });
        connect(m_set, &ResourcePolicy::ResourceSet::lostResources, this, []() { qInfo() << "voip: call audio lost"; });
        connect(m_set, &ResourcePolicy::ResourceSet::managerIsUp, this, []() { qInfo() << "voip: audio policy manager up"; });
        connect(m_set, &ResourcePolicy::ResourceSet::errorCallback, this, [](quint32 code, const char *what) {
            qWarning() << "voip: audio policy error" << code << what;
        });
    }
    const bool ok = on ? m_set->acquire() : m_set->release();
    qInfo() << "voip: call audio" << (on ? "asked for" : "released") << (ok ? "" : "(failed)");
}
