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
#include "ShareCopy.h"

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
    // from the stand-in, so only old ones go. Each copy has a folder of its
    // own (ShareCopy::freshDir), which goes as a whole; loose files are from
    // before 0.8.4
    QDir shared(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + QLatin1String(Keys::kSharedDir));
    const QDateTime before = QDateTime::currentDateTime().addSecs(-3600 * Keys::kSharedKeepHours);
    for (const QFileInfo &f : shared.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden)) {
        if (f.lastModified() >= before)
            continue;
        if (f.isDir() && !f.isSymLink())
            QDir(f.filePath()).removeRecursively();
        else
            QFile::remove(f.filePath());
    }
}

// A path another app names is taken (copied, ShareCopy.h) only when it is
// not one of this app's own files (the session with its tokens, the store,
// the settings): a caller that may not read them could otherwise have
// Rustrx send them.
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
                // copied now, the copy is sent (ShareCopy.h)
                const QString dir = ShareCopy::freshDir(cache);
                const QString copy = dir.isEmpty() ? QString()
                        : ShareCopy::fromPath(path, dir, QStringLiteral("shared-%1").arg(++n),
                                              Keys::kShareMaxBytes, Keys::kShareChunk, &ShareAdaptor::mayShare);
                if (!copy.isEmpty())
                    files << copy;
                else if (!dir.isEmpty())
                    QDir().rmdir(dir);
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
            const QString dir = ShareCopy::freshDir(cache);
            const QString path = dir.isEmpty() ? QString()
                    : copyFromDescriptor(rawData.value<QDBusUnixFileDescriptor>(), dir,
                                         unwrap(m.value(QStringLiteral("name"))).toString(), ++n);
            if (!path.isEmpty())
                files << path;
            else if (!dir.isEmpty())
                QDir().rmdir(dir);
            continue;
        }
        QByteArray data = rawData.type() == QVariant::String ? rawData.toString().toUtf8() : rawData.toByteArray();
        // the same cap as a copied file (unwrap stops reading just past it)
        if (data.size() > Keys::kShareMaxBytes) {
            qWarning() << "share: larger than" << (Keys::kShareMaxBytes >> 20) << "MB, not taken";
            continue;
        }
        if (type.startsWith(QLatin1String("text/")))
            while (data.endsWith('\0'))   // C strings arrive with their terminator
                data.chop(1);
        if (type.startsWith(QLatin1String("text/")) || m.contains(QStringLiteral("linkTitle"))) {
            texts << QString::fromUtf8(data);
        } else if (!data.isEmpty()) {
            // a new file in a folder of its own, as the copies (ShareCopy.h)
            const QString name = m.value(QStringLiteral("name")).toString();
            const QString dir = ShareCopy::freshDir(cache);
            const QString path = dir.isEmpty() ? QString()
                    : ShareCopy::fromBytes(data, dir, name, QStringLiteral("shared-%1").arg(name.isEmpty() ? ++n : n + 1),
                                           Keys::kShareMaxBytes);
            if (!path.isEmpty())
                files << path;
            else if (!dir.isEmpty())
                QDir().rmdir(dir);
        }
    }
    m_owner->receiveShare(files, texts.join(QStringLiteral("\n")));
}

QString ShareAdaptor::copyFromDescriptor(const QDBusUnixFileDescriptor &fd, const QString &cache,
                                         const QString &name, int n)
{
    if (!fd.isValid())
        return QString();
    // the descriptor stays owned by fd
    return ShareCopy::fromDescriptor(fd.fileDescriptor(), cache, name, QStringLiteral("shared-%1").arg(n),
                                     Keys::kShareMaxBytes, Keys::kShareChunk);
}

QVariant ShareAdaptor::unwrap(const QVariant &v)
{
    if (v.userType() == qMetaTypeId<QDBusVariant>())
        return unwrap(v.value<QDBusVariant>().variant());
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::ArrayType) {
            if (arg.currentSignature() == QLatin1String("ay")) {
                // the bus takes up to a GB: reading stops just past the
                // share's cap, and share() refuses what got there
                QByteArray bytes;
                arg.beginArray();
                while (!arg.atEnd() && bytes.size() <= Keys::kShareMaxBytes) {
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
