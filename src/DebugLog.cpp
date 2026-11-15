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

#include <QAtomicInt>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

static const qint64 kMaxSize = 1024 * 1024;
static QtMessageHandler s_previous = nullptr;
static QAtomicInt s_enabled(1);
static QString s_path;
static QMutex s_lock(QMutex::Recursive);

static QString logDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
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
    if (file && ++lines >= 50) {
        lines = 0;
        if (file->size() > kMaxSize) {
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

static void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // the file first: the system log's handler aborts on a fatal message
    if (s_enabled.load() && !s_path.isEmpty())
        appendLine(type, message);
    if (s_previous)
        s_previous(type, context, message);
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

QString DebugLog::collect(const QString &dir, const QString &header)
{
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/rustrx-log-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmm")) + QStringLiteral(".txt");
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(header.toUtf8() + "\n");
    QMutexLocker locker(&s_lock);
    for (const QString &name : { QStringLiteral("app"), QStringLiteral("background") }) {
        for (const QString &suffix : { QStringLiteral(".log.1"), QStringLiteral(".log") }) {
            QFile part(logDir() + QLatin1Char('/') + name + suffix);
            if (!part.open(QIODevice::ReadOnly))
                continue;
            file.write("\n===== " + name.toUtf8() + suffix.toUtf8() + " =====\n");
            file.write(part.readAll());
        }
    }
    return out;
}
