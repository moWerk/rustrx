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

#include "Keys.h"
#include "RoomLists.h"
#include "RoomModel.h"

#include <QJsonDocument>
#include <QJsonObject>
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


// the room ids of a model in its order, and its rows
QStringList modelIds(RoomModel *m)
{
    QStringList ids;
    for (int i = 0; i < m->rowCount(); ++i)
        ids << m->get(i).value("uid").toString() + m->get(i).value("room_id").toString();
    return ids;
}

// what a view holds: the model's signals replayed on a list of keys
struct Follower
{
    explicit Follower(RoomModel *model)
        : m(model)
    {
        keys = modelIds(m);
        QObject::connect(m, &QAbstractItemModel::rowsInserted, [this](const QModelIndex &, int first, int last) {
            for (int i = first; i <= last; ++i)
                keys.insert(i, m->get(i).value("uid").toString() + m->get(i).value("room_id").toString());
        });
        QObject::connect(m, &QAbstractItemModel::rowsRemoved, [this](const QModelIndex &, int first, int last) {
            for (int i = last; i >= first; --i)
                keys.removeAt(i);
        });
        QObject::connect(m, &QAbstractItemModel::rowsMoved,
                         [this](const QModelIndex &, int first, int last, const QModelIndex &, int dest) {
            const QStringList block = keys.mid(first, last - first + 1);
            for (int i = 0; i < block.size(); ++i)
                keys.removeAt(first);
            const int to = dest > last ? dest - block.size() : dest;
            for (int i = 0; i < block.size(); ++i)
                keys.insert(to + i, block.at(i));
        });
    }
    RoomModel *m;
    QStringList keys;
};

// every signal a list sends the app, as text in the order sent
struct Heard
{
    explicit Heard(RoomLists *l)
    {
        QObject::connect(l, &RoomLists::newMessages, [this](const QString &id, const QString &name, int n) {
            lines << QStringLiteral("new %1 %2 %3").arg(id, name).arg(n);
        });
        QObject::connect(l, &RoomLists::roomRead, [this](const QString &id) { lines << "read " + id; });
        QObject::connect(l, &RoomLists::invited, [this](const QString &id, const QString &name, const QString &inviter, bool direct, bool known) {
            lines << QStringLiteral("invited %1 %2 %3 %4 %5").arg(id, name, inviter).arg(direct).arg(known);
        });
        QObject::connect(l, &RoomLists::inviteGone, [this](const QString &id) { lines << "gone " + id; });
        QObject::connect(l, &RoomLists::unreadChanged, [this, l]() {
            lines << QStringLiteral("unread %1 %2").arg(l->unreadTotal()).arg(l->unreadChats());
        });
    }
    QStringList lines;
};

// a core's state: its rooms and the order it sends them in
struct Core
{
    QVector<QVariantMap> rooms;
    qint64 stream = 1, seq = 0, clock = 1000;
    // a shuffled order now and then: the lists keep whatever order comes
    bool shuffled = false;
    QStringList order() const
    {
        QVector<int> idx(rooms.size());
        for (int i = 0; i < idx.size(); ++i)
            idx[i] = i;
        if (!shuffled) {
            std::stable_sort(idx.begin(), idx.end(), [this](int a, int b) {
                const QVariantMap &x = rooms.at(a), &y = rooms.at(b);
                const bool ix = x.value("state") == "invited", iy = y.value("state") == "invited";
                if (ix != iy)
                    return ix;
                if (x.value("ts").toLongLong() != y.value("ts").toLongLong())
                    return x.value("ts").toLongLong() > y.value("ts").toLongLong();
                return x.value("room_id").toString() < y.value("room_id").toString();
            });
        } else {
            for (int i = idx.size() - 1; i > 0; --i)
                std::swap(idx[i], idx[below(i + 1)]);
        }
        QStringList ids;
        for (int i : idx)
            ids << rooms.at(i).value("room_id").toString();
        return ids;
    }
    // the event: every row (full), or only the ones in `sent`; in the
    // order given, else the core's
    QVariantMap event(bool full, const QSet<QString> &sent, QStringList ord = QStringList())
    {
        if (ord.isEmpty())
            ord = order();
        QVariantMap items;
        for (const QVariantMap &r : rooms) {
            const QString id = r.value("room_id").toString();
            if (full || sent.contains(id)) {
                QVariantMap fresh = r;
                fresh.detach();
                items.insert(id, fresh);
            }
        }
        QVariantList ids;
        for (const QString &id : ord)
            ids << id;
        return { { "stream", stream }, { "seq", ++seq }, { "full", full }, { "order", ids }, { "items", items } };
    }
};

