/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "Rustrx.h"
#include "RoomModel.h"
#include "TimelineModel.h"
#include "rustrx_core.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

static Rustrx *s_instance = nullptr;

Rustrx::Rustrx(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_rooms(new RoomModel(this))
    , m_people(new RoomModel(this))
    , m_timeline(new TimelineModel(this))
{
    s_instance = this;
    QDir().mkpath(dataDir);
    const int rc = rustrx_start(dataDir.toUtf8().constData(), &Rustrx::eventCallback, this);
    if (rc != 0)
        qWarning() << "rustrx_start failed:" << rc;
}

QString Rustrx::coreVersion() const
{
    return QString::fromUtf8(rustrx_version());
}

// tokio thread: parse here, hand the map to the GUI thread
void Rustrx::eventCallback(void *ctx, const char *json)
{
    Rustrx *self = static_cast<Rustrx *>(ctx);
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    if (!doc.isObject())
        return;
    QMetaObject::invokeMethod(self, "handleEvent", Qt::QueuedConnection,
                              Q_ARG(QVariantMap, doc.object().toVariantMap()));
}

void Rustrx::command(const QVariantMap &cmd)
{
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(cmd)).toJson(QJsonDocument::Compact);
    const int rc = rustrx_command(json.constData());
    if (rc != 0)
        qWarning() << "rustrx_command" << cmd.value("cmd").toString() << "rc" << rc;
}

void Rustrx::loginPassword(const QString &user, const QString &password, const QString &homeserver)
{
    m_loggingIn = true;
    emit stateChanged();
    command({{"cmd", "login_password"}, {"user", user}, {"password", password}, {"homeserver", homeserver}});
}

void Rustrx::loginSso(const QString &user, const QString &homeserver, const QString &idp)
{
    m_loggingIn = true;
    emit stateChanged();
    QVariantMap cmd{{"cmd", "login_sso"}, {"user", user}, {"homeserver", homeserver}};
    if (!idp.isEmpty())
        cmd.insert("idp", idp);
    command(cmd);
}

void Rustrx::probeServer(const QString &user, const QString &homeserver)
{
    command({{"cmd", "probe_server"}, {"user", user}, {"homeserver", homeserver}});
}

void Rustrx::openRoom(const QString &roomId)
{
    if (m_openRoomId != roomId) {
        m_timeline->clear();
        m_openRoomId = roomId;
        emit openRoomChanged();
    }
    command({{"cmd", "open_room"}, {"room_id", roomId}});
}

void Rustrx::closeRoom()
{
    if (m_openRoomId.isEmpty())
        return;
    command({{"cmd", "close_room"}});
    m_openRoomId.clear();
    m_timeline->clear();
    emit openRoomChanged();
}

void Rustrx::sendText(const QString &roomId, const QString &body, const QString &replyTo)
{
    QVariantMap cmd{{"cmd", "send_text"}, {"room_id", roomId}, {"body", body}};
    if (!replyTo.isEmpty())
        cmd.insert("reply_to", replyTo);
    command(cmd);
}

void Rustrx::sendFile(const QString &roomId, const QString &path)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    command({{"cmd", "send_file"}, {"room_id", roomId}, {"path", p}});
}

void Rustrx::getMedia(const QString &roomId, const QString &eventId, bool thumb)
{
    command({{"cmd", "get_media"}, {"room_id", roomId}, {"event_id", eventId}, {"thumb", thumb}});
}

QVariantMap Rustrx::roomInfo(const QString &roomId) const
{
    return m_roomsById.value(roomId);
}

QString Rustrx::formatTime(const QVariant &ms) const
{
    return QDateTime::fromMSecsSinceEpoch(ms.toLongLong()).toString(QStringLiteral("HH:mm"));
}

QString Rustrx::formatDate(const QVariant &ms) const
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms.toLongLong()).date();
    const QDate today = QDate::currentDate();
    if (d == today)
        //: date divider in a chat
        //% "Today"
        return qtTrId("rustrx-la-today");
    if (d == today.addDays(-1))
        //% "Yesterday"
        return qtTrId("rustrx-la-yesterday");
    return d.toString(Qt::DefaultLocaleLongDate);
}

