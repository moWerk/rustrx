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
#include <QFileInfo>
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
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    f.remove();
    m_pendingRoom = o.value("room").toString();
    m_pendingCall = o.value("call").toString();
    m_pendingKnocks = o.value("knocks").toBool();
    m_pendingUri = o.value("uri").toString();
    m_pendingShare = o.value("share").toObject().toVariantMap();
    m_pendingVoip = o.value("voip").toObject().toVariantMap();
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