QVariantMap randomRoom(const QString &id, qint64 ts)
{
    static const QStringList names = { "alpha", "Alpha", "beta", "Beta 2", "éclair", "Zeta", "😀 room", "", "delta", "Ölfeld" };
    QVariantMap r { { "room_id", id }, { "name", names.at(below(names.size())) }, { "ts", ts },
                    { "state", below(10) == 0 ? "invited" : "joined" }, { "is_fav", below(4) == 0 },
                    { "is_low", below(6) == 0 }, { "is_dm", below(3) == 0 }, { "is_space", below(12) == 0 },
                    { "muted", below(6) == 0 }, { "unread", below(3) ? 0 : below(5) },
                    { "notifications", below(4) ? 0 : below(3) }, { "alias", "#" + id }, { "preview", "hi" },
                    { "inviter", "@x:y" }, { "inviter_known", below(2) == 0 } };
    return r;
}

// one change the core may send: a message, a read, a switch, a room
// joined, left or invited
void mutate(Core &core, QSet<QString> &sent, int &next)
{
    const int what = below(14);
    if (what == 0 || core.rooms.isEmpty()) {
        const QString id = QStringLiteral("!n%1").arg(next++);
        core.rooms << randomRoom(id, core.clock += 7);
        sent.insert(id);
        return;
    }
    const int i = below(core.rooms.size());
    QVariantMap &r = core.rooms[i];
    const QString id = r.value("room_id").toString();
    switch (what) {
    case 1:
        sent.remove(id);
        core.rooms.removeAt(i);
        return;
    case 2: r["is_fav"] = !r.value("is_fav").toBool(); break;
    case 3: r["is_low"] = !r.value("is_low").toBool(); break;
    case 4: r["state"] = r.value("state") == "invited" ? "joined" : "invited"; break;
    case 5: r["unread"] = 0; r["notifications"] = 0; break;
    case 6: r["muted"] = !r.value("muted").toBool(); break;
    case 7: r["name"] = QStringLiteral("name %1").arg(below(5)); break;
    case 8: r["is_dm"] = !r.value("is_dm").toBool(); break;
    case 9: r["is_space"] = !r.value("is_space").toBool(); break;
    case 10: r["latest_event_id"] = QStringLiteral("$%1").arg(next++); break;
    default:
        // a new message: to the top, unread, maybe a notification
        r["ts"] = core.clock += 7;
        r["preview"] = QStringLiteral("message %1").arg(next++);
        r["latest_event_id"] = QStringLiteral("$%1").arg(next++);
        r["unread"] = r.value("unread").toInt() + 1;
        if (below(2))
            r["notifications"] = r.value("notifications").toInt() + 1;
        break;
    }
    sent.insert(id);
}

// everything a list shows or tells, as text
QStringList stateOf(RoomLists *l)
{
    QStringList out;
    for (RoomModel *m : { l->rooms(), l->people(), l->favourites(), l->all(), l->spaces() }) {
        QStringList rows;
        for (int i = 0; i < m->rowCount(); ++i)
            rows << QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m->get(i))).toJson(QJsonDocument::Compact));
        out << rows.join('\n');
    }
    out << QStringLiteral("unread %1 %2").arg(l->unreadTotal()).arg(l->unreadChats());
    QStringList ids = l->roomsById().keys();
    ids.sort();
    for (const QString &id : ids)
        out << id + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(l->roomsById().value(id))).toJson(QJsonDocument::Compact));
    out << l->firstUnread().value("room_id").toString();
    return out;
}
}


