// SPDX-License-Identifier: GPL-3.0-or-later
// Which received files are handed to the phone's apps (OpenCheck.h, audit
// UC5): the name and the bytes must both be an allowed kind, and the bytes
// may not say something else than the name. The host's shared-mime-info
// stands in for the phone's.
//
// Planted-bug: mayOpen by the name alone (byContent not checked): the RPM
// named photo.jpg, the script and the desktop entry named .txt open.
// Planted-bug: sound and video only by the exact type (the Opus voice
// message named .ogg and the .m4a are refused).
// Planted-bug: allowedKind without the playlist exclusion: the playlists
// open in a player that fetches their addresses.
#include "OpenCheck.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

class TestOpenCheck : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &bytes)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size())
            return QString();
        return path;
    }
    QString picture(const QString &name, const char *format)
    {
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(Qt::blue);
        const QString path = m_dir.filePath(name);
        return img.save(path, format) ? path : QString();
    }
    // a zip's first entry: "PK\3\4", a header, then the entry's name at 30
    static QByteArray zip(const QByteArray &firstName, const QByteArray &content)
    {
        QByteArray z("PK\x03\x04", 4);
        z += QByteArray(22, '\0');
        z += char(firstName.size()); z += char(0);
        z += QByteArray(2, '\0');
        return z + firstName + content + QByteArray(64, '\0');
    }

    // an Ogg page with one packet: its first bytes after the page header
    static QByteArray ogg(const QByteArray &packet)
    {
        QByteArray o("OggS\0\x02", 6);
        o += QByteArray(20, '\0');
        o += char(1);
        o += char(packet.size() + 10);
        return o + packet + QByteArray(210, '\0');
    }
    static QByteArray mp4(const char *brand)
    {
        return QByteArray("\0\0\0\x18" "ftyp", 8) + brand + QByteArray(4, '\0') + brand + "isom" + QByteArray(200, '\0');
    }

private slots:
    void opens_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("opens");
        QTest::newRow("jpeg") << picture(QStringLiteral("photo.jpg"), "JPEG") << true;
        QTest::newRow("png") << picture(QStringLiteral("shot.png"), "PNG") << true;
        QTest::newRow("pdf") << write(QStringLiteral("paper.pdf"), "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n1 0 obj\n") << true;
        QTest::newRow("text") << write(QStringLiteral("notes.txt"), "Just some notes.\nTwo lines.\n") << true;
        QTest::newRow("odt") << write(QStringLiteral("letter.odt"),
                                      zip("mimetype", "application/vnd.oasis.opendocument.text")) << true;
        // a docx is a zip by its bytes: allowed, the name's type belongs to it
        QTest::newRow("docx") << write(QStringLiteral("cv.docx"), zip("[Content_Types].xml", "<Types/>")) << true;
        // an old .doc is an OLE file by its first bytes
        QTest::newRow("doc") << write(QStringLiteral("old.doc"),
                                      QByteArray("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8) + QByteArray(600, '\0')) << true;

        // sound and video whose bytes name a kind of the name's type
        QTest::newRow("opus as ogg") << write(QStringLiteral("voice.ogg"), ogg("OpusHead\x01\x01")) << true;
        QTest::newRow("vorbis as ogg") << write(QStringLiteral("song.ogg"), ogg("\x01vorbis")) << true;
        QTest::newRow("m4a, mp4 brand") << write(QStringLiteral("talk.m4a"), mp4("mp42")) << true;
        QTest::newRow("mp4") << write(QStringLiteral("clip.mp4"), mp4("isom")) << true;
        QTest::newRow("mp3") << write(QStringLiteral("song.mp3"), QByteArray("ID3\x03\0\0\0\0\0\0\xff\xfb\x90\0", 14) + QByteArray(400, '\0')) << true;
        QTest::newRow("pdf as mp4") << write(QStringLiteral("clip2.mp4"), "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n") << false;
        QTest::newRow("playlist as mp3") << write(QStringLiteral("song2.mp3"), "[playlist]\nFile1=http://example.org/a\nNumberOfEntries=1\n") << false;

        const QByteArray rpm = QByteArray("\xed\xab\xee\xdb\x03\x00\x00\x00", 8) + QByteArray(200, '\0');
        QTest::newRow("rpm as jpg") << write(QStringLiteral("photo2.jpg"), rpm) << false;
        QTest::newRow("rpm, no suffix") << write(QStringLiteral("photo"), rpm) << false;
        QTest::newRow("rpm") << write(QStringLiteral("setup.rpm"), rpm) << false;
        QTest::newRow("script as txt") << write(QStringLiteral("readme.txt"), "#!/bin/sh\nrm -rf ~\n") << false;
        QTest::newRow("desktop as txt") << write(QStringLiteral("note.txt"),
                                                 "[Desktop Entry]\nType=Application\nExec=sh -c x\n") << false;
        QTest::newRow("html as txt") << write(QStringLiteral("page.txt"), "<!DOCTYPE html><html><script>") << false;
        QTest::newRow("png as jpg") << picture(QStringLiteral("odd.jpg"), "PNG") << false;
        QTest::newRow("svg") << write(QStringLiteral("logo.svg"),
                                      "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\"/>") << false;
        QTest::newRow("html") << write(QStringLiteral("page.html"), "<!DOCTYPE html><html></html>") << false;
        QTest::newRow("playlist") << write(QStringLiteral("list.m3u"), "#EXTM3U\nhttp://example.org/a.mp3\n") << false;
        QTest::newRow("playlist vlc") << write(QStringLiteral("list.vlc"), "#EXTM3U\nhttp://example.org/a.mp3\n") << false;
        QTest::newRow("playlist pls") << write(QStringLiteral("list.pls"), "[playlist]\nFile1=http://example.org/a\nNumberOfEntries=1\n") << false;
        QTest::newRow("playlist asx") << write(QStringLiteral("list.asx"),
                                               "<asx version=\"3.0\"><entry><ref href=\"http://example.org/a\"/></entry></asx>\n") << false;
        QTest::newRow("shell") << write(QStringLiteral("run.sh"), "#!/bin/sh\necho\n") << false;
        QTest::newRow("unknown") << write(QStringLiteral("data.bin"), QByteArray(64, '\x07')) << false;
    }
    void opens()
    {
        QFETCH(QString, path);
        QFETCH(bool, opens);
        QVERIFY(!path.isEmpty());
        QCOMPARE(OpenCheck::mayOpen(path), opens);
    }

    void linksAndFoldersDoNot()
    {
        const QString target = picture(QStringLiteral("real.jpg"), "JPEG");
        const QString link = m_dir.filePath(QStringLiteral("link.jpg"));
        QVERIFY(QFile::link(target, link));
        QVERIFY(OpenCheck::mayOpen(target));
        QVERIFY(!OpenCheck::mayOpen(link));
        QVERIFY(!OpenCheck::mayOpen(m_dir.path()));
        QVERIFY(!OpenCheck::mayOpen(m_dir.filePath(QStringLiteral("missing.jpg"))));
    }
};

QTEST_GUILESS_MAIN(TestOpenCheck)
#include "tst_opencheck.moc"
