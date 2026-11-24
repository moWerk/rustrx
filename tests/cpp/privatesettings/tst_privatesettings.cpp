// SPDX-License-Identifier: GPL-3.0-or-later
// src/PrivateSettings: the settings other apps must not reach live in
// Rustrx's own file (audit SEC1). The one move out of dconf runs against a
// fake dconf here; the real one (DconfOld.h, mlite5) only runs on a phone.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "PrivateSettings.h"

#include <unistd.h>

// dconf as Nemo.Configuration left it: typed values, unset keys absent
class FakeDconf : public PrivateStore::OldValues
{
public:
    QVariantHash keys;
    QStringList unsetKeys;
    int reads = 0;
    int syncs = 0;
    QVariant value(const QString &key) override
    {
        reads++;
        return keys.value(key);
    }
    void unset(const QString &key) override
    {
        unsetKeys << key;
        keys.remove(key);
    }
    bool sync() override
    {
        syncs++;
        return true;
    }
};

class TestPrivateSettings : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString freshFile() { return m_tmp.filePath(QStringLiteral("conf-%1/harbour-rustrx.conf").arg(++m_n)); }

    static FakeDconf userChoices()
    {
        FakeDconf d;
        d.keys.insert(QStringLiteral("sendReceipts"), false);
        d.keys.insert(QStringLiteral("showOnline"), true);
        d.keys.insert(QStringLiteral("notifyText"), false);
        d.keys.insert(QStringLiteral("closedMode"), QStringLiteral("off"));
        d.keys.insert(QStringLiteral("lastRoom"), QStringLiteral("@!room:example.org"));
        d.keys.insert(QStringLiteral("lastRoomAccount"), qlonglong(2));
        d.keys.insert(QStringLiteral("searchHistory"),
                      QStringLiteral("[{\"term\":\"a, b\",\"name\":\"x = \\\"y\\\"\",\"account\":\"@me:example.org\"}]"));
        // the look stays in dconf: not ours
        d.keys.insert(QStringLiteral("avatarCorners"), 0);
        return d;
    }

