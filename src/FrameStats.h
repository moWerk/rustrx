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

#include <QAtomicInt>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class QQuickWindow;

// A frame counter for comparing builds (RUSTRX_FRAMES=1): once a second,
// while frames flow, one line in the log with the frames shown, those that
// came more than 20 and 33 ms after the one before, and the longest gap.
// The window's frameSwapped comes on the render thread: counted there with
// atomics only, read and reset here on the GUI thread.
class FrameStats : public QObject
{
    Q_OBJECT
public:
    explicit FrameStats(QQuickWindow *window);

private:
    void swapped();
    void report();

    QAtomicInt m_frames;
    QAtomicInt m_slow20;
    QAtomicInt m_slow33;
    QAtomicInt m_maxUs;
    // render thread only
    QElapsedTimer m_clock;
    qint64 m_lastUs = -1;
    QTimer m_timer;
};
