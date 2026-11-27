// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_NOTIFYGATE_H
#define RUSTRX_NOTIFYGATE_H

#include <QByteArray>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Who may use a notification's actions over D-Bus (audit 0.8.4 IPC1).
//
// The notification server does not call Rustrx itself: an action arrives
// from a short-lived helper process of the user, with a bus connection of
// its own, which the sandbox cannot tell from any other process of the
// user. So an action is taken (1) from a caller running as
// this process's Unix user, asked of the bus at every call (other users
// are refused), and (2) only for a notification Rustrx posted and has not
// closed, the action matching it: Posted, a small file in the data folder,
// so the app, the stand-in and a push-woken process (started by the tap
// itself, when lipstick may have dropped the notification already) agree.
// (2) is the real barrier: any app of the user can still take an action of
// a notification that is listed as up, e.g. answer a ringing call. The
// actions' arguments are not secret, so a token in them would not help.
// Calls from inside the process (QML) are not D-Bus calls and pass.
namespace NotifyGate {

// in the data folder
static const char kPostedFile[] = "notified.json";
static const char kSecretFile[] = "bus-secret";
// room lines kept at the most; the oldest go first
static const int kMaxRooms = 256;
// the stand-in's line still counts as up this long after it closed:
// lipstick closes a line as one of its buttons is tapped, before the
// helper process it starts for the tap has called Quit
static const qint64 kQuitGraceMs = 5000;

// whether this process sees the host's process ids (sailjail's firejail
// gives it a PID namespace of its own: the bus's ids are not in its /proc)
inline bool hostPids()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith("NSpid:"))
            return line.mid(6).simplified().split(' ').size() == 1;
    return true;
}

