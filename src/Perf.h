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
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QString>

// The perf: markers (review-and-architecture-hints.md, "Timing markers"):
// one monotonic clock from the first line of main(), lines at debug level in
// the category rustrx.perf (RUSTRX_PERF=1 or the perf-log flag turns it on).
// The start-up steps are also kept and summed up once, at info.
namespace Perf {

inline QElapsedTimer &clock()
{
    static QElapsedTimer t;
    return t;
}

inline qint64 ms()
{
    return clock().isValid() ? clock().elapsed() : -1;
}

inline const QLoggingCategory &category()
{
    static const QLoggingCategory c("rustrx.perf", QtInfoMsg);
    return c;
}

inline bool on()
{
    return category().isDebugEnabled();
}

struct Steps {
    QMutex lock;
    QList<QPair<QByteArray, qint64>> kept;
    bool summed = false;
};

inline Steps &steps()
{
    static Steps s;
    return s;
}

// a start-up step, at `t` ms on the clock (now when below 0); any thread
inline void mark(const char *step, qint64 t = -1, const QString &detail = QString())
{
    if (t < 0)
        t = ms();
    {
        Steps &s = steps();
        QMutexLocker locker(&s.lock);
        bool known = s.summed;
        for (int i = 0; !known && i < s.kept.size(); ++i)
            known = s.kept.at(i).first == step;
        if (!known)
            s.kept.append(qMakePair(QByteArray(step), t));
    }
    if (!on())
        return;
    QString line = QStringLiteral("perf: ") + QLatin1String(step) + QStringLiteral(" t=") + QString::number(t);
    if (!detail.isEmpty())
        line += QLatin1Char(' ') + detail;
    qCDebug(category).noquote() << line;
}

// the kept steps in one info line, once per process
inline void summary()
{
    QString line;
    {
        Steps &s = steps();
        QMutexLocker locker(&s.lock);
        if (s.summed)
            return;
        s.summed = true;
        for (const auto &step : s.kept)
            line += QLatin1Char(' ') + QString::fromLatin1(step.first) + QLatin1Char('=') + QString::number(step.second);
    }
    qInfo().noquote() << "perf: startup" + line << "ms";
}

}