// RoomModel's warning when touched rows did not give the new order (it
// then compares the whole list, which hides the mistake from the result)
int g_orderWarnings = 0;
QtMessageHandler g_previousHandler = nullptr;
void countOrderWarnings(QtMsgType type, const QMessageLogContext &context, const QString &text)
{
    if (text.contains(QLatin1String("touched rows did not give the new order")))
        ++g_orderWarnings;
    g_previousHandler(type, context, text);
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

    // A delta places only the rows that changed; the result has to be what
    // sorting every tab again gives: two lists fed the same states, one by
    // deltas, one by full events, compared after every step (every tab's
    // rows in order, the unread totals, roomInfo, the first unread room, the
    // share targets read lazily and followed, every signal in order, the
    // invitations kept in the settings, and the views' copies made from the
    // signals), with settings and the filter changed on the way, shuffled
    // orders (the guard), many rows at once (the full sort) and the merged
    // view of two accounts.
    // Planted-bug: the insertion ignores the core's order on equal keys
    // (r.pos < ... dropped from the binary search).
    // Planted-bug: placeTouched skips the tabs a row left (old.tabs unread).
    // Planted-bug: takeRows does not check that the untouched rows kept
    // their order (inOrder always true).
    void deltaAsFull()
    {
        g_orderWarnings = 0;
        g_previousHandler = qInstallMessageHandler(countOrderWarnings);
        static const char *orders[] = { "activity", "unread", "name" };
        static const char *kinds[] = { "rooms", "people", "favs", "all", "spaces" };
        g_state = 99u;
        int steps = 0, deltas = 0;
        for (int round = 0; round < 30; ++round) {
            QTemporaryDir own;
            QSettings settings(own.filePath("s.ini"), QSettings::IniFormat);
            for (const char *kind : kinds) {
                const QString k = QStringLiteral("tabs/") + QLatin1String(kind) + QLatin1Char('/');
                settings.setValue(k + "sortOrder", QLatin1String(orders[below(3)]));
                settings.setValue(k + "favsOnTop", below(2) == 0);
                settings.setValue(k + "lowLast", below(3) != 0);
                settings.setValue(k + "hideMuted", below(3) == 0);
            }
            // two accounts, each as deltas (d) and as full events (f); the
            // merged view of each pair
            RoomLists d0(&settings, "d0/"), f0(&settings, "f0/"), d1(&settings, "d1/"), f1(&settings, "f1/");
            RoomLists dm(&settings, "dm/"), fm(&settings, "fm/");
            dm.setDisplayOnly(true);
            fm.setDisplayOnly(true);
            RoomLists *pairs[][2] = { { &d0, &f0 }, { &d1, &f1 }, { &dm, &fm } };
            // followed: every tab of the delta lists, d1's targets too
            std::vector<std::unique_ptr<Follower>> followers;
            for (RoomLists *l : { &d0, &d1, &dm, &f0 })
                for (RoomModel *m : { l->rooms(), l->people(), l->favourites(), l->all(), l->spaces() })
                    followers.emplace_back(new Follower(m));
            followers.emplace_back(new Follower(d1.targets()));
            Heard h0d(&d0), h0f(&f0), h1d(&d1), h1f(&f1);
            Core c0, c1;
            int next = 0;
            for (int i = 0, n = below(40); i < n; ++i)
                c0.rooms << randomRoom(QStringLiteral("!a%1").arg(i), c0.clock += 3);
            for (int i = 0, n = below(10); i < n; ++i)
                c1.rooms << randomRoom(QStringLiteral("!a%1").arg(i), c1.clock += 5);
            RoomLists::MergeCache cache;
            const auto merge = [&](bool full) {
                QVector<QString> uids;
                const QVariantList rows = RoomLists::mergeAccounts({ { 0, d0.lastRooms() }, { 1, d1.lastRooms() } }, &cache, &uids);
                dm.setRooms(rows, uids, full);
                fm.setRooms(RoomLists::mergeAccounts({ { 0, f0.lastRooms() }, { 1, f1.lastRooms() } }), true);
            };
            QCOMPARE(d0.applyRooms(c0.event(true, {})), RoomLists::RoomsApplied);
            QCOMPARE(f0.applyRooms(c0.event(true, {})), RoomLists::RoomsApplied);
            c0.seq = 1;
            d1.applyRooms(c1.event(true, {}));
            f1.applyRooms(c1.event(true, {}));
            c1.seq = 1;
            merge(true);
            for (int step = 0; step < 50; ++step) {
                ++steps;
                const int what = below(40);
                if (what == 0) {
                    // a setting of one tab, then shown again
                    const QString k = QStringLiteral("tabs/") + QLatin1String(kinds[below(5)]) + QLatin1Char('/');
                    switch (below(4)) {
                    case 0: settings.setValue(k + "sortOrder", QLatin1String(orders[below(3)])); break;
                    case 1: settings.setValue(k + "favsOnTop", below(2) == 0); break;
                    case 2: settings.setValue(k + "lowLast", below(2) == 0); break;
                    default: settings.setValue(k + "hideMuted", below(2) == 0); break;
                    }
                    for (auto &p : pairs) {
                        p[0]->reloadSettings();
                        p[1]->reloadSettings();
                    }
                } else if (what == 1) {
                    static const QStringList filters = { "", "a", "ph", "!a1", "zz" };
                    const QString f = filters.at(below(filters.size()));
                    for (auto &p : pairs) {
                        p[0]->setRoomFilter(f);
                        p[1]->setRoomFilter(f);
                    }
                } else {
                    Core &core = below(4) ? c0 : c1;
                    RoomLists &dl = &core == &c0 ? d0 : d1, &fl = &core == &c0 ? f0 : f1;
                    QSet<QString> sent;
                    const int changes = below(15) == 0 ? 20 + below(20) : 1 + below(3);
                    for (int k = 0; k < changes; ++k)
                        mutate(core, sent, next);
                    core.shuffled = below(12) == 0;
                    // a delta now and then a full one (a resync, the sweep)
                    const bool full = below(20) == 0;
                    const QStringList ord = core.order();
                    QCOMPARE(dl.applyRooms(core.event(full, sent, ord)), RoomLists::RoomsApplied);
                    deltas += dl.lastFull() ? 0 : 1;
                    QCOMPARE(fl.applyRooms(core.event(true, {}, ord)), RoomLists::RoomsApplied);
                    --core.seq;
                    QCOMPARE(fl.lastFull(), true);
                    merge(full);
                    core.shuffled = false;
                }
                for (auto &p : pairs) {
                    const QStringList a = stateOf(p[0]), b = stateOf(p[1]);
                    if (a != b) {
                        for (int i = 0; i < a.size() && i < b.size(); ++i)
                            if (a.at(i) != b.at(i))
                                qWarning("round %d step %d, part %d:\n delta: %s\n full:  %s", round, step, i,
                                         qPrintable(a.at(i)), qPrintable(b.at(i)));
                        QFAIL("a delta gave other lists than a full sort");
                    }
                }
                QCOMPARE(h0d.lines, h0f.lines);
                QCOMPARE(h1d.lines, h1f.lines);
                QCOMPARE(settings.value("d0/invitesNotified").toStringList(), settings.value("f0/invitesNotified").toStringList());
                for (const auto &f : followers)
                    QCOMPARE(f->keys, modelIds(f->m));
                QCOMPARE(modelIds(d1.targets()), modelIds(f1.targets()));
                if (below(3) == 0)
                    QCOMPARE(modelIds(d0.targets()), modelIds(f0.targets()));
            }
        }
        qInstallMessageHandler(g_previousHandler);
        QCOMPARE(g_orderWarnings, 0);
        qInfo("%s: %d steps, %d of them placed by delta, all as a full sort", qPrintable(QLocale().name()), steps, deltas);
        QVERIFY(deltas > steps / 3);
    }

    // The merged view: two accounts' rooms in one list, in the core's
    // order across both (invitations first, then the last activity), each
    // row with its account and uid; a room in both accounts is two rows.
    // Planted-bug: mergeAccounts keeps the accounts one after the other
    // (no sort), or keys a row by its room id alone.
    void mergedAccountsInterleave()
    {
        const QVariantList first {
            room("!shared", { { "ts", 500 } }),
            room("!a1", { { "ts", 300 } }),
        };
        const QVariantList second {
            room("!b-invite", { { "state", "invited" }, { "ts", 1 } }),
            room("!b1", { { "ts", 400 } }),
            room("!shared", { { "ts", 100 } }),
        };
        const QVariantList merged = RoomLists::mergeAccounts({ { 0, first }, { 2, second } });
        QStringList uids;
        for (const QVariant &v : merged)
            uids << v.toMap().value("uid").toString();
        QCOMPARE(uids, QStringList({ "2/!b-invite", "0/!shared", "2/!b1", "0/!a1", "2/!shared" }));
        QCOMPARE(merged.at(1).toMap().value("account").toInt(), 0);
        QCOMPARE(merged.at(4).toMap().value("account").toInt(), 2);

        // through a display-only list: both rows of the shared room show
        Lists l(dir("merged"), { QString(), QVariant(), QVariant() });
        l.lists->setDisplayOnly(true);
        l.lists->setRooms(merged);
        RoomModel *m = l.lists->all();
        int shared = 0;
        for (int i = 0; i < m->rowCount(); ++i)
            if (m->get(i).value("room_id").toString() == QLatin1String("!shared"))
                ++shared;
        QCOMPARE(shared, 2);
    }

    // The merged list only shows: an account's own list notifies new
    // messages and invitations, the merged one must not (they would come
    // twice) and must not write the invitations already notified.
    // Planted-bug: drop the m_displayOnly checks in distributeRooms.
    void mergedListNotifiesNothing()
    {
        Lists l(dir("quiet"), { QString(), QVariant(), QVariant() });
        l.lists->setDisplayOnly(true);
        int messages = 0, invitations = 0;
        QObject::connect(l.lists.get(), &RoomLists::newMessages, [&]() { ++messages; });
        QObject::connect(l.lists.get(), &RoomLists::invited, [&]() { ++invitations; });
        l.lists->setRooms(RoomLists::mergeAccounts({ { 0, { room("!r", { { "notifications", 0 } }) } } }));
        l.lists->setRooms(RoomLists::mergeAccounts({ { 0, { room("!r", { { "notifications", 3 } }),
                                                            room("!inv", { { "state", "invited" } }) } } }));
        QCOMPARE(messages, 0);
        QCOMPARE(invitations, 0);
        QVERIFY(l.settings.allKeys().filter("invitesNotified").isEmpty());
        // an account's own list does notify (the test's own check)
        Lists own(dir("loud"), { QString(), QVariant(), QVariant() });
        int ownMessages = 0;
        QObject::connect(own.lists.get(), &RoomLists::newMessages, [&]() { ++ownMessages; });
        own.lists->setRooms({ room("!r", { { "notifications", 0 } }) });
        own.lists->setRooms({ room("!r", { { "notifications", 3 } }) });
        QCOMPARE(ownMessages, 1);
    }

    // The core sends a room's count and its newest message apart: a count
    // that rises while the newest message is still the one before waits for
    // it (the notification showed one's own last message, dodoradio), at
    // most kNotifyLatestWaitMs. Planted-bug: announce at once in announce().
    void notificationWaitsForTheNewMessage()
    {
        Lists l(dir("waits"), { QString(), QVariant(), QVariant() });
        QStringList heard;
        QObject::connect(l.lists.get(), &RoomLists::newMessages, [&](const QString &id, const QString &, int n) {
            heard << QStringLiteral("%1:%2:%3").arg(id).arg(n).arg(l.lists->roomInfo(id).value("latest_event_id").toString());
        });
        l.lists->setRooms({ room("!r", { { "notifications", 0 }, { "latest_event_id", "$own" } }) });
        l.lists->setRooms({ room("!r", { { "notifications", 1 }, { "latest_event_id", "$own" } }) });
        QCOMPARE(heard, QStringList());
        l.lists->setRooms({ room("!r", { { "notifications", 1 }, { "latest_event_id", "$new" } }) });
        QCOMPARE(heard, QStringList({ "!r:1:$new" }));
        // with the count and the message together: at once
        l.lists->setRooms({ room("!r", { { "notifications", 2 }, { "latest_event_id", "$next" } }) });
        QCOMPARE(heard.size(), 2);
        // a message that never comes: told after the wait
        l.lists->setRooms({ room("!r", { { "notifications", 3 }, { "latest_event_id", "$next" } }) });
        QCOMPARE(heard.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(heard.size(), 3, Keys::kNotifyLatestWaitMs + 1000);
    }
};

QTEST_GUILESS_MAIN(TestRoomSort)
#include "tst_roomsort.moc"
