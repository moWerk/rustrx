// SPDX-License-Identifier: GPL-3.0-or-later
// A received "voice message" that is an HLS or DASH playlist must not make
// GStreamer fetch its addresses (GstGuard.h, audit 0.8.4 UC4): played
// through playbin against a listener on 127.0.0.1, once as GStreamer comes
// (it fetches: the test means something) and once with the guard.
#include "GstGuard.h"

#include <QFile>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class TestGstGuard : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QTcpServer m_server;
    int m_connections = 0;

    // an HLS playlist under a voice message's name, its segment on the listener
    QString hls()
    {
        const QString path = m_dir.path() + QStringLiteral("/voice.ogg");
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXTINF:10.0,\nhttp://127.0.0.1:"
                    + QByteArray::number(m_server.serverPort()) + "/seg0.ts\n#EXT-X-ENDLIST\n");
        }
        return path;
    }
    QString dash()
    {
        const QString path = m_dir.path() + QStringLiteral("/clip.mp4");
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            const QByteArray base = "http://127.0.0.1:" + QByteArray::number(m_server.serverPort()) + "/";
            f.write("<?xml version=\"1.0\"?>\n<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" "
                    "mediaPresentationDuration=\"PT10S\" minBufferTime=\"PT1S\" profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\">"
                    "<BaseURL>" + base + "</BaseURL><Period><AdaptationSet mimeType=\"audio/mp4\" codecs=\"mp4a.40.2\">"
                    "<Representation id=\"a\" bandwidth=\"64000\"><BaseURL>a.mp4</BaseURL><SegmentBase indexRange=\"0-100\"/>"
                    "</Representation></AdaptationSet></Period></MPD>\n");
        }
        return path;
    }

    // played until it ends, fails or 4 s are over; true when the listener was asked
    bool fetches(const QString &path)
    {
        m_connections = 0;
        GstElement *play = gst_element_factory_make("playbin", nullptr);
        if (!play)
            return false;
        g_object_set(play, "uri", QUrl::fromLocalFile(path).toEncoded().constData(),
                     "audio-sink", gst_element_factory_make("fakesink", nullptr),
                     "video-sink", gst_element_factory_make("fakesink", nullptr), nullptr);
        gst_element_set_state(play, GST_STATE_PLAYING);
        GstBus *bus = gst_element_get_bus(play);
        QElapsedTimer t;
        t.start();
        bool over = false;
        while (!over && t.elapsed() < 4000 && !m_connections) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (GstMessage *m = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
                over = true;
                gst_message_unref(m);
            }
        }
        // a fetch on its way when the error came
        for (int i = 0; i < 20 && !m_connections; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        gst_element_set_state(play, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(play);
        return m_connections > 0;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                ++m_connections;
                s->close();
                s->deleteLater();
            }
        });
        gst_init(nullptr, nullptr);
    }

    void environment()
    {
        GstGuard::setEnvironment();
        const QByteArray ranks = qgetenv("GST_PLUGIN_FEATURE_RANK");
        for (const char *name : GstGuard::kPlaylistDemuxers)
            QVERIFY(ranks.contains(QByteArray(name) + ":NONE"));
        // what was there stays
        qputenv("GST_PLUGIN_FEATURE_RANK", "foo:PRIMARY");
        GstGuard::setEnvironment();
        QVERIFY(qgetenv("GST_PLUGIN_FEATURE_RANK").startsWith("foo:PRIMARY,hlsdemux:NONE"));
        qunsetenv("GST_PLUGIN_FEATURE_RANK");
    }

    // a new GStreamer process with the variable only (as QtMultimedia's
    // start in the app, after main() set it)
    void environmentInAFreshProcess()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("gst-launch-1.0")).isEmpty())
            QSKIP("no gst-launch-1.0");
        if (!gst_registry_check_feature_version(gst_registry_get(), "hlsdemux", 1, 0, 0)
                && !gst_registry_check_feature_version(gst_registry_get(), "hlsdemux2", 1, 0, 0))
            QSKIP("no HLS demuxer on this host: nothing to guard against");
        const QString path = hls();
        auto run = [&](bool guarded) {
            m_connections = 0;
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.remove(QStringLiteral("GST_PLUGIN_FEATURE_RANK"));
            if (guarded) {
                GstGuard::setEnvironment();
                env.insert(QStringLiteral("GST_PLUGIN_FEATURE_RANK"), QString::fromLatin1(qgetenv("GST_PLUGIN_FEATURE_RANK")));
                qunsetenv("GST_PLUGIN_FEATURE_RANK");
            }
            p.setProcessEnvironment(env);
            p.start(QStringLiteral("gst-launch-1.0"), { QStringLiteral("-q"), QStringLiteral("playbin"),
                    QStringLiteral("uri=") + QUrl::fromLocalFile(path).toString(),
                    QStringLiteral("audio-sink=fakesink"), QStringLiteral("video-sink=fakesink") });
            QElapsedTimer t;
            t.start();
            while (p.state() != QProcess::NotRunning && t.elapsed() < 4000 && !m_connections)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            for (int i = 0; i < 20 && !m_connections; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            p.kill();
            p.waitForFinished(2000);
            return m_connections > 0;
        };
        QVERIFY2(run(false), "without the guard, the playlist is fetched");
        QVERIFY(!run(true));
    }

    void registry()
    {
        if (!gst_registry_check_feature_version(gst_registry_get(), "hlsdemux", 1, 0, 0)
                && !gst_registry_check_feature_version(gst_registry_get(), "hlsdemux2", 1, 0, 0))
            QSKIP("no HLS demuxer on this host: nothing to guard against");
        // as GStreamer comes: the playlists are followed
        QVERIFY2(fetches(hls()), "without the guard, the HLS playlist is fetched");
        const bool dashFetched = fetches(dash());
        QVERIFY(GstGuard::demote() > 0);
        for (const char *name : GstGuard::kPlaylistDemuxers) {
            if (GstPluginFeature *f = gst_registry_lookup_feature(gst_registry_get(), name)) {
                QCOMPARE(gst_plugin_feature_get_rank(f), guint(GST_RANK_NONE));
                gst_object_unref(f);
            }
        }
        QVERIFY(!fetches(hls()));
        if (dashFetched)
            QVERIFY(!fetches(dash()));
    }
};

QTEST_GUILESS_MAIN(TestGstGuard)
#include "tst_gstguard.moc"
