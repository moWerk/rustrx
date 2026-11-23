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

// What comes out of pair() is stamped with the time it was captured, not
// when the microphone's thread got round to it (that made the encoder's
// timestamps wobble: clicks at the far end). The captured buffers' time,
// counted on in samples; a buffer more than a frame later than the count
// is a gap (the source dropped samples): the count starts again there,
// the part frame from before it goes, and the next frame out is marked a
// discontinuity. Times in ns, the running time.
struct Clock
{
    qint64 base = -1;      // the capture time the count starts from
    qint64 samples = 0;    // captured since base, up to mic's end
    bool discont = false;  // the next frame out follows a gap

    static qint64 ns(qint64 samples)
    {
        return samples * 1000000000LL / Keys::kVoipAecRate;
    }

    // a captured buffer of bytes at pts; mic holds what still waits for
    // its frame (cleared on a gap); true when it was one
    bool captured(qint64 pts, int bytes, QByteArray &mic)
    {
        bool gap = false;
        if (base < 0) {
            mic.clear();
            base = pts;
            samples = 0;
        } else if (pts - (base + ns(samples)) > ns(frameBytes() / 2)) {
            // late or early within a frame is the thread, not the audio;
            // earlier than the count never steps back
            mic.clear();
            base = pts;
            samples = 0;
            discont = gap = true;
        }
        samples += bytes / 2;
        return gap;
    }

    // the time of clean's first sample, taken out of mic by pair() (mic is
    // what stays in it, its part frame)
    qint64 ptsOf(const QByteArray &clean, const QByteArray &mic) const
    {
        return base + ns(samples - (clean.size() + mic.size()) / 2);
    }
};

}

#endif