// a caller for the log: its unique name, process id and, where this process
// can see it, the process's name and program
inline QString describe(QDBusConnectionInterface *bus, const QString &name)
{
    if (!bus)
        return name;
    const QDBusReply<uint> reply = bus->servicePid(name);
    const uint pid = reply.isValid() ? reply.value() : 0;
    QString d = name + QStringLiteral(" pid ") + (pid ? QString::number(pid) : QStringLiteral("? (") + reply.error().name() + QLatin1Char(')'));
    if (!pid)
        return d;
    if (!hostPids())
        return d + QStringLiteral(" (not visible in the sandbox)");
    const QString dir = QStringLiteral("/proc/") + QString::number(pid);
    QFile comm(dir + QStringLiteral("/comm"));
    if (comm.open(QIODevice::ReadOnly))
        d += QStringLiteral(" comm ") + QString::fromLocal8Bit(comm.read(64).trimmed());
    const QString exe = QFile::symLinkTarget(dir + QStringLiteral("/exe"));
    if (!exe.isEmpty())
        d += QStringLiteral(" exe ") + exe;
    QFile cmdline(dir + QStringLiteral("/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly)) {
        QByteArray c = cmdline.read(256);
        c.replace('\0', ' ');
        d += QStringLiteral(" cmdline ") + QString::fromLocal8Bit(c.trimmed());
    }
    return d;
}

// whether a caller's uid, as the bus told it (valid: the bus answered), is
// this process's
inline bool sameUser(bool valid, uint callerUid, uint ownUid)
{
    return valid && callerUid == ownUid;
}

// true for a call from a process of this user, or not over D-Bus at all
inline bool fromUser(const QDBusContext &ctx, const char *what)
{
    if (!ctx.calledFromDBus())
        return true;
    const QString sender = ctx.message().service();
    QDBusConnectionInterface *bus = ctx.connection().interface();
    const QDBusReply<uint> uid = bus ? bus->serviceUid(sender) : QDBusReply<uint>();
    if (sameUser(uid.isValid(), uid.value(), uint(::getuid()))) {
        qInfo().noquote() << "notification:" << what << "from" << sender << "(this user)";
        return true;
    }
    qWarning().noquote() << "notification:" << what << "refused: from" << describe(bus, sender) << "uid"
                         << (uid.isValid() ? QString::number(uid.value()) : QStringLiteral("? (") + uid.error().name() + QLatin1Char(')'))
                         << "not this user" << ::getuid();
    return false;
}

// The stand-in's own line, whose Quit ends it: up from its post until a
// moment (kQuitGraceMs) after it closed (Service.qml tells both). Kept in
// memory, not in Posted: only the process that posted the line is asked,
// and a killed one's line is gone with it.
class StandInLine
{
public:
    void posted()
    {
        m_up = true;
        m_closedAt = -1;
    }
    void closed(qint64 nowMs)
    {
        if (m_up)
            m_closedAt = nowMs;
        m_up = false;
    }
    bool up(qint64 nowMs) const { return m_up || (m_closedAt >= 0 && nowMs - m_closedAt <= kQuitGraceMs); }

private:
    bool m_up = false;
    qint64 m_closedAt = -1;
};

// Quit, the stand-in's line's button: from this user, and only in the
// stand-in while that line is up (StandInLine; with "Show the background
// line" off there is none, and no other app can end the stand-in). Calls
// from inside the process pass.
inline bool mayQuit(const QDBusContext &ctx, bool standIn, bool lineUp)
{
    if (!fromUser(ctx, "quit"))
        return false;
    if (ctx.calledFromDBus() && !(standIn && lineUp)) {
        qInfo() << (standIn ? "notification: quit with the background line not up, ignored"
                            : "notification: quit outside the stand-in, ignored");
        return false;
    }
    return true;
}

// The notifications Rustrx has up: room lines (their latest event and the
// emoji their buttons offer) and rings (the call's event, until when it
// rings). Read from and written to the file at every use: only one Rustrx
// process runs at a time, and the next one finds what the last one left.
class Posted
{
public:
    explicit Posted(const QString &path = QString()) : m_path(path) {}
    void setPath(const QString &path) { m_path = path; }
    // the file, made empty where there is none
    void create() const
    {
        if (!QFile::exists(m_path))
            write(QJsonObject());
    }

    void roomPosted(int account, const QString &roomId, const QString &eventId, const QStringList &keys)
    {
        QJsonObject all = read();
        QJsonObject rooms = all.value(QStringLiteral("rooms")).toObject();
        QJsonObject r;
        r.insert(QStringLiteral("event"), eventId);
        r.insert(QStringLiteral("keys"), QJsonArray::fromStringList(keys));
        r.insert(QStringLiteral("t"), double(now()));
        rooms.insert(key(account, roomId), r);
        while (rooms.size() > kMaxRooms) {
            QString oldest;
            double t = 0;
            for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it) {
                const double at = it.value().toObject().value(QStringLiteral("t")).toDouble();
                if (oldest.isEmpty() || at < t) {
                    oldest = it.key();
                    t = at;
                }
            }
            rooms.remove(oldest);
        }
        all.insert(QStringLiteral("rooms"), rooms);
        write(all);
    }
    void roomClosed(int account, const QString &roomId) { remove(QStringLiteral("rooms"), key(account, roomId)); }
    void ringPosted(int account, const QString &roomId, const QString &eventId, qint64 forMs)
    {
        QJsonObject all = read();
        QJsonObject rings = all.value(QStringLiteral("rings")).toObject();
        // ended ones that nobody closed (their process went)
        for (const QString &k : rings.keys())
            if (rings.value(k).toObject().value(QStringLiteral("until")).toDouble() < now())
                rings.remove(k);
        QJsonObject r;
        r.insert(QStringLiteral("event"), eventId);
        r.insert(QStringLiteral("until"), double(now() + forMs));
        rings.insert(key(account, roomId), r);
        all.insert(QStringLiteral("rings"), rings);
        write(all);
    }
    void ringEnded(int account, const QString &roomId) { remove(QStringLiteral("rings"), key(account, roomId)); }

    // Reply and Mark as read: the room has a line
    bool hasRoom(int account, const QString &roomId) const
    {
        return read().value(QStringLiteral("rooms")).toObject().contains(key(account, roomId));
    }
    // an emoji button: the line's event and one of its emoji
    bool mayReact(int account, const QString &roomId, const QString &eventId, const QString &emoji) const
    {
        const QJsonValue v = read().value(QStringLiteral("rooms")).toObject().value(key(account, roomId));
        if (!v.isObject() || eventId.isEmpty())
            return false;
        const QJsonObject r = v.toObject();
        return r.value(QStringLiteral("event")).toString() == eventId
                && r.value(QStringLiteral("keys")).toArray().contains(QJsonValue(emoji));
    }
    // Answer: the room rings; Decline also names the ring's event
    bool rings(int account, const QString &roomId) const
    {
        const QJsonValue v = read().value(QStringLiteral("rings")).toObject().value(key(account, roomId));
        return v.isObject() && v.toObject().value(QStringLiteral("until")).toDouble() >= now();
    }
    // how long the room still rings, 0 when it does not: a ring an earlier
    // process posted ends as one of this process's own (Service.qml)
    qint64 ringLeft(int account, const QString &roomId) const
    {
        const QJsonValue v = read().value(QStringLiteral("rings")).toObject().value(key(account, roomId));
        return v.isObject() ? qMax(qint64(0), qint64(v.toObject().value(QStringLiteral("until")).toDouble()) - now()) : 0;
    }
    bool mayDecline(int account, const QString &roomId, const QString &eventId) const
    {
        return rings(account, roomId)
                && read().value(QStringLiteral("rings")).toObject().value(key(account, roomId))
                           .toObject().value(QStringLiteral("event")).toString() == eventId;
    }

private:
    static QString key(int account, const QString &roomId) { return QString::number(account) + QLatin1Char('/') + roomId; }
    static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
    QJsonObject read() const
    {
        QFile f(m_path);
        if (m_path.isEmpty() || !f.open(QIODevice::ReadOnly))
            return QJsonObject();
        // a few hundred lines at the most
        return QJsonDocument::fromJson(f.read(1 << 20)).object();
    }
    void write(const QJsonObject &all) const
    {
        if (m_path.isEmpty())
            return;
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning() << "notification: list of posted notifications not written";
            return;
        }
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        f.commit();
    }
    void remove(const QString &group, const QString &k) const
    {
        QJsonObject all = read();
        QJsonObject g = all.value(group).toObject();
        if (!g.contains(k))
            return;
        g.remove(k);
        all.insert(group, g);
        write(all);
    }
    QString m_path;
};

