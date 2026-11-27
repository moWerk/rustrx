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
// what came of asking for the key, besides the key
enum State {
    KeyOk = 0,
    NoKey = 1,      // none to be had (no Secrets, locked, not answered)
    Refused = 2,    // the collection's name is another app's (audit SEC2)
    Lost = 3,       // no key, but stores the old one encrypted: asks first (SEC3)
};
enum Flags {
    StartOver = 1,  // the user said "Start over": a new key, the old stores aside
    Redraw = 2,     // a login after a refusal: a fresh collection name first
};
QByteArray storeKey(int flags, State *state);
}

// the plugin's entry point (librustrx-secrets.so): the key into `out`,
// returns its length, 0 when there is none; `state` (may be null) gets a
// SecretStore::State, `flags` are SecretStore::Flags
extern "C" int rustrx_secrets_store_key2(char *out, int size, int flags, int *state);
