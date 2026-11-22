// SPDX-License-Identifier: GPL-3.0-or-later
// RoomModel's updates as a view sees them: the signals replayed on a copy
// of the keys must give the new order, in as few steps as the change
// needs; and RoomLists' sharing of unchanged rows.
#include "RoomLists.h"
#include "RoomModel.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QVariantMap room(const QString &id, const QVariantMap &more = QVariantMap())
{
    QVariantMap r { { "room_id", id }, { "name", id }, { "state", "joined" }, { "preview", "hi" },
                    { "latest_event_id", "$1" } };
    for (auto it = more.constBegin(); it != more.constEnd(); ++it)
        r.insert(it.key(), it.value());
    return r;
}

QVariantList rooms(const QStringList &ids)
{
    QVariantList out;
    for (const QString &id : ids)
        out << room(id);
    return out;
}

QStringList keysOf(const RoomModel &m)
{
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.get(i).value("room_id").toString();
    return out;
}

// what a view would hold: the model's signals applied to a list of keys
struct Mirror
{
    explicit Mirror(RoomModel &model)
        : m(model)
    {
        QObject::connect(&m, &QAbstractItemModel::rowsInserted, [this](const QModelIndex &, int first, int last) {
            ++inserts;
            for (int i = first; i <= last; ++i)
                keys.insert(i, m.get(i).value("room_id").toString());
        });
        QObject::connect(&m, &QAbstractItemModel::rowsRemoved, [this](const QModelIndex &, int first, int last) {
            ++removes;
            for (int i = last; i >= first; --i)
                keys.removeAt(i);
        });
        QObject::connect(&m, &QAbstractItemModel::rowsMoved,
                         [this](const QModelIndex &, int first, int last, const QModelIndex &, int dest) {
            ++moves;
            const QStringList block = keys.mid(first, last - first + 1);
            for (int i = 0; i < block.size(); ++i)
                keys.removeAt(first);
            const int to = dest > last ? dest - block.size() : dest;
            for (int i = 0; i < block.size(); ++i)
                keys.insert(to + i, block.at(i));
        });
        QObject::connect(&m, &QAbstractItemModel::dataChanged,
                         [this](const QModelIndex &, const QModelIndex &, const QVector<int> &r) { changes << r; });
    }
    void reset() { inserts = removes = moves = 0; changes.clear(); }
    RoomModel &m;
    QStringList keys;
    int inserts = 0, removes = 0, moves = 0;
    QList<QVector<int>> changes;
};

// a core `rooms` event: the order, and the rows sent
QVariantMap roomsEvent(qint64 stream, qint64 seq, bool full, const QStringList &order, const QVariantList &rows)
{
    QVariantMap items;
    for (const QVariant &r : rows)
        items.insert(r.toMap().value("room_id").toString(), r);
    QVariantList ids;
    for (const QString &id : order)
        ids << id;
    return { { "stream", stream }, { "seq", seq }, { "full", full }, { "order", ids }, { "items", items } };
}

int role(const RoomModel &m, const char *name)
{
    return m.roleNames().key(QByteArray(name));
}

} // namespace

class TestRoomModel : public QObject
{
    Q_OBJECT

private slots:
    // the first fill is one insert (it was one per row, each running the
    // views' handlers)
    void firstFillInOneStep()
    {
        RoomModel m;
        Mirror v(m);
        m.setRooms(rooms({ "a", "b", "c", "d" }));
        QCOMPARE(v.inserts, 1);
        QCOMPARE(v.keys, QStringList({ "a", "b", "c", "d" }));
    }

    // a room that moved is one move, not one per displaced neighbour
    void oneRoomUpIsOneMove()
    {
        RoomModel m;
        Mirror v(m);
        QStringList ids;
        for (int i = 0; i < 50; ++i)
            ids << QString::number(i);
        m.setRooms(rooms(ids));
        v.reset();
        ids.move(37, 0);
        m.setRooms(rooms(ids));
        QCOMPARE(v.moves, 1);
        QCOMPARE(v.keys, ids);
        // and back down: one move as well
        v.reset();
        ids.move(0, 45);
        m.setRooms(rooms(ids));
        QCOMPARE(v.moves, 1);
        QCOMPARE(v.keys, ids);
        // neighbours swapped in pairs: half of them stay, the others move
        v.reset();
        QStringList pairs;
        for (int i = 0; i < 10; i += 2)
            pairs << ids.at(i + 1) << ids.at(i);
        pairs << ids.mid(10);
        m.setRooms(rooms(pairs));
        QCOMPARE(v.moves, 5);
        QCOMPARE(v.keys, pairs);
    }