// What a notification's Answer does (IPC1c): a ring that is up is
// answered; one that is over (an old notification's Answer, or a copy of
// one) only opens its room, the author's choice for 0.8.4, and never
// starts a call there. From inside the process (QML) it is always taken.
enum class Answer { Call, OpenRoom };
inline Answer answerFor(bool fromBus, bool ringing)
{
    return !fromBus || ringing ? Answer::Call : Answer::OpenRoom;
}

// A random secret in a 0600 file in the data folder, made once: only a
// process in Rustrx's sandbox (or an unsandboxed one of the user, which
// could do anything anyway) can read it. A second launch hands it to the
// stand-in, which leaves only for it (IPC1 d). "" when it cannot be made.
inline QString secret(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray s = f.read(128).trimmed();
        if (s.size() >= 32)
            return QString::fromLatin1(s);
        f.close();
    }
    QByteArray raw(32, 0);
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly) || random.read(raw.data(), raw.size()) != raw.size())
        return QString();
    const QByteArray hex = raw.toHex();
    // a half-written or short file is replaced; a race with another start
    // leaves one of the two, which both read back
    const QByteArray tmp = QFile::encodeName(path + QLatin1String(".new"));
    ::unlink(tmp.constData());
    const int fd = ::open(tmp.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return QString();
    const bool ok = ::write(fd, hex.constData(), size_t(hex.size())) == hex.size();
    ::close(fd);
    if (!ok || ::rename(tmp.constData(), QFile::encodeName(path).constData()) != 0) {
        ::unlink(tmp.constData());
        return QString();
    }
    QFile back(path);
    return back.open(QIODevice::ReadOnly) ? QString::fromLatin1(back.read(128).trimmed()) : QString();
}

// equal without telling how much of it was right
inline bool sameSecret(const QString &a, const QString &b)
{
    if (a.isEmpty() || a.size() != b.size())
        return false;
    ushort diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= a.at(i).unicode() ^ b.at(i).unicode();
    return diff == 0;
}

} // namespace NotifyGate

#endif
