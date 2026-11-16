// SPDX-License-Identifier: GPL-3.0-or-later
// The order of the room lists (RoomLists::sortRooms), through RoomLists as
// the app uses it: the core's `rooms` event in, a tab's model out. The
// "all" tab is used because it holds every room that is not a joined
// space, rooms and people alike, in one list.
//
// The order, as README and code mean it (the spec these tests check):
//
//  1. Invitations come first, always.
//  2. With "favourites on top" (favsOnTop, off by default): favourites
//     before the rest, within the invitations as well.
//  3. With "low priority at the bottom" (lowLast, on by default): low
//     priority rooms after the rest, within each group above.
//  4. Then the tab's order (sortOrder):
//     - "activity" (the default, and any value not known): as the core
//       sent them, which is by activity;
//     - "unread": the highest badge first, the badge being the
//       notification count, or the unread count when there are no
//       notifications;
//     - "name": by name, compared the way the locale sorts (QCollator).
//  5. Ties keep the core's order (the sort is stable).
//  6. Each switch and the order are a tab's own; a tab without its own
//     value takes the value all tabs shared before.
//
// The randomized test compares against the comparator Rustrx.cpp had
// before f3e0dc9 built the keys once, lifted unchanged.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "RoomLists.h"
#include "RoomModel.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <memory>

namespace {

struct Config {
    QString order;     // empty: not set, the default
    QVariant favs;     // invalid: not set
    QVariant low;
};

// a RoomLists with its own settings file holding the "all" tab's values
class Lists
{
public:
    Lists(const QString &dir, const Config &c, const QString &tab = QStringLiteral("all"))
        : settings(dir + QStringLiteral("/settings.ini"), QSettings::IniFormat)
    {
        settings.clear();
        const QString own = QStringLiteral("tabs/") + tab + QLatin1Char('/');
        if (!c.order.isEmpty()) settings.setValue(own + "sortOrder", c.order);
        if (c.favs.isValid()) settings.setValue(own + "favsOnTop", c.favs);
        if (c.low.isValid()) settings.setValue(own + "lowLast", c.low);
        lists.reset(new RoomLists(&settings));
    }
    QSettings settings;
    std::unique_ptr<RoomLists> lists;

    // the room ids of the "all" tab after the core sent these rooms
    QStringList all(const QVariantList &rooms)
    {
        lists->setRooms(rooms);
        QStringList ids;
        RoomModel *m = lists->all();
        for (int i = 0; i < m->rowCount(); ++i)
            ids << m->get(i).value("room_id").toString();
        return ids;
    }
};

QVariantMap room(const QString &id, const QVariantMap &more = QVariantMap())
{
    QVariantMap r { { "room_id", id }, { "name", id }, { "state", "joined" } };
    for (auto it = more.constBegin(); it != more.constEnd(); ++it)
        r.insert(it.key(), it.value());
    return r;
}

QStringList idsOf(const QVariantList &rooms)
{
    QStringList ids;
    for (const QVariant &v : rooms)
        ids << v.toMap().value("room_id").toString();
    return ids;
}

// --- the comparator before f3e0dc9 (src/Rustrx.cpp at f3e0dc9^, lines
// 58-68 and 486-514), unchanged but for the settings it read ---

static bool isInvited(const QVariantMap &room)
{
    return room.value("state").toString() == QLatin1String("invited");
}

// the number a room's badge shows: notifications, else unread messages
static int badgeCount(const QVariantMap &room)
{
    const int n = room.value("notifications").toInt();
    return n > 0 ? n : room.value("unread").toInt();
}

void oldSortRooms(QVariantList &rooms, const QString &order, bool favsFirst, bool lowAtEnd)
{
    std::stable_sort(rooms.begin(), rooms.end(), [&](const QVariant &va, const QVariant &vb) {
        const QVariantMap a = va.toMap(), b = vb.toMap();
        const bool ia = isInvited(a), ib = isInvited(b);
        if (ia != ib)
            return ia;
        if (favsFirst) {
            const bool fa = a.value("is_fav").toBool(), fb = b.value("is_fav").toBool();
            if (fa != fb)
                return fa;
        }
        if (lowAtEnd) {
            const bool la = a.value("is_low").toBool(), lb = b.value("is_low").toBool();
            if (la != lb)
                return lb;
        }
        if (order == QLatin1String("unread")) {
            // the most unread on top; equal counts keep the activity order
            const int ua = badgeCount(a), ub = badgeCount(b);
            if (ua != ub)
                return ua > ub;
        } else if (order == QLatin1String("name")) {
            return QString::localeAwareCompare(a.value("name").toString(), b.value("name").toString()) < 0;
        }
        return false;
    });
}

// xorshift32: the same lists on every machine
quint32 g_state = 3000u;
quint32 next()
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    return g_state;
}
int below(int n) { return int(next() % quint32(n)); }

}

