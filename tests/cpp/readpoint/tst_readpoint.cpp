// SPDX-License-Identifier: GPL-3.0-or-later
// TimelineModel::seenEventId: the message a read receipt goes to when a
// row was on screen. The room page sends receipts by it, so a wrong
// answer marks unseen messages read or moves nothing.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "timeline.h"

#include <QtTest>

namespace {

QVariantMap message(const char *id) { return { { "id", id }, { "kind", "event" }, { "event_id", id } }; }
QVariantMap echo(const char *txn) { return { { "id", txn }, { "kind", "event" }, { "txn_id", txn } }; }
QVariantMap virt(const char *id, const char *kind) { return { { "id", id }, { "kind", kind } }; }

}

// rows as the core sends them, oldest first; the model holds them newest
// first, row 0 the newest
class TestReadPoint : public QObject
{
    Q_OBJECT

private slots:
    // a message row is its own read point
    void ownRow()
    {
        Timeline t;
        t.model.setItems({ message("$1"), message("$2"), message("$3") });
        QCOMPARE(t.model.seenEventId(0), QStringLiteral("$3"));
        QCOMPARE(t.model.seenEventId(1), QStringLiteral("$2"));
    }

    // a date line, the unread line and a message still being sent carry no
    // event: the next older message counts. Planted-bug: drop the kind check.
    void skipsVirtualRows()
    {
        Timeline t;
        QVariantMap dated = virt("d", "date");
        dated.insert("event_id", "$d");   // a stray id must not count
        t.model.setItems({ message("$1"), message("$2"), dated, virt("m", "readmarker"), echo("t1") });
        QCOMPARE(t.model.seenEventId(0), QStringLiteral("$2"));
        QCOMPARE(t.model.seenEventId(1), QStringLiteral("$2"));
        QCOMPARE(t.model.seenEventId(2), QStringLiteral("$2"));
    }

    // a local echo has no event id yet. Planted-bug: drop the event_id check.
    void skipsLocalEchoes()
    {
        Timeline t;
        t.model.setItems({ message("$1"), echo("t1"), echo("t2") });
        QCOMPARE(t.model.seenEventId(0), QStringLiteral("$1"));
    }

    // nothing older: no receipt at all, never a newer message
    void noneOlder()
    {
        Timeline t;
        t.model.setItems({ virt("s", "start"), message("$2") });
        QCOMPARE(t.model.seenEventId(1), QString());
        QCOMPARE(t.model.seenEventId(5), QString());
        QCOMPARE(t.model.seenEventId(-1), QStringLiteral("$2"));
    }

    // where the room opens when its unread line is not loaded: the newest
    // message with our own receipt (an older receipt left on a message
    // does not count). Planted-bug: search from the oldest row.
    void ownReceiptRow()
    {
        Timeline t;
        QVariantMap old = message("$1"), read = message("$3");
        old.insert("own_read", true);
        read.insert("own_read", true);
        t.model.setItems({ old, message("$2"), read, message("$4"), message("$5") });
        QCOMPARE(t.model.rowOwnRead(), 2);
        t.model.setItems({ message("$1"), message("$2") });
        QCOMPARE(t.model.rowOwnRead(), -1);
    }
};

QTEST_GUILESS_MAIN(TestReadPoint)
#include "tst_readpoint.moc"
