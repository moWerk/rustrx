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

#include "CallRoutePlan.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

struct pa_threaded_mainloop;
struct pa_context;
struct pa_operation;
struct pa_time_event;
struct pa_mainloop_api;
struct pa_card_info;
struct pa_sink_info;
struct timeval;

// Where a call is heard, on phones with Android's audio (droid): Sailfish's
// audio policy keeps an app's streams on the loudspeaker as media, and
// does not give apps its call mode. During a call this switches the
// phone's card to its communication mode (the one Android VoIP apps use:
// earpiece, and on many phones the phone's own echo cancelling) and picks
// the earpiece or the loudspeaker; afterwards both go back as they were.
// The call's streams join the policy's "aliencall" group (App Support's
// calls) by the rule rpm/harbour-rustrx-xpolicy.conf installs, so the
// policy leaves them there. A phone without a communication mode (the
// mainline Pro¹-X) is left alone. The choices are in CallRoutePlan.h.
//
// Nothing here waits on the GUI thread (audit B7): setActive and
// setSpeaker queue a step and return, and PulseAudio's own thread (a
// threaded mainloop) works the queue in order, each step from its
// callbacks: profile before port, and a speaker toggle during an
// activation after it. A step PulseAudio does not finish within
// Keys::kCallRouteTimeoutMs is given up and logged, and the connection is
// dropped (the next step connects anew). The destructor gives a pending
// restore Keys::kCallRouteQuitWaitMs, then stops that thread.
class CallRoute : public QObject
{
    Q_OBJECT
public:
    explicit CallRoute(QObject *parent = nullptr);
    ~CallRoute() override;

    void setActive(bool on);
    void setSpeaker(bool on);

    // tests: no step queued or running
    bool idle();

private:
    enum class Op { Activate, Speaker, Restore };
    enum class Step { Idle, Connecting, Cards, Profile, Sinks, Port, RestoreProfile, RestorePort };
    struct Job { Op op; bool speaker; };

    // all below run on PulseAudio's thread, or with its lock held
    void enqueue(Job job);
    void next();
    void begin();
    void connectPulse();
    void listCards();
    void setProfile();
    void listSinks();
    void setPort(const QString &port, Step step);
    void restoreProfile();
    void restorePort();
    void restored();
    void done();
    void giveUp(const char *why);
    void dropContext();
    void track(pa_operation *op);

    static void onKick(pa_mainloop_api *api, void *self);
    static void onState(pa_context *ctx, void *self);
    static void onCard(pa_context *ctx, const pa_card_info *info, int eol, void *self);
    static void onSink(pa_context *ctx, const pa_sink_info *info, int eol, void *self);
    static void onSuccess(pa_context *ctx, int success, void *self);
    static void onTimeout(pa_mainloop_api *api, pa_time_event *e, const struct timeval *tv, void *self);

    // the GUI thread's view
    bool m_active = false;
    bool m_speaker = false;

    // PulseAudio's thread (under its lock)
    pa_threaded_mainloop *m_loop = nullptr;
    pa_context *m_ctx = nullptr;
    pa_operation *m_op = nullptr;      // the one request in flight
    pa_time_event *m_timer = nullptr;  // the running step's time limit
    QList<Job> m_queue;
    Job m_job{ Op::Activate, false };
    Step m_step = Step::Idle;
    bool m_routed = false;             // the call is on our route now
    bool m_routeSpeaker = false;       // the speaker as last applied or asked
    QList<CallRoutePlan::Card> m_cards;
    QList<CallRoutePlan::Sink> m_sinks;
    // what was there before the call
    QString m_card, m_cardProfile, m_sink, m_sinkPort;
    QString m_headsetPort;             // a plugged headset's port, if any
    QString m_portName;                // the port being set

    // the destructor's wait for the queue to empty
    QMutex m_idleLock;
    QWaitCondition m_idleCond;
    bool m_idle = true;
};
