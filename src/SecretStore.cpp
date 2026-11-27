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
#include "SecretStore.h"
#include "Keys.h"
#include "StoreKeyGuard.h"
#include "Wipe.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <cstring>
#include <Sailfish/Secrets/secretmanager.h>
#include <Sailfish/Secrets/secret.h>
#include <Sailfish/Secrets/result.h>
#include <Sailfish/Secrets/createcollectionrequest.h>
#include <Sailfish/Secrets/storesecretrequest.h>
#include <Sailfish/Secrets/storedsecretrequest.h>

using namespace Sailfish::Secrets;

static const QString kName = QStringLiteral("store-key");

// the collection's name, StoreKeyGuard::collectionName (audit SEC2)
static QString s_collection;

static Secret::Identifier keyId()
{
    return Secret::Identifier(kName, s_collection, SecretManager::DefaultEncryptedStoragePluginName);
}

// 16 letters and digits for a collection name; empty without randomness
static QString randomName()
{
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return QString();
    QString out;
    while (out.size() < 16) {
        const QByteArray b = random.read(32);
        if (b.isEmpty())
            return QString();
        // below 248 (4 x 62): every character equally likely
        for (int i = 0; i < b.size() && out.size() < 16; i++)
            if (uchar(b.at(i)) < 248)
                out += QLatin1Char(kChars[uchar(b.at(i)) % 62]);
    }
    return out;
}

// The stand-in and a push wake run with nobody at the screen: they never
// ask (a locked phone's prompt waited for good, and the process held the
// bus name, so Rustrx did not open), and never make a key. Only the app
// asks and makes the first one.
static bool background()
{
    return qgetenv(Keys::kEnvBackground) == "1" || qgetenv(Keys::kEnvPushWake) == "1";
}

static SecretManager::UserInteractionMode interaction()
{
    return background() ? SecretManager::PreventInteraction : SecretManager::SystemInteraction;
}

// "missing" after another error in this process is not believed (StoreKeyGuard)
static bool s_failedBefore = false;

// the stored key, or empty when there is none (or the daemon cannot be reached)
static QByteArray fetch(SecretManager &manager, StoreKeyGuard::Fetch *answer)
{
    StoredSecretRequest request;
    request.setManager(&manager);
    request.setIdentifier(keyId());
    request.setUserInteractionMode(interaction());
    request.startRequest();
    request.waitForFinished();
    const Result r = request.result();
    if (r.code() == Result::Succeeded) {
        *answer = StoreKeyGuard::Found;
        return request.secret().data();
    }
    *answer = r.errorCode() == Result::InvalidCollectionError ? StoreKeyGuard::NoCollection
            : r.errorCode() == Result::InvalidSecretError ? StoreKeyGuard::NoSecret
            : r.errorCode() == Result::PermissionsError ? StoreKeyGuard::Refused
            : StoreKeyGuard::Failed;
    if (*answer == StoreKeyGuard::Failed)
        s_failedBefore = true;
    qInfo() << "secrets: no store key:" << r.errorCode() << r.errorMessage();
    return QByteArray();
}

static StoreKeyGuard::Create createCollection(SecretManager &manager)
{
    CreateCollectionRequest request;
    request.setManager(&manager);
    request.setCollectionName(s_collection);
    request.setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request.setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request.setAccessControlMode(SecretManager::OwnerOnlyMode);
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.startRequest();
    request.waitForFinished();
    const Result r = request.result();
    if (r.code() == Result::Succeeded)
        return StoreKeyGuard::Created;
    if (r.errorCode() == Result::CollectionAlreadyExistsError)
        return StoreKeyGuard::Existed;
    qWarning() << "secrets: cannot create the collection:" << r.errorCode() << r.errorMessage();
    return StoreKeyGuard::CreateFailed;
}

