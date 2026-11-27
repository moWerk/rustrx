// SPDX-License-Identifier: GPL-3.0-or-later
// src/StoreKeyGuard.h: when SecretStore may make a new store key, and that
// the stores the old key encrypted are moved aside, not lost (audit SEC3).
// The Secrets calls themselves need a phone.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QTemporaryDir>
#include <QtTest>
#include "StoreKeyGuard.h"

using namespace StoreKeyGuard;

class TestStoreKeyGuard : public QObject
{
    Q_OBJECT

    static void write(const QString &path, const QByteArray &data = QByteArray("x"))
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    // an account folder with an encrypted store and its sealed session
    static void account(const QString &dir, bool encrypted = true)
    {
        write(dir + QStringLiteral("/store/matrix-sdk-state.sqlite3"), "state");
        if (encrypted)
            write(dir + QStringLiteral("/store/rustrx-encrypted"), "fnv1a64:0123456789abcdef");
        write(dir + QStringLiteral("/session.json"), "sealed");
        QVERIFY(QFile::setPermissions(dir + QStringLiteral("/session.json"), QFile::ReadOwner | QFile::WriteOwner));
        write(dir + QStringLiteral("/media/a.jpg"));
    }

private slots:
    // Planted-bug: "missing" believed after an error, or after a second
    // answer that differs.
    void answersMustAgree()
    {
        QVERIFY(answersAgree(NoCollection, NoCollection, false));
        QVERIFY(answersAgree(NoSecret, NoSecret, false));
        QVERIFY(!answersAgree(NoCollection, NoCollection, true));
        QVERIFY(!answersAgree(NoCollection, NoSecret, false));
        QVERIFY(!answersAgree(NoSecret, Failed, false));
        QVERIFY(!answersAgree(NoCollection, Found, false));
        QVERIFY(!answersAgree(Failed, Failed, false));
        QVERIFY(!answersAgree(Found, Found, false));
    }

    // Planted-bug: CollectionAlreadyExists counted as success after "no
    // collection" (the transient query error, audit SEC3).
    void noCollectionThenOneIsAContradiction()
    {
        QVERIFY(mayMint(NoCollection, Created));
        QVERIFY(!mayMint(NoCollection, Existed));
        QVERIFY(mayMint(NoSecret, Existed));
        QVERIFY(mayMint(NoSecret, Created));
        QVERIFY(!mayMint(NoSecret, CreateFailed));
        QVERIFY(!mayMint(NoCollection, CreateFailed));
    }