class TestRoomSort : public QObject
{
    Q_OBJECT

    QTemporaryDir tmp;

    QString dir(const QString &name)
    {
        QDir().mkpath(tmp.path() + QLatin1Char('/') + name);
        return tmp.path() + QLatin1Char('/') + name;
    }

private slots:
    // Planted-bug: the comparator puts an invitation after a joined room
    // (return b.invited).
    void invitationsFirst()
    {
        Lists l(dir("inv"), { QStringLiteral("name"), true, true });
        const QVariantList rooms {
            room("a", { { "is_fav", true } }),
            room("z", { { "state", "invited" }, { "is_low", true } }),
            room("b"),
            room("y", { { "state", "invited" } }),
        };
        QCOMPARE(l.all(rooms), QStringList({ "y", "z", "a", "b" }));
    }

    // Planted-bug: the favourite key ignores the switch
    // (k.fav = r.value("is_fav").toBool()).
    void favouritesSwitch()
    {
        const QVariantList rooms { room("1"), room("2", { { "is_fav", true } }), room("3"),
                                   room("4", { { "is_fav", true }, { "state", "invited" } }),
                                   room("5", { { "state", "invited" } }) };
        Lists on(dir("favon"), { QString(), true, QVariant() });
        QCOMPARE(on.all(rooms), QStringList({ "4", "5", "2", "1", "3" }));
        Lists off(dir("favoff"), { QString(), false, QVariant() });
        QCOMPARE(off.all(rooms), QStringList({ "4", "5", "1", "2", "3" }));
        // off by default
        Lists unset(dir("favunset"), { QString(), QVariant(), QVariant() });
        QCOMPARE(unset.all(rooms), QStringList({ "4", "5", "1", "2", "3" }));
    }

    // Planted-bug: low priority goes to the top instead (return a.low).
    void lowPrioritySwitch()
    {
        const QVariantList rooms { room("1", { { "is_low", true } }), room("2"),
                                   room("3", { { "is_low", true }, { "is_fav", true } }), room("4", { { "is_fav", true } }) };
        Lists on(dir("lowon"), { QString(), true, true });
        QCOMPARE(on.all(rooms), QStringList({ "4", "3", "2", "1" }));
        Lists off(dir("lowoff"), { QString(), true, false });
        QCOMPARE(off.all(rooms), QStringList({ "3", "4", "1", "2" }));
    }

    // Planted-bug: lowLast's default in Keys.h is false.
    void defaults()
    {
        // activity, favourites not on top, low priority at the bottom
        const QVariantList rooms { room("c", { { "is_low", true } }), room("b", { { "is_fav", true }, { "unread", 9 } }), room("a") };
        Lists l(dir("defaults"), { QString(), QVariant(), QVariant() });
        QCOMPARE(l.all(rooms), QStringList({ "b", "a", "c" }));
        // what the filter menu shows for the current tab ("rooms", unset)
        QCOMPARE(l.lists->sortOrder(), QStringLiteral("activity"));
        QCOMPARE(l.lists->lowLast(), true);
        QCOMPARE(l.lists->favsOnTop(), false);
    }

    // Planted-bug: the badge is the unread count only (notifications
    // ignored).
    void unreadOrder()
    {
        const QVariantList rooms {
            room("quiet"),
            room("u3", { { "unread", 3 } }),
            room("n2u9", { { "notifications", 2 }, { "unread", 9 } }),
            room("u5", { { "unread", 5 } }),
            room("n5", { { "notifications", 5 } }),
            room("u2", { { "unread", 2 } }),
        };
        Lists l(dir("unread"), { QStringLiteral("unread"), QVariant(), QVariant() });
        // n2u9 has a badge of 2: notifications before unread; ties
        // (u5/n5, n2u9/u2) keep the core's order
        QCOMPARE(l.all(rooms), QStringList({ "u5", "n5", "u3", "n2u9", "u2", "quiet" }));
    }

