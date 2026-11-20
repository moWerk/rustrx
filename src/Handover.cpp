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
#include "Handover.h"
#include "Keys.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

static QString handoverPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1Char('/') + QLatin1String(Keys::kHandoverFile);
}

void Handover::setPendingShare(const QStringList &files, const QString &text)
{
    m_pendingShare = QVariantMap{{"files", files}, {"text", text}};
}

int Handover::takePendingAccount()
{
    const int a = m_pendingAccount;
    m_pendingAccount = 0;
    return a;
}

QString Handover::takePendingRoom()
{
    const QString r = m_pendingRoom;
    m_pendingRoom.clear();
    return r;
}

QString Handover::takePendingCall()
{
    const QString r = m_pendingCall;
    m_pendingCall.clear();
    return r;
}

bool Handover::takePendingKnocks()
{
    const bool r = m_pendingKnocks;
    m_pendingKnocks = false;
    return r;
}

QString Handover::takePendingUri()
{
    const QString r = m_pendingUri;
    m_pendingUri.clear();
    return r;
}

QVariantMap Handover::takePendingVoip()
{
    const QVariantMap r = m_pendingVoip;
    m_pendingVoip.clear();
    return r;
}

QVariantMap Handover::takePendingShare()
{
    const QVariantMap r = m_pendingShare;
    m_pendingShare.clear();
    return r;
}

bool Handover::startUnit(const QString &unit)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(Keys::kSystemdService),
            QString::fromLatin1(Keys::kSystemdPath), QString::fromLatin1(Keys::kSystemdManager),
            QStringLiteral("StartUnit"));
    call << unit << QStringLiteral("replace");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, Keys::kStartUnitTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "starting" << unit << "failed:" << reply.errorMessage();
        return false;
    }
    return true;
}

// The new process opens the store only when this one is gone (it waits for
// the bus name), so what it should show goes through a file.
bool Handover::handOver()
{
    if (!m_standIn && !m_pushWake)
        return false;
    QJsonObject o;
    o.insert("room", m_pendingRoom);
    o.insert("account", m_pendingAccount);
    o.insert("call", m_pendingCall);
    o.insert("knocks", m_pendingKnocks);
    o.insert("uri", m_pendingUri);
    o.insert("share", QJsonObject::fromVariantMap(m_pendingShare));
    o.insert("voip", QJsonObject::fromVariantMap(m_pendingVoip));
    QFile f(handoverPath());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) < 0)
        qWarning() << "stand-in: cannot write" << f.fileName() << f.errorString();
    f.close();
    qInfo() << "stand-in: handing over to the app";
    // the app could not be started: this one stays and keeps notifying,
    // and the request is dropped rather than shown by a later start
    if (!startUnit(QString::fromLatin1(Keys::kOpenUnit))) {
        f.remove();
        return true;
    }
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

void Handover::takeHandover()
{
    QFile f(handoverPath());
    // written by the stand-in seconds before this start; an older one was
    // left over, or put there by someone else (what it asks for, a call
    // answered or a file shared, is not taken from a stale file)
    const QFileInfo info(f);
    if (info.exists() && info.lastModified().msecsTo(QDateTime::currentDateTime()) > Keys::kHandoverMaxAgeMs) {
        qWarning() << "stand-in: handover file too old, ignored";
        f.remove();
        return;
    }
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray json = f.read(Keys::kHandoverMaxBytes + 1);
    f.close();
    f.remove();
    if (!accept(json, QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + QLatin1String(Keys::kSharedDir)))
        qWarning() << "stand-in: handover file not as the stand-in writes it, ignored";
}

// ---- what handover.json may hold -------------------------------------------

namespace {

// an id from the protocol as it is passed on (a call or party id): not
// empty, within the spec's 255 bytes, no spaces or control characters
bool isOpaque(const QJsonValue &v)
{
    if (!v.isString())
        return false;
    const QString s = v.toString();
    if (s.isEmpty() || s.toUtf8().size() > Keys::kHandoverMaxIdBytes)
        return false;
    for (const QChar c : s)
        if (c.isSpace() || c.category() == QChar::Other_Control)
            return false;
    return true;
}

// "!opaque:server", or "!opaque" alone from room version 12 on
bool isRoomId(const QJsonValue &v)
{
    return isOpaque(v) && v.toString().size() > 1 && v.toString().startsWith(QLatin1Char('!'));
}

bool isRoomOrNone(const QJsonValue &v)
{
    return (v.isString() && v.toString().isEmpty()) || isRoomId(v);
}

bool isText(const QJsonValue &v, int maxBytes)
{
    return v.isString() && v.toString().toUtf8().size() <= maxBytes;
}

bool onlyKeys(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : o.keys())
        if (!keys.contains(k))
            return false;
    return true;
}

