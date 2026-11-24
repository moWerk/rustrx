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
#include "DebugLog.h"
#include "Keys.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

static QtMessageHandler s_previous = nullptr;
static QAtomicInt s_enabled(1);
static QString s_path;
static QMutex s_lock(QMutex::Recursive);

static QString logDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + QLatin1String(Keys::kLogDir);
}

static void appendLine(QtMsgType type, const QString &message)
{
    static const char levels[] = { 'D', 'W', 'C', 'F', 'I' };
    const char level = type >= 0 && type < int(sizeof levels) ? levels[type] : '?';
    const QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")).toUtf8()
            + ' ' + level + ' ' + message.toUtf8() + '\n';
    QMutexLocker locker(&s_lock);
    // one file kept open (an open, a stat and a close per line woke the
    // disk for every line); its size looked at every 50 lines: at most
    // about 1 MB, then it becomes the older file and a new one starts
    static QFile *file = nullptr;
    static int lines = 0;
    if (file && file->fileName() != s_path) {
        delete file;
        file = nullptr;
    }
    if (file && ++lines >= Keys::kLogCheckEveryLines) {
        lines = 0;
        if (file->size() > Keys::kLogMaxSize) {
            file->close();
            QFile::remove(s_path + QStringLiteral(".1"));
            QFile::rename(s_path, s_path + QStringLiteral(".1"));
            delete file;
            file = nullptr;
        }
    }
    if (!file) {
        file = new QFile(s_path);
        if (!file->open(QIODevice::Append | QIODevice::WriteOnly)) {
            delete file;
            file = nullptr;
            return;
        }
    }
    file->write(line);
    file->flush();
}

QString DebugLog::redact(const QString &line)
{
    // most lines have none of it: no expression runs for them
    const bool url = line.contains(QLatin1String("://"));
    const bool token = line.contains(QLatin1String("token"), Qt::CaseInsensitive);
    const bool bearer = line.contains(QLatin1String("Bearer"), Qt::CaseInsensitive);
    if (!url && !token && !bearer)
        return line;
    QString out = line;
    if (url) {
        // user:password@ (a TURN server's, as Voip hands it to webrtcbin)
        static const QRegularExpression userinfo(QStringLiteral("\\b([a-z][a-z0-9+.-]*://)(?!\\[cut\\]@)[^\\s/?#@\"'<>]+@"),
                                                 QRegularExpression::CaseInsensitiveOption);
        out.replace(userinfo, QStringLiteral("\\1[cut]@"));
        // ?query, to the end of the address
        static const QRegularExpression query(QStringLiteral("\\b([a-z][a-z0-9+.-]*://[^\\s?#\"'<>]*)\\?(?!\\[cut\\])[^\\s#\"'<>)]*"),
                                              QRegularExpression::CaseInsensitiveOption);
        out.replace(query, QStringLiteral("\\1?[cut]"));
    }
    if (token) {
        // access_token=…, "refresh_token": "…", token: …
        static const QRegularExpression value(QStringLiteral("(token[\"']?\\s*[:=]\\s*[\"']?)(?!\\[cut\\])[^\\s\"'&,;)}\\]]+"),
                                              QRegularExpression::CaseInsensitiveOption);
        out.replace(value, QStringLiteral("\\1[cut]"));
    }
    if (bearer) {
        static const QRegularExpression auth(QStringLiteral("\\b(Bearer\\s+)(?!\\[cut\\])[^\\s\"',;)}\\]]+"),
                                             QRegularExpression::CaseInsensitiveOption);
        out.replace(auth, QStringLiteral("\\1[cut]"));
    }
    return out;
}

static void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // the file and the system log alike
    const QString line = DebugLog::redact(message);
    // the file first: the system log's handler aborts on a fatal message
    if (s_enabled.load() && !s_path.isEmpty())
        appendLine(type, line);
    if (s_previous)
        s_previous(type, context, line);
}

void DebugLog::install(const QString &name)
{
    QDir().mkpath(logDir());
    s_path = logDir() + QLatin1Char('/') + name + QStringLiteral(".log");
    s_previous = qInstallMessageHandler(handler);
}

void DebugLog::setEnabled(bool on)
{
    s_enabled.store(on ? 1 : 0);
}

bool DebugLog::enabled()
{
    return s_enabled.load() != 0;
}

int DebugLog::clear(const QString &dir)
{
    QMutexLocker locker(&s_lock);
    for (const QString &name : { QString::fromLatin1(Keys::kLogApp), QString::fromLatin1(Keys::kLogBackground),
                                 QString::fromLatin1(Keys::kLogPush) }) {
        const QString base = logDir() + QLatin1Char('/') + name;
        QFile::remove(base + QStringLiteral(".log.1"));
        // emptied, not removed: the file a process has open is in append
        // mode and goes on at its new end
        QFile::resize(base + QStringLiteral(".log"), 0);
    }
    // the copies in Downloads (and in its Rustrx folder, where 0.4.7 put them)
    int removed = 0;
    const QStringList pattern{ QStringLiteral("rustrx-log-*.txt") };
    for (const QString &d : { dir, dir + QLatin1Char('/') + QLatin1String(Keys::kDownloadDir) }) {
        QDir folder(d);
        for (const QString &f : folder.entryList(pattern, QDir::Files))
            if (folder.remove(f)) ++removed;
    }
    return removed;
}

QString DebugLog::collect(const QString &dir, const QString &header)
{
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/rustrx-log-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmm")) + QStringLiteral(".txt");
    // written beside and renamed into place: a link planted under that
    // name (Downloads is open to other apps) is replaced, never written
    // through into Rustrx's own files
    QSaveFile file(out);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(header.toUtf8() + "\n");
    QMutexLocker locker(&s_lock);
    for (const QString &name : { QString::fromLatin1(Keys::kLogApp), QString::fromLatin1(Keys::kLogBackground),
                                 QString::fromLatin1(Keys::kLogPush) }) {
        for (const QString &suffix : { QStringLiteral(".log.1"), QStringLiteral(".log") }) {
            QFile part(logDir() + QLatin1Char('/') + name + suffix);
            if (!part.open(QIODevice::ReadOnly))
                continue;
            file.write("\n===== " + name.toUtf8() + suffix.toUtf8() + " =====\n");
            file.write(part.readAll());
        }
    }
    return file.commit() ? out : QString();
}