    // Planted-bug: the name comparison is turned round (> 0).
    void nameOrder()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        const QVariantList rooms {
            room("1", { { "name", "beta" } }), room("2", { { "name", "Alpha" } }),
            room("3", { { "name", "alpha" } }), room("4", { { "name", "Éclair" } }),
            room("5", { { "name", "delta" } }), room("6", { { "name", "beta" } }),
        };
        Lists l(dir("name"), { QStringLiteral("name"), QVariant(), QVariant() });
        // by the locale, not by code point: "Éclair" with the e, after
        // "delta"; lower case before upper case; equal names (1, 6) keep
        // the core's order
        QCOMPARE(l.all(rooms), QStringList({ "3", "2", "1", "6", "5", "4" }));
        QLocale::setDefault(QLocale::system());
    }

    // Planted-bug: the comparator's last answer is "ib < ia" instead of
    // false, which turns the activity order round.
    void activityOrder()
    {
        QVariantList rooms;
        for (int i = 0; i < 40; ++i)
            rooms << room(QString::number(i), { { "unread", i % 3 }, { "name", QString::number(39 - i) } });
        for (const QString &order : { QString(), QStringLiteral("activity"), QStringLiteral("nonsense") }) {
            Lists l(dir("activity" + order), { order, QVariant(), QVariant() });
            QCOMPARE(l.all(rooms), idsOf(rooms));
        }
    }

    // Planted-bug: tabValue reads the shared value even where the tab has
    // its own (m_settings->value(key, fallback) always).
    void tabOwnSettings()
    {
        Lists l(dir("own"), { QStringLiteral("unread"), QVariant(), QVariant() });
        l.settings.setValue("sortOrder", "name");   // the old shared value
        const QVariantList rooms { room("b"), room("a", { { "unread", 1 } }), room("c", { { "unread", 2 } }) };
        QCOMPARE(l.all(rooms), QStringList({ "c", "a", "b" }));
        // a tab without its own value: the shared one
        Lists shared(dir("shared"), { QString(), QVariant(), QVariant() }, QStringLiteral("people"));
        shared.settings.setValue("sortOrder", "name");
        shared.lists.reset(new RoomLists(&shared.settings));
        QCOMPARE(shared.all(rooms), QStringList({ "a", "b", "c" }));
    }

    // Planted-bug: std::sort instead of std::stable_sort, so ties lose
    // the core's order.
    void randomAgainstOldComparator()
    {
        // the locale is the process's own (run.sh runs this under more
        // than one): Qt 5.15's localeAwareCompare keeps the collator of the
        // first call, so switching QLocale's default here would compare
        // two locales, a state the app (which never sets it) cannot reach
        const QString locale = QLocale().name();
        static const QStringList names = {
            "alpha", "Alpha", "ALPHA", "älpha", "beta", "Beta 2", "beta 10", "_x", "1", "", "😀 room",
            "Zeta", "zeta", "Æon", "éclair", "eclair", "Ölfeld", "oelfeld", "日本", "#room", "zz", "Z",
        };
        static const char *orders[] = { "activity", "unread", "name" };
        // every order and switch combination, each a RoomLists of its own
        struct Combo { QString order; bool favs, low; std::unique_ptr<Lists> l; };
        std::vector<Combo> combos;
        for (const char *o : orders)
            for (int f = 0; f < 2; ++f)
                for (int lo = 0; lo < 2; ++lo) {
                    Combo c { QLatin1String(o), f == 1, lo == 1, nullptr };
                    c.l.reset(new Lists(dir(QStringLiteral("rnd-%1-%2-%3").arg(QLatin1String(o)).arg(f).arg(lo)),
                                        { c.order, c.favs, c.low }));
                    combos.push_back(std::move(c));
                }
        g_state = 3000u;
        int lists = 0, sorts = 0;
        for (int n = 0; n < 3000; ++n) {
            QVariantList rooms;
            const int size = below(n % 50 == 0 ? 120 : 25);
            for (int i = 0; i < size; ++i) {
                // few distinct values, so ties are common
                QVariantMap r {
                    { "room_id", QStringLiteral("!%1:%2").arg(n).arg(i) },
                    { "name", names.at(below(names.size())) },
                    { "state", below(8) == 0 ? "invited" : "joined" },
                    { "is_fav", below(4) == 0 },
                    { "is_low", below(5) == 0 },
                    { "is_dm", below(3) == 0 },
                };
                if (below(2)) r.insert("unread", below(4));
                if (below(3) == 0) r.insert("notifications", below(3));
                rooms << r;
            }
            ++lists;
            for (Combo &c : combos) {
                QVariantList want = rooms;
                oldSortRooms(want, c.order, c.favs, c.low);
                const QStringList got = c.l->all(rooms);
                ++sorts;
                if (got != idsOf(want)) {
                    qWarning("list %d, %s favs %d low %d:\n  old: %s\n  now: %s", n, qPrintable(c.order), c.favs, c.low,
                             qPrintable(idsOf(want).join(' ')), qPrintable(got.join(' ')));
                    QFAIL("a different order");
                }
            }
        }
        qInfo("%s: %d lists, %d sorts, all as before", qPrintable(locale), lists, sorts);
    }
};

QTEST_GUILESS_MAIN(TestRoomSort)
#include "tst_roomsort.moc"