    // A new order takes at most one move per row outside a longest run
    // already in order. Planted-bug: no longest run kept (every row may move).
    void reorderMovesOnlyTheRest()
    {
        QRandomGenerator rng(17);
        for (int round = 0; round < 300; ++round) {
            const int n = 2 + int(rng.bounded(14));
            QStringList ids;
            for (int i = 0; i < n; ++i)
                ids << QString::number(i);
            QStringList shuffled = ids;
            for (int i = n - 1; i > 0; --i)
                shuffled.swapItemsAt(i, int(rng.bounded(i + 1)));
            // the longest increasing run of old positions, by the book
            QVector<int> best(n, 1);
            int longest = 0;
            for (int i = 0; i < n; ++i) {
                for (int k = 0; k < i; ++k)
                    if (shuffled.at(k).toInt() < shuffled.at(i).toInt())
                        best[i] = qMax(best[i], best[k] + 1);
                longest = qMax(longest, best[i]);
            }
            RoomModel m;
            Mirror v(m);
            m.setRooms(rooms(ids));
            v.reset();
            m.setRooms(rooms(shuffled));
            QCOMPARE(v.keys, shuffled);
            QVERIFY2(v.moves <= n - longest, qPrintable(shuffled.join(',')));
        }
    }

    // Planted-bug: the runs of removed or new rows go one row at a time.
    void filterInRuns()
    {
        RoomModel m;
        Mirror v(m);
        QStringList all, some;
        for (int i = 0; i < 40; ++i) {
            all << QString::number(i);
            if (i % 10 < 3)
                some << QString::number(i);
        }
        m.setRooms(rooms(all));
        v.reset();
        m.setRooms(rooms(some));
        QCOMPARE(v.removes, 4);
        QCOMPARE(v.keys, some);
        v.reset();
        m.setRooms(rooms(all));
        QCOMPARE(v.inserts, 4);
        QCOMPARE(v.keys, all);
    }

    // Planted-bug: a block of neighbours that moved is checked against the
    // wrong position (the replayed keys come out in another order).
    void randomChanges()
    {
        QRandomGenerator rng(4711);
        for (int round = 0; round < 400; ++round) {
            RoomModel m;
            Mirror v(m);
            QStringList now;
            const int n = int(rng.bounded(30));
            for (int i = 0; i < n; ++i)
                now << QStringLiteral("r%1").arg(i);
            m.setRooms(rooms(now));
            int next = n;
            for (int step = 0; step < 5; ++step) {
                QStringList want = now;
                // some go, some come, some move
                for (int k = int(rng.bounded(4)); k > 0 && !want.isEmpty(); --k)
                    want.removeAt(int(rng.bounded(want.size())));
                for (int k = int(rng.bounded(5)); k > 0; --k)
                    want.insert(int(rng.bounded(want.size() + 1)), QStringLiteral("r%1").arg(next++));
                for (int k = int(rng.bounded(4)); k > 0 && want.size() > 1; --k)
                    want.move(int(rng.bounded(want.size())), int(rng.bounded(want.size())));
                if (rng.bounded(5) == 0)
                    std::reverse(want.begin(), want.end());
                m.setRooms(rooms(want));
                QCOMPARE(keysOf(m), want);
                QCOMPARE(v.keys, want);
                now = want;
            }
        }
    }

