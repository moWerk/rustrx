// SPDX-License-Identifier: GPL-3.0-or-later
// src/DevHook.h: the dev D-Bus hook takes the requests the test tooling
// sends (tools/test/*.sh, the call tests' vd.sh) and refuses everything
// else (audit S4). Requests go through JSON as they do in Rustrx::dev.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include "DevHook.h"

class TestDevHook : public QObject
{
    Q_OBJECT

    static QString refusal(const char *json)
    {
        return DevHook::refusal(QJsonDocument::fromJson(QByteArray(json)).object().toVariantMap());
    }

private slots:
    // Planted-bug: "voip" missing from the kinds (every call test
    // refused). Planted-bug: "tab" missing from ui's keys.
    void theToolingsRequestsPass_data()
    {
        QTest::addColumn<QString>("json");
        for (const char *j : {
                 "{\"quit\":true}",
                 "{\"state\":true}",
                 "{\"rooms\":true}",
                 "{\"ui\":\"activate\"}",
                 "{\"ui\":\"open_room\",\"room_id\":\"!r:example.org\"}",
                 "{\"ui\":\"settings\",\"tab\":\"account\"}",
                 "{\"ui\":\"back\"}",
                 "{\"cmd\":\"call_start\",\"room_id\":\"!r:example.org\",\"skip_lobby\":true}",
                 "{\"cmd\":\"refresh_rooms\"}",
                 "{\"voip\":\"place\",\"room_id\":\"!r:example.org\",\"invitee\":\"@b:example.org\",\"tone\":true}",
                 "{\"voip\":\"answer\",\"tone\":true,\"aec\":false}",
                 "{\"voip\":\"hangup\"}",
                 "{\"voip\":\"status\",\"speaker\":true}",
             })
            QTest::newRow(j) << QString::fromLatin1(j);
    }
    void theToolingsRequestsPass()
    {
        QFETCH(QString, json);
        QCOMPARE(refusal(json.toLatin1().constData()), QString());
    }

    // Planted-bug: any "cmd" passes (the old refused list missed
    // send_file and get_media). Planted-bug: the keys of a request are
    // not checked (a call_start with a path rides along). Two kinds in
    // one request (dev() would take the first branch) are refused twice:
    // by the kind count and by the key check.
    void everythingElseIsRefused_data()
    {
        QTest::addColumn<QString>("json");
        for (const char *j : {
                 "{}",
                 "{\"cmd\":\"export_keys\",\"path\":\"/tmp/k\"}",
                 "{\"cmd\":\"logout\"}",
                 "{\"cmd\":\"send_file\",\"room_id\":\"!r:example.org\",\"path\":\"/home/x/.config/session.json\"}",
                 "{\"cmd\":\"get_media\",\"dest_dir\":\"/tmp\"}",
                 "{\"cmd\":\"push_set\"}",
                 "{\"cmd\":\"\"}",
                 "{\"cmd\":\"refresh_rooms\",\"path\":\"/tmp/x\"}",
                 "{\"cmd\":\"call_start\",\"room_id\":\"!r:example.org\",\"path\":\"/tmp/x\"}",
                 "{\"ui\":\"logout\"}",
                 "{\"ui\":\"open_room\",\"url\":\"https://example.org\"}",
                 "{\"voip\":\"transfer\"}",
                 "{\"voip\":\"place\",\"room_id\":\"!r:example.org\",\"lifetime\":1}",
                 "{\"quit\":true,\"cmd\":\"logout\"}",
                 "{\"state\":true,\"rooms\":true}",
                 "{\"rooms\":true,\"room_id\":\"!r:example.org\"}",
                 "{\"secret\":true}",
             })
            QTest::newRow(j) << QString::fromLatin1(j);
    }
    void everythingElseIsRefused()
    {
        QFETCH(QString, json);
        QVERIFY(!refusal(json.toLatin1().constData()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDevHook)
#include "tst_devhook.moc"
