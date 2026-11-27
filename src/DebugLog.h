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

#include <QString>

// A debug log in the app's data folder, on unless switched off (until 1.0):
// every line Rustrx writes to the system log (the core's included) also goes
// to logs/<name>.log, at most 1 MB and one older file, so a user can share
// what happened without having to make it happen again.
namespace DebugLog {
// install the message handler; `name` is "app" or "background"
void install(const QString &name);
void setEnabled(bool on);
bool enabled();
// the app's and the stand-in's logs in one text file in `dir`, with a
// header (versions, device); returns its path, empty on failure
QString collect(const QString &dir, const QString &header);
// the logs emptied (the open one keeps writing, from the start) and the
// copies shared to `dir` before removed; returns how many copies went
int clear(const QString &dir);
// the collected copies (rustrx-log-*.txt) in `dir` and in its Rustrx
// folder removed; returns how many went
int removeCopies(const QString &dir);
// a line as it is logged: URL queries and user:password parts (TURN),
// access_token / token values and Bearer tokens cut (audit 0.8.4 SEC7)
QString redact(const QString &line);
}
