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
};

QTEST_GUILESS_MAIN(TestStoreKeyGuard)
#include "tst_storekeyguard.moc"
