// SPDX-License-Identifier: GPL-3.0-or-later
// TimelineModel::clearMediaPaths: after "Clear media" the rows' picture
// paths point to deleted files. The model forgets them and says which
// rows to ask again, so pictures on screen come back without a restart.
//
// Each test names the bug it exists to catch ("Planted-bug:").

#include "timeline.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

QVariantMap message(const char *id) { return { { "id", id }, { "kind", "event" }, { "event_id", id } }; }

}

class TestMediaPaths : public QObject
{
    Q_OBJECT

private slots:
    // the paths are gone from the rows, and the rows that had one are
    // listed with the kind they had. Planted-bug: keep m_thumbs.
    void forgetsAndLists()
    {
        Timeline t;
        t.model.setItems({ message("$1"), message("$2"), message("$3") });
        t.model.setMediaPath("$1", true, "/m/1-thumb.jpg");
        t.model.setMediaPath("$2", false, "/m/2.png");
        // a picture of a row no longer loaded is not asked for
        t.model.setMediaPath("$gone", true, "/m/gone-thumb.jpg");
        QSignalSpy changed(&t.model, &QAbstractItemModel::dataChanged);
        const auto shown = t.model.clearMediaPaths();
        QCOMPARE(shown.size(), 2);
        QVERIFY(shown.contains(qMakePair(QStringLiteral("$1"), true)));
        QVERIFY(shown.contains(qMakePair(QStringLiteral("$2"), false)));
        QVERIFY(!changed.isEmpty());
        const int thumbRole = t.role("thumb_path"), mediaRole = t.role("media_path");
        for (int i = 0; i < t.model.rowCount(); ++i) {
            QVERIFY(t.model.data(t.model.index(i), thumbRole).toString().isEmpty());
            QVERIFY(t.model.data(t.model.index(i), mediaRole).toString().isEmpty());
        }
        QVERIFY(t.model.mediaPath("$2").isEmpty());
        // a second clear has nothing left to ask
        QVERIFY(t.model.clearMediaPaths().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMediaPaths)
#include "tst_mediapaths.moc"
