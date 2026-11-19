// SPDX-License-Identifier: GPL-3.0-or-later
// src/ShareCopy.h: a shared path is copied into the app's cache when it is
// shared, and the copy is sent (audit S7): swapping the path for a link
// afterwards changes nothing, a link is not followed, only regular files
// are taken, and the own-files check sees the file that was opened.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QTemporaryDir>
#include <QtTest>
#include "ShareCopy.h"

#include <sys/stat.h>

class TestShareCopy : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_cache, m_own, m_outside;

    static void write(const QString &path, const QByteArray &data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
    }
    // the app's own folder is m_own: nothing in it may be shared
    QString copy(const QString &source, qint64 max = 1 << 20)
    {
        const QString own = m_own;
        return ShareCopy::fromPath(source, m_cache, QStringLiteral("shared-1"), max, 4096,
                                   [own](const QString &real) { return !real.startsWith(own + QLatin1Char('/')); });
    }

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        m_cache = m_tmp.path() + "/cache/shared";
        m_own = m_tmp.path() + "/own";
        m_outside = m_tmp.path() + "/outside";
        QDir().mkpath(m_own);
        QDir().mkpath(m_outside);
        write(m_own + "/session.json", "tokens");
    }
    void cleanup()
    {
        QDir(m_tmp.path()).removeRecursively();
        QDir().mkpath(m_tmp.path());
    }

    // Planted-bug: the path itself is passed on, not a copy (a link swapped
    // in after the share sends the session).
    void theCopyIsWhatIsSent()
    {
        const QString photo = m_outside + "/photo.jpg";
        write(photo, "picture");
        const QString sent = copy(photo);
        QCOMPARE(sent, m_cache + "/photo.jpg");
        QCOMPARE(read(sent), QByteArray("picture"));
        // swapped for a link to the session after the share
        QVERIFY(QFile::remove(photo));
        QVERIFY(QFile::link(m_own + "/session.json", photo));
        QCOMPARE(read(sent), QByteArray("picture"));
    }

    // Planted-bug: no O_NOFOLLOW (a link to a file is taken).
    void aLinkIsRefused()
    {
        write(m_outside + "/real.txt", "text");
        QVERIFY(QFile::link(m_outside + "/real.txt", m_outside + "/link.txt"));
        QCOMPARE(copy(m_outside + "/link.txt"), QString());
        QVERIFY(QFile::link(m_own + "/session.json", m_outside + "/evil.txt"));
        QCOMPARE(copy(m_outside + "/evil.txt"), QString());
    }

    // Planted-bug: the own-files check on the name instead of the opened
    // file (a folder link further up the path leads into the own folder).
    void aFolderLinkIntoOwnFilesIsRefused()
    {
        QVERIFY(QFile::link(m_own, m_outside + "/dir"));
        QCOMPARE(copy(m_outside + "/dir/session.json"), QString());
        QCOMPARE(copy(m_own + "/session.json"), QString());
        QVERIFY(!QFile::exists(m_cache + "/session.json"));
    }

    // Planted-bug: no regular-file check (a FIFO hangs the share, a device
    // is read). Planted-bug: no size cap.
    void onlyRegularFilesWithinTheCap()
    {
        QCOMPARE(copy(m_outside), QString());
        const QString fifo = m_outside + "/fifo";
        QCOMPARE(mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        QCOMPARE(copy(fifo), QString());
        write(m_outside + "/big.bin", QByteArray(10000, 'x'));
        QCOMPARE(copy(m_outside + "/big.bin", 9999), QString());
        QVERIFY(!QFile::exists(m_cache + "/big.bin"));   // nothing half-copied left
        QCOMPARE(copy(m_outside + "/big.bin", 10000), m_cache + "/big.bin");
        QCOMPARE(read(m_cache + "/big.bin").size(), 10000);
    }

    // Planted-bug: the copy follows a link that sits where it is written
    // (the session overwritten through the cache).
    void theCopyIsANewFile()
    {
        QDir().mkpath(m_cache);
        QVERIFY(QFile::link(m_own + "/session.json", m_cache + "/note.txt"));
        write(m_outside + "/note.txt", "note");
        QCOMPARE(copy(m_outside + "/note.txt"), m_cache + "/note.txt");
        QCOMPARE(read(m_own + "/session.json"), QByteArray("tokens"));
        QCOMPARE(read(m_cache + "/note.txt"), QByteArray("note"));
        QVERIFY(!QFileInfo(m_cache + "/note.txt").isSymLink());
    }

    void names()
    {
        QCOMPARE(ShareCopy::target("/c", "../../x.jpg", "shared-1"), QStringLiteral("/c/x.jpg"));
        QCOMPARE(ShareCopy::target("/c", "", "shared-1"), QStringLiteral("/c/shared-1"));
        QCOMPARE(ShareCopy::target("/c", "a/..", "shared-1"), QStringLiteral("/c/shared-1"));
    }
};

QTEST_GUILESS_MAIN(TestShareCopy)
#include "tst_sharecopy.moc"