QByteArray SecretStore::storeKey(int flags, State *state)
{
    *state = NoKey;
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    s_collection = StoreKeyGuard::collectionName(dataDir, !background(), randomName());
    if (s_collection.isEmpty()) {
        qInfo() << "secrets: no collection name (none drawn yet, or it could not be written)";
        return QByteArray();
    }
    SecretManager manager;
    const bool failedBefore = s_failedBefore;
    StoreKeyGuard::Fetch first = StoreKeyGuard::Failed;
    QByteArray key = fetch(manager, &first);
    // another app holds the name (audit SEC2): no key, said apart from "no
    // Secrets"; a login asks with Redraw, and a fresh name of Rustrx's own
    // is drawn, unless a store still needs the key kept under the old one
    if (first == StoreKeyGuard::Refused) {
        if (!(flags & Redraw) || background()) {
            *state = Refused;
            return QByteArray();
        }
        const QString fresh = StoreKeyGuard::redrawName(dataDir, randomName());
        if (fresh.isEmpty()) {
            qWarning() << "secrets: the collection is refused, and no fresh name could be drawn";
            *state = Refused;
            return QByteArray();
        }
        qWarning() << "secrets: the collection was refused; a fresh name for this login";
        s_collection = fresh;
        key = fetch(manager, &first);
        if (first == StoreKeyGuard::Refused) {
            *state = Refused;
            return QByteArray();
        }
    }
    if (!key.isEmpty())
        *state = KeyOk;
    if (!key.isEmpty() || first == StoreKeyGuard::Failed || background())
        return key;   // the key, or sailfish-secrets does not work: no key

    // the first time, or the key is really gone: asked once more, and a key
    // is made only when the answers agree (audit SEC3); otherwise this is a
    // failed restore and the next try asks again
    StoreKeyGuard::Fetch again = StoreKeyGuard::Failed;
    key = fetch(manager, &again);
    if (!key.isEmpty()) {
        *state = KeyOk;
        return key;
    }
    if (!StoreKeyGuard::answersAgree(first, again, failedBefore)) {
        qWarning() << "secrets: no new store key, the answers disagree:" << first << again << failedBefore;
        return QByteArray();
    }
    // the key is gone while stores it encrypted are here: a new one only
    // after the user's yes (audit SEC3); they could open again should the
    // key come back, which a new one would end
    if (StoreKeyGuard::encryptedStores(dataDir) && !(flags & StartOver)) {
        qWarning() << "secrets: the store key is missing and encrypted stores are here; asking first";
        *state = Lost;
        return QByteArray();
    }
    const StoreKeyGuard::Create created = createCollection(manager);
    if (!StoreKeyGuard::mayMint(first, created)) {
        if (created != StoreKeyGuard::CreateFailed)
            qWarning() << "secrets: no new store key: the collection was missing, then there";
        return QByteArray();
    }

    // 32 random bytes as hex, in a collection of our own
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return QByteArray();
    QByteArray bytes = random.read(32);
    if (bytes.size() != 32) {
        Wipe::array(bytes);
        return QByteArray();
    }

    // stores the old key encrypted are moved aside first, not deleted
    // (they open again should that key come back); back in place when the
    // new key is not kept
    StoreKeyGuard::Moves moves;
    if (!StoreKeyGuard::moveAside(dataDir, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")), &moves)) {
        qWarning() << "secrets: no new store key: the old encrypted stores could not be moved aside";
        Wipe::array(bytes);
        return QByteArray();
    }
    key = bytes.toHex();
    Wipe::array(bytes);

    Secret secret(keyId());
    secret.setType(Secret::TypeBlob);
    secret.setData(key);
    StoreSecretRequest request;
    request.setManager(&manager);
    request.setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.setSecret(secret);
    request.startRequest();
    request.waitForFinished();
    if (request.result().code() != Result::Succeeded) {
        qWarning() << "secrets: cannot store the key:" << request.result().errorCode() << request.result().errorMessage();
        StoreKeyGuard::moveBack(moves);
        Wipe::array(key);
        return QByteArray();
    }
    if (!moves.isEmpty())
        qWarning() << "secrets: a new store key; the stores the old one encrypted are kept in"
                   << QFileInfo(QFileInfo(moves.first().second).path()).path();
    qInfo() << "secrets: a new store key is kept in sailfish-secrets";
    *state = KeyOk;
    return key;
}

extern "C" Q_DECL_EXPORT int rustrx_secrets_store_key2(char *out, int size, int flags, int *state)
{
    // the only owner of the key's buffer once storeKey() has returned: wiped
    // after the copy (audit B13)
    SecretStore::State st = SecretStore::NoKey;
    QByteArray key = SecretStore::storeKey(flags, &st);
    if (state)
        *state = st;
    const int n = key.size();
    if (n > 0 && n < size) {
        memcpy(out, key.constData(), size_t(n));
        out[n] = 0;
    }
    Wipe::array(key);
    return n > 0 && n < size ? n : 0;
}
