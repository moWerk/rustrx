// SPDX-License-Identifier: GPL-3.0-or-later
// TimelineModel::applyTimeline: the core sends the order of all ids and
// only the new or changed items. The model has to end up with the same
// rows as from the whole list, tell the view only what changed, in as few
// steps as it can, and ask for everything again when it lost track.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "TimelineModel.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

QVariantMap msg(const QString &id, const QString &body = QString())
{
    return { { "id", id }, { "kind", "event" }, { "event_id", "$" + id }, { "body", body.isEmpty() ? id : body } };
}

// an event: order oldest first, the items given
QVariantMap ev(qint64 stream, qint64 seq, bool full, const QStringList &order, const QVariantList &items)
{
    QVariantMap byId;
    for (const QVariant &v : items)
        byId.insert(v.toMap().value("id").toString(), v);
    QVariantList ids;
    for (const QString &id : order)
        ids << id;
    return { { "stream", stream }, { "seq", seq }, { "full", full }, { "order", ids }, { "items", byId } };
}

// the rows' ids, row 0 (the newest) first
QStringList rows(const TimelineModel &m)
{
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i), m.roleNames().key("id")).toString();
    return out;
}

QString body(const TimelineModel &m, int row)
{
    return m.data(m.index(row), m.roleNames().key("body")).toString();
}

// what the view was told
struct Told
{
    QSignalSpy inserted, removed, moved, changed;
    explicit Told(TimelineModel *m)
        : inserted(m, &QAbstractItemModel::rowsInserted), removed(m, &QAbstractItemModel::rowsRemoved),
          moved(m, &QAbstractItemModel::rowsMoved), changed(m, &QAbstractItemModel::dataChanged) {}
    void clear() { inserted.clear(); removed.clear(); moved.clear(); changed.clear(); }
};

}

class TestTimelineApply : public QObject
{
    Q_OBJECT

private slots:
    // the first event fills the model; a new message is one insertion at
    // row 0 and touches no other row
    void fullThenNewMessage()
    {
        TimelineModel m;
        Told told(&m);
        QVERIFY(!m.applyTimeline(ev(1, 1, true, { "a", "b", "c" }, { msg("a"), msg("b"), msg("c") })));
        QCOMPARE(rows(m), QStringList({ "c", "b", "a" }));
        told.clear();
        QVERIFY(!m.applyTimeline(ev(1, 2, false, { "a", "b", "c", "d" }, { msg("d") })));
        QCOMPARE(rows(m), QStringList({ "d", "c", "b", "a" }));
        QCOMPARE(told.inserted.count(), 1);
        QCOMPARE(told.inserted.at(0).at(1).toInt(), 0);
        QCOMPARE(told.inserted.at(0).at(2).toInt(), 0);
        QCOMPARE(told.changed.count(), 0);
    }

