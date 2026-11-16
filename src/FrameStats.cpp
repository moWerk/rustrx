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
#include "FrameStats.h"

#include <QQuickWindow>
#include <QtGlobal>

namespace {
const int kSlowUs = 20000;
const int kVerySlowUs = 33000;
// a longer gap is the window resting (nothing changed), not a slow frame:
// the next frame starts a new run
const int kIdleUs = 500000;
const int kReportMs = 1000;
}

FrameStats::FrameStats(QQuickWindow *window)
    : QObject(window)
{
    m_clock.start();
    connect(window, &QQuickWindow::frameSwapped, this, &FrameStats::swapped, Qt::DirectConnection);
    connect(&m_timer, &QTimer::timeout, this, &FrameStats::report);
    m_timer.start(kReportMs);
}

void FrameStats::swapped()
{
    const qint64 now = m_clock.nsecsElapsed() / 1000;
    const qint64 gap = m_lastUs < 0 ? -1 : now - m_lastUs;
    m_lastUs = now;
    m_frames.fetchAndAddRelaxed(1);
    if (gap < 0 || gap > kIdleUs)
        return;
    if (gap > kSlowUs)
        m_slow20.fetchAndAddRelaxed(1);
    if (gap > kVerySlowUs)
        m_slow33.fetchAndAddRelaxed(1);
    // the GUI thread resets it in between
    int seen = m_maxUs.load();
    while (gap > seen && !m_maxUs.testAndSetRelaxed(seen, int(gap), seen)) {
    }
}

void FrameStats::report()
{
    const int frames = m_frames.fetchAndStoreRelaxed(0);
    const int slow20 = m_slow20.fetchAndStoreRelaxed(0);
    const int slow33 = m_slow33.fetchAndStoreRelaxed(0);
    const int maxUs = m_maxUs.fetchAndStoreRelaxed(0);
    if (frames == 0)
        return;
    qInfo("frames=%d slow20=%d slow33=%d max=%dms", frames, slow20, slow33, (maxUs + 500) / 1000);
}
