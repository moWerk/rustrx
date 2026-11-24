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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QString>

// When SecretStore may make a new store key, and what happens to the
// stores the old one encrypted (audit SEC3). Without sailfish-secrets, so
// tests/cpp/storekeyguard can run it on the host.
namespace StoreKeyGuard {

// what sailfish-secrets answered when asked for the key
enum Fetch { Found, NoCollection, NoSecret, Failed };
// and when asked to make the collection
enum Create { Created, Existed, CreateFailed };

// A key is made only when Secrets says twice, the same way, that there is
// none, and this process has had no other error from it before: a failed
// query can come back as "no collection" (audit SEC3), so "missing" after
// an error is a failed restore, tried again later, not a fresh start.
inline bool answersAgree(Fetch first, Fetch again, bool failedBefore)
{
    if (failedBefore || (first != NoCollection && first != NoSecret))
        return false;
    return again == first;
}

// "no collection", and then making it finds one: the answers contradict
// each other, no key is made. A collection without the key is a start
// that stopped between the two steps, and goes on.
inline bool mayMint(Fetch first, Create created)
{
    if (created == CreateFailed)
        return false;
    return !(first == NoCollection && created == Existed);
}

// the folders that hold an account: the data folder itself (the first)
// and accounts/<n>
inline QStringList accountDirs(const QString &dataDir)
{
    QStringList dirs { dataDir };
    QDir more(dataDir + QLatin1String("/accounts"));
    for (const QString &name : more.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool ok = false;
        if (name.toInt(&ok) > 0 && ok)
            dirs << more.filePath(name);
    }
    return dirs;
}

// what moveAside did: (from, to) for a folder moved, ("", copy) for a copy
typedef QList<QPair<QString, QString>> Moves;

// undoes moveAside; the folders it made go once empty (rmdir only)
inline void moveBack(const Moves &moves)
{
    for (int i = moves.size() - 1; i >= 0; i--) {
        if (moves.at(i).first.isEmpty())
            QFile::remove(moves.at(i).second);
        else
            QDir().rename(moves.at(i).second, moves.at(i).first);
        const QString account = QFileInfo(moves.at(i).second).path();
        QDir().rmdir(account);
        QDir().rmdir(QFileInfo(account).path());
    }
}

// Before a new key: every store the old one encrypted (core: ENCRYPTED_MARKER
// in store/) goes to <data>/store-lost-<stamp>/<n>/, where neither the
// account sweep nor a logout looks, instead of being deleted by the core
// once the new key does not open it; a copy of its sealed session.json goes
// with it (the file itself stays: the core drops it and asks for a login,
// as before). False when a step failed; everything is then as it was.
inline bool moveAside(const QString &dataDir, const QString &stamp, Moves *moves)
{
    const QString aside = dataDir + QLatin1String("/store-lost-") + stamp;
    const QStringList dirs = accountDirs(dataDir);
    for (int i = 0; i < dirs.size(); i++) {
        const QString store = dirs.at(i) + QLatin1String("/store");
        if (!QFile::exists(store + QLatin1String("/rustrx-encrypted")))
            continue;
        const QString to = aside + QLatin1Char('/') + (i == 0 ? QStringLiteral("0") : QDir(dirs.at(i)).dirName());
        const QString session = dirs.at(i) + QLatin1String("/session.json");
        bool ok = QDir().mkpath(to) && QDir().rename(store, to + QLatin1String("/store"));
        if (ok)
            *moves << qMakePair(store, to + QLatin1String("/store"));
        if (ok && QFile::exists(session)) {
            ok = QFile::copy(session, to + QLatin1String("/session.json"));
            if (ok)
                *moves << qMakePair(QString(), to + QLatin1String("/session.json"));
        }
        if (!ok) {
            moveBack(*moves);
            moves->clear();
            QDir().rmdir(to);
            QDir().rmdir(aside);
            return false;
        }
    }
    return true;
}

}