    // Planted-bug: latest_event_id loses its place among the silent keys
    // (every new message rebinds all roles of its row).
    void silentKeysTellNothing()
    {
        RoomModel m;
        Mirror v(m);
        m.setRooms({ room("a") });
        v.reset();
        m.setRooms({ room("a", { { "latest_event_id", "$2" }, { "dm_user", "@x:y" } }) });
        QCOMPARE(v.changes.size(), 0);
        // the fresh values are there for get()
        QCOMPARE(m.get(0).value("latest_event_id").toString(), QStringLiteral("$2"));
        m.setRooms({ room("a", { { "latest_event_id", "$3" }, { "preview", "new" } }) });
        QCOMPARE(v.changes.size(), 1);
        QVERIFY(!v.changes.at(0).isEmpty());
        QVERIFY(v.changes.at(0).contains(role(m, "preview")));
        QVERIFY(v.changes.at(0).contains(role(m, "preview_html")));
        QCOMPARE(m.data(m.index(0), role(m, "preview_html")).toString(), QStringLiteral("new"));
        // a key without a role: every role, as before
        m.setRooms({ room("a", { { "latest_event_id", "$3" }, { "preview", "new" }, { "something", 1 } }) });
        QCOMPARE(v.changes.size(), 2);
        QVERIFY(v.changes.at(1).isEmpty());
    }

    // Planted-bug: section is not computed (the invitations lose their heading).
    void sectionFromState()
    {
        RoomModel m;
        Mirror v(m);
        m.setRooms({ room("a", { { "state", "invited" } }), room("b") });
        const int section = role(m, "section");
        QVERIFY(!m.data(m.index(0), section).toString().isEmpty());
        QVERIFY(m.data(m.index(1), section).toString().isEmpty());
        v.reset();
        m.setRooms({ room("a"), room("b") });
        QCOMPARE(v.changes.size(), 1);
        QVERIFY(v.changes.at(0).contains(section));
        QVERIFY(m.data(m.index(0), section).toString().isEmpty());
    }

    // Planted-bug: an unwatched model keeps its old rows.
    void unwatchedTakesRowsSilently()
    {
        RoomModel m;
        m.setRooms(rooms({ "a", "b" }));
        m.setRooms({ room("b", { { "name", "B" } }), room("c") });
        QCOMPARE(keysOf(m), QStringList({ "b", "c" }));
        QCOMPARE(m.data(m.index(0), role(m, "name_html")).toString(), QStringLiteral("B"));
        // a view that comes later sees the rows, then its updates
        Mirror v(m);
        v.keys = keysOf(m);
        QVERIFY(m.watched());
        m.setRooms(rooms({ "c", "b" }));
        QCOMPARE(v.moves, 1);
        QCOMPARE(v.keys, QStringList({ "c", "b" }));
    }

    // QML's handlers and bindings count as followers (QObject::receivers).
    // Planted-bug: watched() checks only C++ connections.
    void qmlFollowersCount()
    {
        QQmlEngine engine;
        RoomModel bound, connected, alone;
        engine.rootContext()->setContextProperty("bound", &bound);
        engine.rootContext()->setContextProperty("connected", &connected);
        QQmlComponent c(&engine);
        c.setData("import QtQml 2.2\n"
                  "QtObject {\n"
                  "  property int n: bound.count\n"
                  "  property var c: Connections { target: connected; onDataChanged: {} }\n"
                  "}\n", QUrl());
        QScopedPointer<QObject> o(c.create());
        QVERIFY2(o, qPrintable(c.errorString()));
        QVERIFY(bound.watched());
        QVERIFY(connected.watched());
        QVERIFY(!alone.watched());
    }

    // one name size for every list
    // Planted-bug: the size is the model's own again.
    void nameSizeShared()
    {
        RoomModel a, b;
        a.setNameEmojiSize(55);
        QCOMPARE(b.nameEmojiSize(), 55);
        a.setNameEmojiSize(42);
    }