    // a page of older messages is one insertion at the far end.
    // Planted-bug: insert row by row (two rowsInserted).
    void olderPageIsOneInsertion()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b" }, { msg("a"), msg("b") }));
        Told told(&m);
        QVERIFY(!m.applyTimeline(ev(1, 2, false, { "x", "y", "a", "b" }, { msg("x"), msg("y") })));
        QCOMPARE(rows(m), QStringList({ "b", "a", "y", "x" }));
        QCOMPARE(told.inserted.count(), 1);
        QCOMPARE(told.inserted.at(0).at(1).toInt(), 2);
        QCOMPARE(told.inserted.at(0).at(2).toInt(), 3);
    }

    // only the changed row is told, with its roles; the rows the event
    // does not carry are kept as they were.
    // Planted-bug: take the rows not carried from nowhere (an empty map).
    void changedRowOnly()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b", "c" }, { msg("a"), msg("b"), msg("c") }));
        Told told(&m);
        QVERIFY(!m.applyTimeline(ev(1, 2, false, { "a", "b", "c" }, { msg("b", "edited") })));
        QCOMPARE(rows(m), QStringList({ "c", "b", "a" }));
        QCOMPARE(body(m, 1), QStringLiteral("edited"));
        QCOMPARE(body(m, 0), QStringLiteral("c"));
        QCOMPARE(body(m, 2), QStringLiteral("a"));
        QCOMPARE(told.changed.count(), 1);
        QCOMPARE(told.changed.at(0).at(0).toModelIndex().row(), 1);
        const auto roles = told.changed.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(m.roleNames().key("body")));
        QVERIFY(!roles.contains(m.roleNames().key("sender")));
    }

    // own_read moving tells that role only, not every role (an empty list).
    // Planted-bug: drop "own_read" from the role names.
    void ownReadHasItsRole()
    {
        TimelineModel m;
        QVariantMap a = msg("a"), b = msg("b");
        a.insert("own_read", true);
        m.applyTimeline(ev(1, 1, true, { "a", "b" }, { a, b }));
        QCOMPARE(m.rowOwnRead(), 1);
        Told told(&m);
        b.insert("own_read", true);
        m.applyTimeline(ev(1, 2, false, { "a", "b" }, { msg("a"), b }));
        QCOMPARE(m.rowOwnRead(), 0);
        QCOMPARE(told.changed.count(), 2);
        const int role = m.roleNames().key("own_read");
        for (const QList<QVariant> &args : told.changed)
            QCOMPARE(args.at(2).value<QVector<int>>(), QVector<int>({ role }));
    }

    // neighbouring rows that go are one removal; a moved row moves
    void removalsAndMoves()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b", "c", "d", "e" },
                           { msg("a"), msg("b"), msg("c"), msg("d"), msg("e") }));
        Told told(&m);
        QVERIFY(!m.applyTimeline(ev(1, 2, false, { "a", "e" }, {})));
        QCOMPARE(rows(m), QStringList({ "e", "a" }));
        QCOMPARE(told.removed.count(), 1);
        QCOMPARE(told.changed.count(), 0);
        told.clear();
        QVERIFY(!m.applyTimeline(ev(1, 3, false, { "e", "a" }, {})));
        QCOMPARE(rows(m), QStringList({ "a", "e" }));
        QCOMPARE(told.moved.count(), 1);
        QCOMPARE(told.inserted.count() + told.removed.count() + told.changed.count(), 0);
    }

    // a removed item that comes back has to come with the event: the
    // model does not keep what the core no longer has.
    // Planted-bug: keep every item ever sent (the store never shrinks).
    void removedItemsAreForgotten()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b" }, { msg("a"), msg("b") }));
        m.applyTimeline(ev(1, 2, false, { "a" }, {}));
        QVERIFY(m.applyTimeline(ev(1, 3, false, { "a", "b" }, {})));
        QCOMPARE(rows(m), QStringList({ "a" }));
    }

    // a skipped event or an unknown id: nothing applied, everything asked
    // for once; a full one puts it right.
    // Planted-bug: apply a delta whose seq skips one.
    void lostTrackAsksOnce()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a" }, { msg("a") }));
        QVERIFY(m.applyTimeline(ev(1, 3, false, { "a", "c" }, { msg("c") })));
        QCOMPARE(rows(m), QStringList({ "a" }));
        QVERIFY(!m.applyTimeline(ev(1, 4, false, { "a", "d" }, { msg("d") })));
        QCOMPARE(rows(m), QStringList({ "a" }));
        QVERIFY(!m.applyTimeline(ev(1, 5, true, { "a", "c", "d" }, { msg("a"), msg("c"), msg("d") })));
        QCOMPARE(rows(m), QStringList({ "d", "c", "a" }));
        QVERIFY(m.applyTimeline(ev(1, 6, false, { "a", "zz" }, {})));
        QCOMPARE(rows(m), QStringList({ "d", "c", "a" }));
    }

    // an earlier stream's events (an earlier open of the room, late) are
    // dropped; a later stream's delta before its full one asks for it.
    // Planted-bug: take every full event (the late old one wins).
    void streams()
    {
        TimelineModel m;
        m.applyTimeline(ev(5, 1, true, { "a" }, { msg("a", "new") }));
        QVERIFY(!m.applyTimeline(ev(3, 1, true, { "a", "b" }, { msg("a", "old"), msg("b") })));
        QVERIFY(!m.applyTimeline(ev(3, 2, false, { "a", "b", "c" }, { msg("c") })));
        QCOMPARE(rows(m), QStringList({ "a" }));
        QCOMPARE(body(m, 0), QStringLiteral("new"));
        QVERIFY(m.applyTimeline(ev(6, 2, false, { "a" }, {})));
    }

    // a new stream reuses ids for other items: its full event compares
    // every row. Planted-bug: on a full event, keep a row as it is when
    // its id matches.
    void newStreamComparesEverything()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "1", "2" }, { msg("1", "first"), msg("2", "second") }));
        Told told(&m);
        m.applyTimeline(ev(2, 1, true, { "1", "2" }, { msg("1", "other"), msg("2", "second") }));
        QCOMPARE(body(m, 1), QStringLiteral("other"));
        QCOMPARE(told.changed.count(), 1);
    }

    // after clear (another room) a delta waits for its stream's full one
    void clearWaitsForFull()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a" }, { msg("a") }));
        m.clear();
        QVERIFY(!m.applyTimeline(ev(1, 2, false, { "a", "b" }, { msg("b") })));
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(!m.applyTimeline(ev(2, 1, true, { "x" }, { msg("x") })));
        QCOMPARE(rows(m), QStringList({ "x" }));
    }

    // the event rows follow the rows: a new message moves every row down.
    // Planted-bug: make the index only once (rowOfEvent answers the old row).
    void eventRowsFollow()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b" }, { msg("a"), msg("b") }));
        QCOMPARE(m.rowOfEvent("$a"), 1);
        m.applyTimeline(ev(1, 2, false, { "a", "b", "c" }, { msg("c") }));
        QCOMPARE(m.rowOfEvent("$a"), 2);
        QCOMPARE(m.rowOfEvent("$c"), 0);
        QCOMPARE(m.rowOfEvent("$gone"), -1);
        Told told(&m);
        m.setMediaPath("$b", true, "/tmp/b.jpg");
        QCOMPARE(told.changed.count(), 1);
        QCOMPARE(told.changed.at(0).at(0).toModelIndex().row(), 1);
        m.clear();
        QCOMPARE(m.rowOfEvent("$a"), -1);
    }

    // a view asking for a row while the rows change (in a rowsInserted
    // handler) does not leave the index behind for the later steps.
    // Planted-bug: mark the index dirty only once, at the start of apply.
    void eventRowsFollowWhileAsked()
    {
        TimelineModel m;
        m.applyTimeline(ev(1, 1, true, { "a", "b" }, { msg("a"), msg("b") }));
        connect(&m, &QAbstractItemModel::rowsInserted, &m, [&m]() { m.rowOfEvent("$a"); });
        m.applyTimeline(ev(1, 2, false, { "x", "a", "b", "c" }, { msg("x"), msg("c") }));
        QCOMPARE(rows(m), QStringList({ "c", "b", "a", "x" }));
        QCOMPARE(m.rowOfEvent("$a"), 2);
        QCOMPARE(m.rowOfEvent("$x"), 3);
    }

    // the deltas end where the whole list would have
    void sameAsWholeList()
    {
        TimelineModel byDelta, whole;
        byDelta.applyTimeline(ev(1, 1, true, { "a", "b", "c" }, { msg("a"), msg("b"), msg("c") }));
        byDelta.applyTimeline(ev(1, 2, false, { "x", "a", "c", "b", "d" }, { msg("x"), msg("c", "c2"), msg("d") }));
        whole.setItems({ msg("x"), msg("a"), msg("c", "c2"), msg("b"), msg("d") });
        QCOMPARE(rows(byDelta), rows(whole));
        for (int i = 0; i < whole.rowCount(); ++i)
            QCOMPARE(body(byDelta, i), body(whole, i));
    }
};

QTEST_GUILESS_MAIN(TestTimelineApply)
#include "tst_timelineapply.moc"