private slots:
    // Planted-bug: a key or default changed in one place only (Settings.qml
    // aliases a property the store does not know, or the other way round).
    void everyKeyIsAProperty()
    {
        PrivateSettings face;
        for (const QString &key : PrivateStore::keys()) {
            const int i = face.metaObject()->indexOfProperty(key.toLatin1().constData());
            QVERIFY2(i >= 0, qPrintable(key));
            QVERIFY2(face.metaObject()->property(i).hasNotifySignal(), qPrintable(key));
        }
        QCOMPARE(face.metaObject()->propertyCount() - face.metaObject()->propertyOffset(), PrivateStore::keys().size());
    }

    // the defaults are what Settings.qml had before 0.8.4.
    // Planted-bug: sendReceipts' default false.
    void defaultsAsBefore()
    {
        PrivateStore store(freshFile());
        PrivateSettings s;
        QCOMPARE(s.sendReceipts(), true);
        QCOMPARE(s.readWhenShown(), true);
        QCOMPARE(s.sendTyping(), true);
        QCOMPARE(s.showOnline(), false);
        QCOMPARE(s.chatDetails(), true);
        QCOMPARE(s.strangerInvites(), true);
        QCOMPARE(s.notifyText(), true);
        QCOMPARE(s.closedMode(), QString());
        QCOMPARE(s.reopenLast(), false);
        QCOMPARE(s.lastRoom(), QString());
        QCOMPARE(s.lastRoomAccount(), 0);
        QCOMPARE(s.secretsNoticeSeen(), false);
        QCOMPARE(s.callNoticeSeen(), false);
        QCOMPARE(s.searchHistory(), QStringLiteral("[]"));
    }

    // the user's choices in dconf are in the file afterwards, typed, also
    // read back by a later start; dconf no longer has them.
    // Planted-bug: the values kept as the file's strings ("false" is true
    // in JS), or the dconf keys left in place.
    void migrationKeepsChoices()
    {
        const QString file = freshFile();
        FakeDconf dconf = userChoices();
        {
            PrivateStore store(file);
            QVERIFY(store.migrate(dconf));
        }
        QCOMPARE(dconf.unsetKeys.size(), 7);
        QVERIFY(!dconf.unsetKeys.contains(QStringLiteral("avatarCorners")));
        QVERIFY(dconf.keys.contains(QStringLiteral("avatarCorners")));
        QCOMPARE(dconf.syncs, 1);

        PrivateStore again(file);
        PrivateSettings s;
        QCOMPARE(again.value(QStringLiteral("sendReceipts")).type(), QVariant::Bool);
        QCOMPARE(s.sendReceipts(), false);
        QCOMPARE(s.showOnline(), true);
        QCOMPARE(s.notifyText(), false);
        QCOMPARE(s.sendTyping(), true);   // not set in dconf: the default
        QCOMPARE(s.closedMode(), QStringLiteral("off"));
        QCOMPARE(s.lastRoom(), QStringLiteral("@!room:example.org"));
        QCOMPARE(s.lastRoomAccount(), 2);
        QCOMPARE(s.searchHistory(), userChoices().keys.value(QStringLiteral("searchHistory")).toString());
    }

    // after the move dconf is never read again: what another app writes
    // there later changes nothing.
    // Planted-bug: no done flag (every start copies dconf over).
    void movedOnlyOnce()
    {
        const QString file = freshFile();
        FakeDconf dconf;
        {
            PrivateStore store(file);
            QVERIFY(store.migrate(dconf));
        }
        FakeDconf hostile;
        hostile.keys.insert(QStringLiteral("sendReceipts"), false);
        hostile.keys.insert(QStringLiteral("showOnline"), true);
        PrivateStore store(file);
        QVERIFY(store.migrate(hostile));
        QCOMPARE(hostile.reads, 0);
        QCOMPARE(store.value(QStringLiteral("sendReceipts")).toBool(), true);
        QCOMPARE(store.value(QStringLiteral("showOnline")).toBool(), false);
    }

    // a file that cannot be written: dconf keeps the values for the next
    // start, this one uses them.
    // Planted-bug: dconf emptied without checking the write, or the done
    // flag left pending after the failed write.
    void unwritableFileKeepsDconf()
    {
        if (geteuid() == 0)
            QSKIP("root writes anyway");
        const QString file = freshFile();
        const QString dir = QFileInfo(file).path();
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));
        FakeDconf dconf = userChoices();
        {
            PrivateStore store(file);
            QVERIFY(!store.migrate(dconf));
            QCOMPARE(store.value(QStringLiteral("sendReceipts")).toBool(), false);
            QVERIFY(dconf.unsetKeys.isEmpty());
            // writable again later in the same run: a change is written,
            // the move is not marked done by it
            QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
            store.setValue(QStringLiteral("sendTyping"), false);
        }
        QVERIFY(dconf.unsetKeys.isEmpty());
        PrivateStore store(file);
        QVERIFY(store.migrate(dconf));
        QCOMPARE(dconf.unsetKeys.size(), 7);
        QCOMPARE(store.value(QStringLiteral("showOnline")).toBool(), true);
        QCOMPARE(store.value(QStringLiteral("sendTyping")).toBool(), false);
    }

    // a value set in one engine reaches the other's object with its signal,
    // and a later start.
    // Planted-bug: relay() not connected (Service.qml would keep the old
    // notifyText until a restart).
    void bothEnginesHearAChange()
    {
        const QString file = freshFile();
        {
            PrivateStore store(file);
            PrivateSettings window, service;
            QSignalSpy heard(&service, &PrivateSettings::notifyTextChanged);
            QSignalSpy other(&service, &PrivateSettings::chatDetailsChanged);
            window.setNotifyText(false);
            QCOMPARE(heard.count(), 1);
            QCOMPARE(other.count(), 0);
            QCOMPARE(service.notifyText(), false);
            window.setNotifyText(false);
            QCOMPARE(heard.count(), 1);   // no change, no signal
            window.setLastRoomAccount(3);
        }
        PrivateStore store(file);
        PrivateSettings s;
        QCOMPARE(s.notifyText(), false);
        QCOMPARE(s.lastRoomAccount(), 3);
    }
};

QTEST_GUILESS_MAIN(TestPrivateSettings)
#include "tst_privatesettings.moc"