    // every encrypted store goes aside with a copy of its session, nothing
    // is deleted, the session file, a plain store and the rest of the
    // folder stay (the start's sweep removes an accounts/<n> without
    // session.json: moving it would take the account's folder in the same
    // start), and the place is outside the account folders.
    // Planted-bug: the store removed instead of moved, the session moved,
    // or the copy readable by others.
    void encryptedStoresGoAside()
    {
        QTemporaryDir tmp;
        const QString data = tmp.path();
        account(data);
        account(data + QStringLiteral("/accounts/2"));
        account(data + QStringLiteral("/accounts/3"), false);
        write(data + QStringLiteral("/accounts/notanumber/store/rustrx-encrypted"));
        Moves moves;
        QVERIFY(moveAside(data, QStringLiteral("20261124-120000"), &moves));
        QCOMPARE(moves.size(), 4);
        const QString aside = data + QStringLiteral("/store-lost-20261124-120000");
        QCOMPARE(QFileInfo(aside).path(), data);
        QVERIFY(QFile::exists(aside + QStringLiteral("/0/store/matrix-sdk-state.sqlite3")));
        QVERIFY(QFile::exists(aside + QStringLiteral("/0/store/rustrx-encrypted")));
        QVERIFY(QFile::exists(aside + QStringLiteral("/0/session.json")));
        QVERIFY(QFile::exists(data + QStringLiteral("/session.json")));
        QCOMPARE(QFile::permissions(aside + QStringLiteral("/0/session.json")) & 0x0077, QFileDevice::Permissions());
        QVERIFY(QFile::exists(aside + QStringLiteral("/2/store/matrix-sdk-state.sqlite3")));
        QVERIFY(QFile::exists(aside + QStringLiteral("/2/session.json")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/2/session.json")));
        QVERIFY(!QFile::exists(data + QStringLiteral("/store")));
        QVERIFY(!QFile::exists(data + QStringLiteral("/accounts/2/store")));
        QVERIFY(QFile::exists(data + QStringLiteral("/media/a.jpg")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/3/store/matrix-sdk-state.sqlite3")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/3/session.json")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/notanumber/store/rustrx-encrypted")));
    }

    // the key could not be kept: everything back where it was, the aside
    // folder gone.
    // Planted-bug: moveBack leaves the session's copy.
    void moveBackRestores()
    {
        QTemporaryDir tmp;
        const QString data = tmp.path();
        account(data);
        account(data + QStringLiteral("/accounts/1"));
        Moves moves;
        QVERIFY(moveAside(data, QStringLiteral("s"), &moves));
        moveBack(moves);
        QVERIFY(QFile::exists(data + QStringLiteral("/store/rustrx-encrypted")));
        QVERIFY(QFile::exists(data + QStringLiteral("/session.json")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/1/store/matrix-sdk-state.sqlite3")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/1/session.json")));
        QVERIFY(!QFile::exists(data + QStringLiteral("/store-lost-s")));
    }

    // no encrypted store: nothing moved, no folder made
    void nothingToMove()
    {
        QTemporaryDir tmp;
        account(tmp.path(), false);
        Moves moves;
        QVERIFY(moveAside(tmp.path(), QStringLiteral("s"), &moves));
        QVERIFY(moves.isEmpty());
        QVERIFY(!QFile::exists(tmp.filePath(QStringLiteral("store-lost-s"))));
        QVERIFY(QFile::exists(tmp.filePath(QStringLiteral("store/matrix-sdk-state.sqlite3"))));
    }

    // a move that fails (the target taken) undoes the earlier ones and says
    // so: no key is made then.
    // Planted-bug: true returned after a failed move, or the first
    // account's store left aside.
    void failedMoveUndoes()
    {
        QTemporaryDir tmp;
        const QString data = tmp.path();
        account(data);
        account(data + QStringLiteral("/accounts/1"));
        // something already where account 1's store would go
        write(data + QStringLiteral("/store-lost-s/1/store/keep"));
        Moves moves;
        QVERIFY(!moveAside(data, QStringLiteral("s"), &moves));
        QVERIFY(moves.isEmpty());
        QVERIFY(QFile::exists(data + QStringLiteral("/store/rustrx-encrypted")));
        QVERIFY(QFile::exists(data + QStringLiteral("/session.json")));
        QVERIFY(QFile::exists(data + QStringLiteral("/accounts/1/store/rustrx-encrypted")));
        QVERIFY(QFile::exists(data + QStringLiteral("/store-lost-s/1/store/keep")));
    }

    // a "Refused" (another app's collection) is no "missing": no key is
    // made from it, and nothing is moved (audit SEC2).
    // Planted-bug: Refused taken as NoCollection.
    void refusedIsNotMissing()
    {
        QVERIFY(!answersAgree(Refused, Refused, false));
        QVERIFY(!answersAgree(Refused, NoCollection, false));
        QVERIFY(!answersAgree(NoCollection, Refused, false));
    }

    // the key is gone while a store it encrypted is here: SecretStore asks
    // before a new one (SEC3). Any account counts, a plain store does not.
    // Planted-bug: only the first account's folder looked at.
    void encryptedStoresAreSeen()
    {
        QTemporaryDir tmp;
        const QString data = tmp.path();
        QVERIFY(!encryptedStores(data));
        account(data + QStringLiteral("/accounts/3"), false);
        QVERIFY(!encryptedStores(data));
        account(data + QStringLiteral("/accounts/2"));
        QVERIFY(encryptedStores(data));
    }

    // A new install draws a name of its own, once; an install from before
    // (an encrypted store, or a session file) keeps "rustrx", the name its
    // key lies under; the stand-in and a push wake never write the file
    // and look for no key on a new install. A bad file is not taken.
    // Planted-bug: a new name drawn for an install with an encrypted store
    // (its key would be looked for in an empty collection, and the next
    // step makes a new one).
    void collectionNames()
    {
        const QString random = QStringLiteral("AbCdEfGh01234567");
        {
            QTemporaryDir tmp;
            QCOMPARE(collectionName(tmp.path(), false, random), QString());
            QVERIFY(!QFile::exists(tmp.filePath(QLatin1String(kNameFile))));
            QCOMPARE(collectionName(tmp.path(), true, random), QStringLiteral("rustrxAbCdEfGh01234567"));
            QCOMPARE(collectionName(tmp.path(), true, QStringLiteral("zzzzzzzzzzzzzzzz")), QStringLiteral("rustrxAbCdEfGh01234567"));
            QCOMPARE(collectionName(tmp.path(), false, QString()), QStringLiteral("rustrxAbCdEfGh01234567"));
        }
        {
            QTemporaryDir tmp;
            account(tmp.path());
            QCOMPARE(collectionName(tmp.path(), false, random), QStringLiteral("rustrx"));
            QVERIFY(!QFile::exists(tmp.filePath(QLatin1String(kNameFile))));
            QCOMPARE(collectionName(tmp.path(), true, random), QStringLiteral("rustrx"));
            QCOMPARE(readName(tmp.path()), QStringLiteral("rustrx"));
        }
        {
            // a plain store with its session (no key ever): from before too
            QTemporaryDir tmp;
            account(tmp.path() + QStringLiteral("/accounts/1"), false);
            QCOMPARE(collectionName(tmp.path(), true, random), QStringLiteral("rustrx"));
        }
        {
            QTemporaryDir tmp;
            write(tmp.filePath(QLatin1String(kNameFile)), "../../evil name");
            QCOMPARE(collectionName(tmp.path(), true, random), QStringLiteral("rustrxAbCdEfGh01234567"));
            // a name that cannot be kept is not used
            QCOMPARE(collectionName(QStringLiteral("/nonexistent-rustrx-test"), true, random), QString());
            // the plugin takes under 32
            QVERIFY(!validName(QStringLiteral("rustrx") + QString(26, QLatin1Char('a'))));
            QVERIFY(validName(QStringLiteral("rustrx") + QString(25, QLatin1Char('a'))));
        }
    }

    // after a refusal the next login draws a fresh name, but never while a
    // store needs the key under the old one.
    // Planted-bug: redrawn with an encrypted store here.
    void redrawOnlyWithoutEncryptedStores()
    {
        QTemporaryDir tmp;
        QCOMPARE(collectionName(tmp.path(), true, QStringLiteral("aaaaaaaaaaaaaaaa")), QStringLiteral("rustrxaaaaaaaaaaaaaaaa"));
        QCOMPARE(redrawName(tmp.path(), QStringLiteral("bbbbbbbbbbbbbbbb")), QStringLiteral("rustrxbbbbbbbbbbbbbbbb"));
        QCOMPARE(collectionName(tmp.path(), false, QString()), QStringLiteral("rustrxbbbbbbbbbbbbbbbb"));
        account(tmp.path());
        QCOMPARE(redrawName(tmp.path(), QStringLiteral("cccccccccccccccc")), QString());
        QCOMPARE(readName(tmp.path()), QStringLiteral("rustrxbbbbbbbbbbbbbbbb"));
    }
};

QTEST_GUILESS_MAIN(TestStoreKeyGuard)
#include "tst_storekeyguard.moc"