// a share as ShareAdaptor makes it: its files are copies in our cache's
// shared folder (ShareCopy.h), never a path elsewhere
bool isShare(const QJsonValue &v, const QString &sharedDir)
{
    if (!v.isObject())
        return false;
    const QJsonObject o = v.toObject();
    if (!onlyKeys(o, { "files", "text" }))
        return false;
    if (o.contains("text") && !isText(o.value("text"), Keys::kHandoverMaxTextBytes))
        return false;
    if (!o.contains("files"))
        return true;
    if (!o.value("files").isArray() || o.value("files").toArray().size() > Keys::kHandoverMaxFiles)
        return false;
    const QString dir = QDir::cleanPath(sharedDir);
    for (const QJsonValue &f : o.value("files").toArray()) {
        if (!isText(f, Keys::kHandoverMaxTextBytes))
            return false;
        const QString path = f.toString();
        const QFileInfo info(path);
        if (path != QDir::cleanPath(path) || info.path() != dir || info.fileName().isEmpty()
                || info.fileName() == QLatin1String("..") || info.isSymLink())
            return false;
    }
    return true;
}

// a ringing call as Voip::ringing() makes it, with "answer" from the
// notification's Answer
bool isRinging(const QJsonValue &v)
{
    if (!v.isObject())
        return false;
    const QJsonObject o = v.toObject();
    if (o.isEmpty())
        return true;   // no call
    if (!onlyKeys(o, { "room_id", "call_id", "party_id", "version", "offer", "peer_name", "peer_user", "expires_at", "answer" }))
        return false;
    const QJsonValue party = o.value("party_id");
    const QJsonValue version = o.value("version");
    const QJsonValue expires = o.value("expires_at");
    const qint64 latest = QDateTime::currentMSecsSinceEpoch() + Keys::kVoipInviteLifetimeMaxMs + Keys::kHandoverRingSlackMs;
    return isRoomId(o.value("room_id"))
            && isOpaque(o.value("call_id"))
            // a version 0 invite has no party
            && (party.isUndefined() || (party.isString() && party.toString().isEmpty()) || isOpaque(party))
            && ((version.isDouble() && version.toDouble() == 0) || (isOpaque(version) && version.toString().size() <= Keys::kHandoverMaxVersionChars))
            && isText(o.value("offer"), Keys::kHandoverMaxTextBytes)
            && (o.value("peer_name").isUndefined() || isText(o.value("peer_name"), Keys::kHandoverMaxNameBytes))
            && (o.value("peer_user").isUndefined() || o.value("peer_user").toString() == QString()
                || isOpaque(o.value("peer_user")))
            && expires.isDouble() && expires.toDouble() > 0 && expires.toDouble() <= double(latest)
            && (o.value("answer").isUndefined() || o.value("answer").isBool());
}

bool isUri(const QJsonValue &v)
{
    if (!isText(v, Keys::kHandoverMaxUriBytes))
        return false;
    const QString uri = v.toString();
    return uri.isEmpty() || uri.startsWith(QLatin1String("matrix:"))
            || uri.startsWith(QLatin1String("https://matrix.to/")) || uri.startsWith(QLatin1String("http://matrix.to/"));
}

}

bool Handover::accept(const QByteArray &json, const QString &sharedDir)
{
    if (json.size() > Keys::kHandoverMaxBytes)
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    // every field as handOver() writes it; each one checked, all or nothing
    // "account" since 0.7; without it (an older stand-in's file) the first
    const bool hasAccount = o.contains("account");
    if (!onlyKeys(o, { "room", "account", "call", "knocks", "uri", "share", "voip" }) || o.size() != (hasAccount ? 7 : 6))
        return false;
    const double account = hasAccount ? o.value("account").toDouble(-1) : 0;
    if (account < 0 || account > 9999 || account != int(account))
        return false;
    if (!isRoomOrNone(o.value("room")) || !isRoomOrNone(o.value("call")) || !o.value("knocks").isBool()
            || !isUri(o.value("uri")) || !isShare(o.value("share"), sharedDir) || !isRinging(o.value("voip")))
        return false;
    m_pendingRoom = o.value("room").toString();
    m_pendingAccount = int(account);
    m_pendingCall = o.value("call").toString();
    m_pendingKnocks = o.value("knocks").toBool();
    m_pendingUri = o.value("uri").toString();
    m_pendingShare = o.value("share").toObject().toVariantMap();
    m_pendingVoip = o.value("voip").toObject().toVariantMap();
    return true;
}

QString Handover::backgroundFlagPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1Char('/') + QLatin1String(Keys::kBackgroundFlag);
}

bool Handover::background() const
{
    return QFile::exists(backgroundFlagPath());
}

void Handover::setBackground(bool on)
{
    if (on) {
        QDir().mkpath(QFileInfo(backgroundFlagPath()).path());
        QFile f(backgroundFlagPath());
        if (!f.open(QIODevice::WriteOnly))
            qWarning() << "background: cannot write" << f.fileName() << f.errorString();
    } else {
        QFile::remove(backgroundFlagPath());
    }
}
