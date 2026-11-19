// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_AECFRAMES_H
#define RUSTRX_AECFRAMES_H

#include "Keys.h"

#include <QByteArray>

// The echo canceller's two inputs in step (Voip.cpp, on GStreamer's
// threads under Voip's lock; tested in tests/cpp/voip): the far end's
// audio, as it goes to the loudspeaker, waits in a short queue, and each
// 10 ms microphone frame is taken with the queue's next frame, or with
// silence when none came. 16-bit mono at Keys::kVoipAecRate.
namespace AecFrames {

// one frame, in bytes
inline int frameBytes()
{
    return Keys::kVoipAecRate * Keys::kVoipAecFrameMs / 1000 * 2;
}

// the far end's queue at most (Keys::kVoipAecQueueMs), in bytes
inline int farQueueBytes()
{
    return Keys::kVoipAecRate * Keys::kVoipAecQueueMs / 1000 * 2;
}

// The far end comes in bursts (the network): the queue keeps its newest
// part only, so the echo in the microphone stays within what the
// canceller learns (Keys::kVoipAecTailMs).
inline void queueFar(QByteArray &far, const QByteArray &data, int most)
{
    far += data;
    if (far.size() > most)
        far.remove(0, far.size() - most);
}

// Every whole frame of mic, each with far's next frame (taken from far)
// or with silence, through process(mic, far, out); what process wrote,
// frame after frame. A part frame stays in mic for the next call.
template <typename Process>
QByteArray pair(QByteArray &mic, QByteArray &far, int bytes, Process process)
{
    QByteArray clean;
    QByteArray silence;
    while (mic.size() >= bytes) {
        QByteArray frame(bytes, Qt::Uninitialized);
        const bool farFrame = far.size() >= bytes;
        if (!farFrame && silence.isEmpty())
            silence = QByteArray(bytes, '\0');
        process(mic.constData(), farFrame ? far.constData() : silence.constData(), frame.data());
        if (farFrame)
            far.remove(0, bytes);
        clean += frame;
        mic.remove(0, bytes);
    }
    return clean;
}

}

#endif
