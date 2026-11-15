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

#include <QByteArray>

// The key that encrypts the local store, kept in sailfish-secrets: a
// collection of its own, locked by the device lock, open to this app only.
// Empty when sailfish-secrets is missing or fails; the store then stays
// unencrypted, as before (community ports without sailfish-secrets).
namespace SecretStore {
QByteArray storeKey();
}

// the plugin's entry point (librustrx-secrets.so): the key into `out`,
// returns its length, 0 when there is none
extern "C" int rustrx_secrets_store_key(char *out, int size);