    // Planted-bug: RoomLists passes the fresh row on although it equals the last.
    void unchangedRowsShared()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        lists.setRooms(rooms({ "a", "b" }));
        const QVariantList first = lists.lastRooms();
        lists.setRooms({ room("a"), room("b", { { "preview", "x" } }) });
        const QVariantList second = lists.lastRooms();
        // the same map underneath: a pointer compare
        QVERIFY(first.at(0).toMap().isSharedWith(second.at(0).toMap()));
        QVERIFY(!first.at(1).toMap().isSharedWith(second.at(1).toMap()));
        QCOMPARE(second.at(1).toMap().value("preview").toString(), QStringLiteral("x"));
    }

    // Planted-bug: the share targets stay empty while nothing shows them.
    void targetsWhenRead()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        lists.setRooms({ room("i", { { "state", "invited" } }), room("a"), room("b") });
        QCOMPARE(keysOf(*lists.targets()), QStringList({ "a", "b" }));
        lists.setRooms({ room("b"), room("c") });
        QCOMPARE(keysOf(*lists.targets()), QStringList({ "b", "c" }));
    }

    // Planted-bug: the merge cache hands out a row whose source changed.
    void mergeCache()
    {
        RoomLists::MergeCache cache;
        const QVariantList one = rooms({ "a" });
        const QVariantList m1 = RoomLists::mergeAccounts({ { 1, one } }, &cache);
        const QVariantList m2 = RoomLists::mergeAccounts({ { 1, one } }, &cache);
        QVERIFY(m1.at(0).toMap().isSharedWith(m2.at(0).toMap()));
        const QVariantList m3 = RoomLists::mergeAccounts({ { 1, { room("a", { { "name", "A" } }) } } }, &cache);
        QCOMPARE(m3.at(0).toMap().value("name").toString(), QStringLiteral("A"));
        QCOMPARE(m3.at(0).toMap().value("uid").toString(), QStringLiteral("1/a"));
    }

    // The order and only the changed rows: the rows not sent stay the
    // same maps, a sent one is new, a room not in the order goes.
    // Planted-bug: applyRooms takes the rows of `items` only (the rows not
    // sent drop out of the list).
    void deltaKeepsUnsentRows()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        QCOMPARE(lists.applyRooms(roomsEvent(3, 1, true, { "a", "b", "c" }, rooms({ "a", "b", "c" }))), RoomLists::RoomsApplied);
        const QVariantList first = lists.lastRooms();
        QCOMPARE(lists.lastChanged(), 3);
        QCOMPARE(lists.applyRooms(roomsEvent(3, 2, false, { "b", "a" }, { room("b", { { "preview", "x" } }) })), RoomLists::RoomsApplied);
        const QVariantList second = lists.lastRooms();
        QCOMPARE(second.size(), 2);
        QCOMPARE(second.at(0).toMap().value("preview").toString(), QStringLiteral("x"));
        QVERIFY(second.at(1).toMap().isSharedWith(first.at(0).toMap()));
        QCOMPARE(lists.lastChanged(), 1);
        QCOMPARE(keysOf(*lists.rooms()), QStringList({ "b", "a" }));
        QVERIFY(lists.roomInfo("c").isEmpty());
        // a full event (a resync) with the same rows keeps them as they were
        QCOMPARE(lists.applyRooms(roomsEvent(3, 3, true, { "b", "a" }, { room("b", { { "preview", "x" } }), room("a") })), RoomLists::RoomsApplied);
        QVERIFY(lists.lastRooms().at(0).toMap().isSharedWith(second.at(0).toMap()));
        QVERIFY(lists.lastRooms().at(1).toMap().isSharedWith(first.at(0).toMap()));
        QCOMPARE(lists.lastChanged(), 0);
    }

    // A skipped event, or a room the lists do not have, is not applied and
    // asks for everything once; a full one applies and may ask again; an
    // earlier stream's event is dropped; a later stream starts at its full one.
    // Planted-bug: applyRooms applies an event after a gap in `seq`.
    // Planted-bug: m_resyncAsked is not reset by a full event (the next
    // gap is never asked about).
    void gapsAndUnknownRoomsAskOnce()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        // nothing yet: a delta cannot be applied
        QCOMPARE(lists.applyRooms(roomsEvent(5, 2, false, { "a" }, {})), RoomLists::RoomsResync);
        QCOMPARE(lists.applyRooms(roomsEvent(5, 3, false, { "a" }, {})), RoomLists::RoomsDropped);
        QCOMPARE(lists.applyRooms(roomsEvent(5, 4, true, { "a" }, rooms({ "a" }))), RoomLists::RoomsApplied);
        // seq 5 missed
        QCOMPARE(lists.applyRooms(roomsEvent(5, 6, false, { "a" }, { room("a", { { "unread", 1 } }) })), RoomLists::RoomsResync);
        QCOMPARE(lists.applyRooms(roomsEvent(5, 7, false, { "a" }, {})), RoomLists::RoomsDropped);
        QCOMPARE(lists.lastRooms().at(0).toMap().value("unread").toInt(), 0);
        QCOMPARE(lists.applyRooms(roomsEvent(5, 8, true, { "a" }, { room("a", { { "unread", 1 } }) })), RoomLists::RoomsApplied);
        QCOMPARE(lists.lastRooms().at(0).toMap().value("unread").toInt(), 1);
        // a room the lists never had
        QCOMPARE(lists.applyRooms(roomsEvent(5, 9, false, { "z", "a" }, {})), RoomLists::RoomsResync);
        QCOMPARE(keysOf(*lists.rooms()), QStringList({ "a" }));
        QCOMPARE(lists.applyRooms(roomsEvent(5, 10, true, { "z", "a" }, rooms({ "z", "a" }))), RoomLists::RoomsApplied);
        // an earlier stream's late event, then a new stream (a new login)
        QCOMPARE(lists.applyRooms(roomsEvent(4, 99, true, { "old" }, rooms({ "old" }))), RoomLists::RoomsDropped);
        QCOMPARE(lists.applyRooms(roomsEvent(6, 2, false, { "a" }, {})), RoomLists::RoomsResync);
        QCOMPARE(lists.applyRooms(roomsEvent(6, 1, true, { "a" }, rooms({ "a" }))), RoomLists::RoomsApplied);
        QCOMPARE(lists.applyRooms(roomsEvent(6, 2, false, { "a" }, {})), RoomLists::RoomsApplied);
        QCOMPARE(keysOf(*lists.rooms()), QStringList({ "a" }));
        // logged out: a late event of that login's stream is dropped, the
        // next login's stream starts afresh
        // Planted-bug: clear() forgets the streams before (the rooms of the
        // account logged out of come back).
        lists.clear();
        QCOMPARE(lists.applyRooms(roomsEvent(6, 3, true, { "a" }, rooms({ "a" }))), RoomLists::RoomsDropped);
        QCOMPARE(keysOf(*lists.rooms()), QStringList());
        QCOMPARE(lists.applyRooms(roomsEvent(9, 1, true, { "b" }, rooms({ "b" }))), RoomLists::RoomsApplied);
        QCOMPARE(keysOf(*lists.rooms()), QStringList({ "b" }));
    }

    // A delta's unchanged rows reach the merged view as before: its cache
    // hands out the same merged row, the changed one is merged anew.
    // Planted-bug: applyRooms copies the rows it keeps (the merge cache
    // misses every row).
    void deltaKeepsTheMergeCache()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        lists.applyRooms(roomsEvent(1, 1, true, { "a", "b" }, rooms({ "a", "b" })));
        RoomLists::MergeCache cache;
        const QVariantList m1 = RoomLists::mergeAccounts({ { 0, lists.lastRooms() } }, &cache);
        lists.applyRooms(roomsEvent(1, 2, false, { "a", "b" }, { room("a", { { "name", "A" } }) }));
        const QVariantList m2 = RoomLists::mergeAccounts({ { 0, lists.lastRooms() } }, &cache);
        QVERIFY(m1.at(1).toMap().isSharedWith(m2.at(1).toMap()));
        QCOMPARE(m2.at(0).toMap().value("name").toString(), QStringLiteral("A"));
    }

    // A watched list hears of the one changed row only.
    // Planted-bug: applyRooms passes the sent rows on for every room.
    void deltaTellsOneRow()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
        RoomLists lists(&s);
        Mirror view(*lists.rooms());
        lists.applyRooms(roomsEvent(1, 1, true, { "a", "b", "c" }, rooms({ "a", "b", "c" })));
        view.reset();
        lists.applyRooms(roomsEvent(1, 2, false, { "a", "b", "c" }, { room("b", { { "preview", "new" } }) }));
        QCOMPARE(view.changes.size(), 1);
        QCOMPARE(view.inserts + view.removes + view.moves, 0);
        view.reset();
        // a resync with the same rows tells nothing
        lists.applyRooms(roomsEvent(1, 3, true, { "a", "b", "c" }, { room("a"), room("b", { { "preview", "new" } }), room("c") }));
        QCOMPARE(view.changes.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRoomModel)
#include "tst_roommodel.moc"
