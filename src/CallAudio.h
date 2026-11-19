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
#pragma once

#include <QObject>

namespace ResourcePolicy { class ResourceSet; }

// While a classic call runs, Sailfish's audio policy can be asked for the
// phone's call audio, as the dialer does for its calls. Off for now (no
// class): measured on the Jolla Phone 2026, "ipcall" (the communication
// mode, earpiece and the phone's echo cancelling) is refused to an app
// ("Permission denied"), and "call" is granted but is the cellular mode:
// earpiece, but the microphone goes silent and playback is corked. A dev
// command sets a class for further tries. The streams are bound by a
// property they carry (media.name, kStreamTag): in the sandbox the
// policy's D-Bus peer is not the process that plays.
class CallAudio : public QObject
{
    Q_OBJECT
public:
    explicit CallAudio(QObject *parent = nullptr);
    void setActive(bool on);
    // tests: another policy class, or the audio bound by process too
    void configure(const QString &applicationClass, bool byProcess);
    static const char kStreamTag[];

private:
    ResourcePolicy::ResourceSet *m_set = nullptr;
    bool m_active = false;
    QString m_class;   // none: not asked
    bool m_byProcess = false;
};
