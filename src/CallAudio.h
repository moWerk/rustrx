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

// While a classic call runs, Sailfish's audio policy is asked for the
// phone's call audio (class "ipcall"), as the dialer does for its calls:
// the policy then treats the call's streams as a call, not as media. The
// streams are bound by a property they carry (media.name, kStreamTag),
// not only by process: in the sandbox the policy's D-Bus peer is not the
// process that plays.
class CallAudio : public QObject
{
    Q_OBJECT
public:
    explicit CallAudio(QObject *parent = nullptr);
    void setActive(bool on);
    static const char kStreamTag[];

private:
    ResourcePolicy::ResourceSet *m_set = nullptr;
    bool m_active = false;
};
