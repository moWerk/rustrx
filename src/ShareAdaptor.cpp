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
#include "ShareAdaptor.h"
#include "Rustrx.h"
#include "Keys.h"

#include <QDBusArgument>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

ShareAdaptor::ShareAdaptor(Rustrx *owner) : QObject(owner), m_owner(owner)
{
    // copies already sent, or never: a share can wait across the handover
    // from the stand-in, so only old ones go
    QDir shared(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + QLatin1String(Keys::kSharedDir));
    const QDateTime before = QDateTime::currentDateTime().addSecs(-3600 * Keys::kSharedKeepHours);
    for (const QFileInfo &f : shared.entryInfoList(QDir::Files))
        if (f.lastModified() < before)
            QFile::remove(f.filePath());
}

// A path another app names is sent only when it is not one of this app's
// own files (the session with its tokens, the store, the settings): a
// caller that may not read them could otherwise have Rustrx send them.
bool ShareAdaptor::mayShare(const QString &path)
{
    const QString real = QFileInfo(path).canonicalFilePath();
    if (real.isEmpty())
        return false;
    for (auto where : { QStandardPaths::AppDataLocation, QStandardPaths::AppConfigLocation, QStandardPaths::CacheLocation }) {
        const QString own = QFileInfo(QStandardPaths::writableLocation(where)).canonicalFilePath();
        if (!own.isEmpty() && (real == own || real.startsWith(own + QLatin1Char('/'))))
            return false;
    }
    return true;
}

void ShareAdaptor::share(const QVariantMap &config)
{
    QStringList files;
    QStringList texts;
    const QVariantList resources = unwrap(config.value(QStringLiteral("resources"))).toList();
    qInfo() << "share:" << resources.size() << "resources";
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + QLatin1String(Keys::kSharedDir);
    int n = 0;
    for (const QVariant &raw : resources) {
        const QVariant r = unwrap(raw);
        qInfo() << "share: resource of type" << r.typeName();
        if (r.type() == QVariant::String || r.type() == QVariant::Url) {
            const QString path = Rustrx::localPath(r.toString());
            if (QFileInfo(path).isFile()) {
                if (mayShare(path))
                    files << path;
                else
                    qWarning() << "share: refused, a file of this app";
            } else {
                qInfo() << "share: not a readable file, sent as text";
                texts << path;   // a URL that is not a local file: send it as text
            }
            continue;
        }
        const QVariantMap m = r.toMap();
        const QString type = unwrap(m.value(QStringLiteral("type"))).toString();
        const QVariant rawData = unwrap(m.value(QStringLiteral("data")));
        // a file from a sandboxed app (the gallery, say) comes as an open
        // file descriptor, since this app may not read the other's folders
        if (rawData.userType() == qMetaTypeId<QDBusUnixFileDescriptor>()) {
            const QString path = copyFromDescriptor(rawData.value<QDBusUnixFileDescriptor>(), cache,
                    unwrap(m.value(QStringLiteral("name"))).toString(), ++n);
            if (!path.isEmpty())
                files << path;
            continue;
        }
        QByteArray data = rawData.type() == QVariant::String ? rawData.toString().toUtf8() : rawData.toByteArray();
        if (type.startsWith(QLatin1String("text/")))
            while (data.endsWith('\0'))   // C strings arrive with their terminator
                data.chop(1);
        if (type.startsWith(QLatin1String("text/")) || m.contains(QStringLiteral("linkTitle"))) {
            texts << QString::fromUtf8(data);
        } else if (!data.isEmpty()) {
            QDir().mkpath(cache);
            QString name = m.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                name = QStringLiteral("shared-%1").arg(++n);
            const QString path = cache + "/" + QFileInfo(name).fileName();
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
                files << path;
            }
        }
    }
    m_owner->receiveShare(files, texts.join(QStringLiteral("\n")));
}

QString ShareAdaptor::copyFromDescriptor(const QDBusUnixFileDescriptor &fd, const QString &cache,
                                         const QString &name, int n)
{
    if (!fd.isValid())
        return QString();
    QDir().mkpath(cache);
    const QString file = name.isEmpty() ? QStringLiteral("shared-%1").arg(n) : QFileInfo(name).fileName();
    const QString path = cache + "/" + file;
    QFile in;
    QFile out(path);
    // the descriptor stays owned by fd; QFile reads it without closing it
    if (!in.open(fd.fileDescriptor(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
            || !out.open(QIODevice::WriteOnly)) {
        qWarning() << "share: cannot copy a shared file";
        return QString();
    }
    in.seek(0);
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(Keys::kShareChunk);
        if (chunk.isEmpty() || out.write(chunk) != chunk.size())
            break;
        if (out.size() > Keys::kShareMaxBytes) {
            qWarning() << "share: larger than" << (Keys::kShareMaxBytes >> 20) << "MB, not taken";
            out.remove();
            return QString();
        }
    }
    qInfo() << "share: received" << out.size() << "bytes";
    return path;
}

QVariant ShareAdaptor::unwrap(const QVariant &v)
{
    if (v.userType() == qMetaTypeId<QDBusVariant>())
        return unwrap(v.value<QDBusVariant>().variant());
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::ArrayType) {
            if (arg.currentSignature() == QLatin1String("ay")) {
                QByteArray bytes;
                arg.beginArray();
                while (!arg.atEnd()) {
                    uchar b = 0;
                    arg >> b;
                    bytes.append(char(b));
                }
                arg.endArray();
                return bytes;
            }
            if (arg.currentSignature().startsWith(QLatin1String("a{"))) {
                QVariantMap map;
                arg >> map;
                return map;
            }
            QVariantList list;
            arg.beginArray();
            while (!arg.atEnd()) {
                QVariant item;
                arg >> item;
                list << unwrap(item);
            }
            arg.endArray();
            return list;
        }
        if (arg.currentType() == QDBusArgument::MapType) {
            QVariantMap map;
            arg >> map;
            return map;
        }
    }
    return v;
}
