// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_DEVHOOK_H
#define RUSTRX_DEVHOOK_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The dev D-Bus hook (Rustrx::dev, answered only while the "dev-commands"
// file is in the data folder) takes exactly the requests the test tooling
// sends (tools/test/*.sh and the call tests): nothing else, so a new core
// command is never reachable through it by accident (audit S4; before, a
// refused list missed send_file, get_media and others).
//
// One kind per request, each with the keys it may carry:
//   {"quit": true}                       a clean exit before an install
//   {"state": true}, {"rooms": true}     what the app shows, read only
//   {"ui": activate|open_room|settings|back|verify_notice|show_account|add_account, "room_id", "tab", "account"}
//   {"login": "password", "user", "password", "homeserver"}   a login on the shown account (tests with a local server)
//   {"voip": place|answer|hangup|status, "room_id", "invitee", "tone", "aec", "speaker"}
//   {"cmd": call_start, "room_id", "voice", "skip_lobby"}, {"cmd": refresh_rooms}
namespace DevHook {

// why the request is refused, or "" when it is one of those
inline QString refusal(const QVariantMap &req)
{
    struct Kind { const char *key; QStringList values; QStringList keys; };
    static const QList<Kind> kinds{
        { "quit", {}, {} },
        { "state", {}, {} },
        { "rooms", {}, {} },
        { "ui", { "activate", "open_room", "settings", "back", "verify_notice", "show_account", "add_account" }, { "room_id", "tab", "account" } },
        { "login", { "password" }, { "user", "password", "homeserver" } },
        { "voip", { "place", "answer", "hangup", "status" }, { "room_id", "invitee", "tone", "aec", "speaker" } },
    };
    // core commands, each with its own keys
    static const QMap<QString, QStringList> commands{
        { "call_start", { "room_id", "voice", "skip_lobby" } },
        { "refresh_rooms", {} },
    };
    QStringList found;
    for (const QString &k : req.keys()) {
        bool isKind = k == QLatin1String("cmd");
        for (const Kind &d : kinds)
            isKind = isKind || k == QLatin1String(d.key);
        if (isKind)
            found << k;
    }
    if (found.size() != 1)
        return found.isEmpty() ? QStringLiteral("unknown request") : QStringLiteral("more than one request");
    const QString kind = found.first();
    QStringList keys;
    if (kind == QLatin1String("cmd")) {
        const QString cmd = req.value(kind).toString();
        if (!commands.contains(cmd))
            return QStringLiteral("not a dev command: ") + cmd;
        keys = commands.value(cmd);
    } else {
        for (const Kind &d : kinds) {
            if (kind != QLatin1String(d.key))
                continue;
            if (!d.values.isEmpty() && !d.values.contains(req.value(kind).toString()))
                return QStringLiteral("not a dev request: ") + kind + QLatin1Char(' ') + req.value(kind).toString();
            keys = d.keys;
        }
    }
    for (const QString &k : req.keys())
        if (k != kind && !keys.contains(k))
            return QStringLiteral("not a key of ") + kind + QStringLiteral(": ") + k;
    return QString();
}

}

#endif