void Rustrx::distributeRooms(const QVariantList &rooms)
{
    QVariantList groups, dms;
    int unread = 0;
    m_roomsById.clear();
    for (const QVariant &v : rooms) {
        QVariantMap r = v.toMap();
        const QString id = r.value("room_id", r.value("id")).toString();
        r.insert("room_id", id);
        const bool invited = r.value("state").toString() == QLatin1String("invited");
        const bool fav = r.value("is_fav").toBool();
        //: list section for pending invitations
        //% "Invitations"
        //: list section for favourite rooms, pinned on top
        //% "Favourites"
        r.insert("section", invited ? qtTrId("rustrx-se-invitations") : fav ? qtTrId("rustrx-se-favourites") : QString());
        m_roomsById.insert(id, r);
        unread += r.value("notifications").toInt();
        if (r.value("is_dm").toBool())
            dms.append(r);
        else
            groups.append(r);
        // notifications while running: a room other than the open one gained notifications
        const int n = r.value("notifications").toInt();
        const int before = m_notificationCounts.value(id, -1);
        if (before >= 0 && n > before && id != m_openRoomId)
            emit newMessages(id, r.value("name").toString(), n - before);
        m_notificationCounts.insert(id, n);
    }
    m_rooms->setRooms(groups);
    m_people->setRooms(dms);
    if (unread != m_unreadTotal) {
        m_unreadTotal = unread;
        emit unreadChanged();
    }
}

void Rustrx::handleEvent(const QVariantMap &ev)
{
    const QString type = ev.value("ev").toString();
    if (type == QLatin1String("ready")) {
        m_ready = true;
        m_loggedIn = ev.value("logged_in").toBool();
        emit stateChanged();
    } else if (type == QLatin1String("logged_in")) {
        m_ready = true;
        m_loggedIn = true;
        m_loggingIn = false;
        m_userId = ev.value("user_id").toString();
        m_homeserver = ev.value("homeserver").toString();
        emit stateChanged();
    } else if (type == QLatin1String("login_error")) {
        m_loggingIn = false;
        emit stateChanged();
        emit loginError(ev.value("message").toString());
    } else if (type == QLatin1String("logged_out")) {
        m_loggedIn = false;
        m_userId.clear();
        m_openRoomId.clear();
        m_timeline->clear();
        m_rooms->setRooms({});
        m_people->setRooms({});
        m_notificationCounts.clear();
        emit stateChanged();
        emit openRoomChanged();
    } else if (type == QLatin1String("server_info")) {
        emit serverInfo(ev);
    } else if (type == QLatin1String("sync_state")) {
        m_syncState = ev.value("state").toString();
        emit syncStateChanged();
    } else if (type == QLatin1String("rooms")) {
        distributeRooms(ev.value("rooms").toList());
    } else if (type == QLatin1String("timeline")) {
        if (ev.value("room_id").toString() == m_openRoomId)
            m_timeline->setItems(ev.value("items").toList());
    } else if (type == QLatin1String("media")) {
        m_timeline->setMediaPath(ev.value("event_id").toString(), ev.value("thumb").toBool(),
                                 ev.value("path").toString());
    } else if (type == QLatin1String("open_url")) {
        emit openUrl(ev.value("url").toString());
    } else if (type == QLatin1String("error")) {
        const QString cmd = ev.value("cmd").toString();
        if (cmd == QLatin1String("login_password") || cmd == QLatin1String("login_sso")) {
            m_loggingIn = false;
            emit stateChanged();
            emit loginError(ev.value("message").toString());
        } else {
            emit errorOccurred(cmd, ev.value("message").toString());
        }
    } else if (type == QLatin1String("verification")) {
        emit verification(ev);
    } else if (type == QLatin1String("sas")) {
        emit sas(ev);
    } else if (type == QLatin1String("verification_state")) {
        m_verificationState = ev.value("state").toString();
        emit encryptionChanged();
    } else if (type == QLatin1String("recovery_state")) {
        m_recoveryState = ev.value("state").toString();
        emit encryptionChanged();
    } else if (type == QLatin1String("recovery_key")) {
        emit recoveryKey(ev.value("key").toString());
    } else if (type == QLatin1String("recovery_done")) {
        emit recoveryDone();
    } else if (type == QLatin1String("members")) {
        emit members(ev.value("room_id").toString(), ev.value("members").toList());
    } else if (type == QLatin1String("devices")) {
        emit devices(ev.value("devices").toList());
    } else if (type == QLatin1String("device_deleted")) {
        emit deviceDeleted(ev.value("device_id").toString());
    } else if (type == QLatin1String("profile")) {
        emit profile(ev);
    } else if (type == QLatin1String("dm_ready")) {
        emit dmReady(ev.value("room_id").toString(), ev.value("user_id").toString());
    } else if (type == QLatin1String("joined")) {
        emit joined(ev.value("room_id").toString());
    } else if (type == QLatin1String("paginated")) {
        emit paginated(ev.value("room_id").toString(), ev.value("at_start").toBool());
    }
}
