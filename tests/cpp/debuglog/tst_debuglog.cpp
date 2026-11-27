// SPDX-License-Identifier: GPL-3.0-or-later
// What the debug log cuts from a line before it is written (DebugLog::redact,
// audit 0.8.4 SEC7), and that it reaches the file that way.
#include "DebugLog.h"
#include "Keys.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class TestDebugLog : public QObject
{
    Q_OBJECT
    QTemporaryDir m_home;
private slots:
    // the data folder in a temporary one, nothing under ~
    void initTestCase() { QVERIFY(m_home.isValid()); qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.path())); }

    void redact_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("logged");
        QTest::newRow("plain") << "perf: room 120 ms rooms=40" << "perf: room 120 ms rooms=40";
        QTest::newRow("ids stay") << "call: incoming !abcdefghijklmnopqrstuvwxyz:example.org $AbCdEfGhIjKlMnOpQrStUvWxYz0123456789"
                                  << "call: incoming !abcdefghijklmnopqrstuvwxyz:example.org $AbCdEfGhIjKlMnOpQrStUvWxYz0123456789";
        QTest::newRow("url without query") << "fetch https://example.org/_matrix/client/v3/sync failed"
                                           << "fetch https://example.org/_matrix/client/v3/sync failed";
        QTest::newRow("url query") << "error sending request for url (https://example.org/_matrix/media/v3/download/x?access_token=SECRETSECRET&a=1): timeout"
                                   << "error sending request for url (https://example.org/_matrix/media/v3/download/x?[cut]): timeout";
        QTest::newRow("query, fragment kept") << "open https://call.element.io/room?secret=1#/abc" << "open https://call.element.io/room?[cut]#/abc";
        QTest::newRow("turn credentials") << "voip: warning from nice: turn://1700000000%3Aalice:c2VjcmV0cGFzcw%3D%3D@turn.example.org:3478?transport=udp"
                                          << "voip: warning from nice: turn://[cut]@turn.example.org:3478?[cut]";
        QTest::newRow("turns credentials") << "relay turns://u:p@t.example.org:5349" << "relay turns://[cut]@t.example.org:5349";
        QTest::newRow("access_token=") << "body access_token=syt_abc_DEF123 next" << "body access_token=[cut] next";
        QTest::newRow("json token") << "{\"access_token\":\"syt_abc\",\"refresh_token\": \"syr_def\",\"x\":1}"
                                    << "{\"access_token\":\"[cut]\",\"refresh_token\": \"[cut]\",\"x\":1}";
        QTest::newRow("token=") << "push token=0b5e-11 registered" << "push token=[cut] registered";
        QTest::newRow("short token") << "openid token: abc123def456ghi789jkl012" << "openid token: [cut]";
        QTest::newRow("word token") << "the token expired, logging in again" << "the token expired, logging in again";
        QTest::newRow("bearer") << "Authorization: Bearer syt_YWxpY2U_abcdef_123 sent" << "Authorization: Bearer [cut] sent";
    }
    void redact()
    {
        QFETCH(QString, line);
        QFETCH(QString, logged);
        QCOMPARE(DebugLog::redact(line), logged);
        // cut once is cut for good
        QCOMPARE(DebugLog::redact(logged), logged);
    }

    // through the message handler into the file
    void theFileGetsTheCutLine()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + QLatin1String(Keys::kLogDir);
        QFile::remove(dir + QStringLiteral("/test.log"));
        DebugLog::install(QStringLiteral("test"));
        DebugLog::setEnabled(true);
        qInfo().noquote() << "GET https://example.org/x?access_token=PLANTED-SECRET Bearer PLANTED-BEARER";
        qInstallMessageHandler(nullptr);
        QFile f(dir + QStringLiteral("/test.log"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray written = f.readAll();
        QVERIFY(written.contains("GET https://example.org/x?[cut] Bearer [cut]"));
        QVERIFY(!written.contains("PLANTED"));
        f.remove();
    }

    // the bundle for the share sheet is made in the folder given (the
    // app's cache, audit SEC8), and the copies are removed by name only:
    // other files stay, a link is removed, not what it points at.
    // Planted-bug: removeCopies follows a link (QFile::remove on its target).
    void copiesGoByName()
    {
        QTemporaryDir tmp;
        const QString shared = tmp.filePath(QStringLiteral("cache/shared-log"));
        const QString first = DebugLog::collect(shared, QStringLiteral("header"));
        QVERIFY(!first.isEmpty());
        QCOMPARE(QFileInfo(first).path(), shared);
        QVERIFY(QFileInfo(first).fileName().startsWith(QStringLiteral("rustrx-log-")));
        const QString downloads = tmp.filePath(QStringLiteral("Downloads"));
        QVERIFY(QDir().mkpath(downloads + QStringLiteral("/Rustrx")));
        QFile keep(downloads + QStringLiteral("/holiday.jpg"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.close();
        QFile old(downloads + QStringLiteral("/Rustrx/rustrx-log-20261101-1200.txt"));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.close();
        const QString target = tmp.filePath(QStringLiteral("private-file"));
        QFile t(target);
        QVERIFY(t.open(QIODevice::WriteOnly));
        t.close();
        QVERIFY(QFile::link(target, downloads + QStringLiteral("/rustrx-log-planted.txt")));
        QCOMPARE(DebugLog::removeCopies(downloads), 2);
        QVERIFY(QFile::exists(downloads + QStringLiteral("/holiday.jpg")));
        QVERIFY(QFile::exists(target));
        QCOMPARE(DebugLog::removeCopies(shared), 1);
        QVERIFY(!QFile::exists(first));
    }
};

QTEST_GUILESS_MAIN(TestDebugLog)
#include "tst_debuglog.moc"
