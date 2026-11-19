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
#include "CallRoute.h"
#include "Keys.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <unistd.h>
#include <pulse/pulseaudio.h>

using namespace CallRoutePlan;

CallRoute::CallRoute(QObject *parent) : QObject(parent)
{
}

// A restore still pending (or the one a running call needs) gets a moment,
// bounded: a hung PulseAudio must not hold up quitting. Then PulseAudio's
// thread is stopped; nothing of ours runs on it after that.
CallRoute::~CallRoute()
{
    if (!m_loop)
        return;
    if (m_active) {
        m_active = false;
        enqueue({ Op::Restore, false });
    }
    {
        QMutexLocker lock(&m_idleLock);
        QElapsedTimer waited;
        waited.start();
        while (!m_idle && waited.elapsed() < Keys::kCallRouteQuitWaitMs)
            m_idleCond.wait(&m_idleLock, ulong(Keys::kCallRouteQuitWaitMs - waited.elapsed()));
        if (!m_idle)
            qWarning() << "voip: call route still busy on quitting, left as it is";
    }
    pa_threaded_mainloop_lock(m_loop);
    m_queue.clear();
    if (m_op) {
        pa_operation_cancel(m_op);
        pa_operation_unref(m_op);
        m_op = nullptr;
    }
    if (m_timer) {
        pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_loop);
        api->time_free(m_timer);
        m_timer = nullptr;
    }
    dropContext();
    m_step = Step::Idle;
    pa_threaded_mainloop_unlock(m_loop);
    pa_threaded_mainloop_stop(m_loop);
    pa_threaded_mainloop_free(m_loop);
}

void CallRoute::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    if (!on)
        m_speaker = false;
    enqueue({ on ? Op::Activate : Op::Restore, false });
}

void CallRoute::setSpeaker(bool on)
{
    m_speaker = on;
    enqueue({ Op::Speaker, on });
}

void CallRoute::follow()
{
    if (m_active)
        enqueue({ Op::Follow, false });
}

bool CallRoute::idle()
{
    QMutexLocker lock(&m_idleLock);
    return m_idle;
}

// ---- the GUI thread hands a step over -------------------------------------

void CallRoute::enqueue(Job job)
{
    if (!m_loop) {
        m_loop = pa_threaded_mainloop_new();
        if (m_loop && pa_threaded_mainloop_start(m_loop) < 0) {
            pa_threaded_mainloop_free(m_loop);
            m_loop = nullptr;
        }
        if (!m_loop) {
            qWarning() << "voip: no PulseAudio thread for the call's route";
            return;
        }
    }
    // held only while PulseAudio's thread runs one of the callbacks below,
    // none of which waits
    pa_threaded_mainloop_lock(m_loop);
    m_queue << job;
    {
        QMutexLocker lock(&m_idleLock);
        m_idle = false;
    }
    pa_mainloop_api_once(pa_threaded_mainloop_get_api(m_loop), &CallRoute::onKick, this);
    pa_threaded_mainloop_unlock(m_loop);
}

// ---- PulseAudio's thread, its lock held -----------------------------------

void CallRoute::onKick(pa_mainloop_api *, void *self)
{
    static_cast<CallRoute *>(self)->next();
}

void CallRoute::next()
{
    if (m_step != Step::Idle)
        return;   // the running step calls next() when it is done
    if (m_queue.isEmpty()) {
        QMutexLocker lock(&m_idleLock);
        m_idle = true;
        m_idleCond.wakeAll();
        return;
    }
    m_job = m_queue.takeFirst();
    pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_loop);
    struct timeval tv;
    pa_gettimeofday(&tv);
    pa_timeval_add(&tv, pa_usec_t(Keys::kCallRouteTimeoutMs) * PA_USEC_PER_MSEC);
    m_timer = api->time_new(api, &tv, &CallRoute::onTimeout, this);
    begin();
}

void CallRoute::begin()
{
    const bool ready = m_ctx && pa_context_get_state(m_ctx) == PA_CONTEXT_READY;
    switch (m_job.op) {
    case Op::Activate:
        if (ready)
            listCards();
        else
            connectPulse();
        return;
    case Op::Restore:
        if (m_card.isEmpty()) {
            // nothing was switched
            m_routed = false;
            m_routeSpeaker = false;
            done();
        } else if (ready) {
            restoreProfile();
        } else {
            connectPulse();
        }
        return;
    case Op::Speaker:
        m_routeSpeaker = m_job.speaker;
        if (!m_routed || (m_sink.isEmpty() && !m_ucm))
            done();   // taken up by the next activation
        else if (ready)
            setSpeakerRoute();
        else
            connectPulse();
        return;
    case Op::Follow:
        if (!m_routed || !m_ucm)
            done();
        else if (ready)
            listUcmSink();
        else
            connectPulse();
        return;
    }
}

void CallRoute::connectPulse()
{
    dropContext();
    m_step = Step::Connecting;
    pa_context *ctx = pa_context_new(pa_threaded_mainloop_get_api(m_loop), "Rustrx call route");
    m_ctx = ctx;
    if (!ctx) {
        giveUp("no PulseAudio context");
        return;
    }
    pa_context_set_state_callback(ctx, &CallRoute::onState, this);
    // no autospawn: starting a sound server is not an app's business, and
    // it would wait here
    if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0 && m_ctx == ctx && m_step == Step::Connecting)
        giveUp("no PulseAudio for the call's route");
}

void CallRoute::onState(pa_context *ctx, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (ctx != r->m_ctx)
        return;
    const pa_context_state_t state = pa_context_get_state(ctx);
    if (state == PA_CONTEXT_READY) {
        if (r->m_step != Step::Connecting)
            return;
        switch (r->m_job.op) {
        case Op::Activate:
            r->listCards();
            break;
        case Op::Restore:
            r->restoreProfile();
            break;
        case Op::Speaker:
            r->setSpeakerRoute();
            break;
        case Op::Follow:
            r->listUcmSink();
            break;
        }
    } else if (!PA_CONTEXT_IS_GOOD(state)) {
        if (r->m_step != Step::Idle)
            r->giveUp("PulseAudio went away");
        else
            r->dropContext();
    }
}

void CallRoute::track(pa_operation *op)
{
    if (!op) {
        giveUp("PulseAudio refused a request");
        return;
    }
    m_op = op;
}

void CallRoute::listCards()
{
    m_step = Step::Cards;
    m_cards.clear();
    m_cardIndexes.clear();
    track(pa_context_get_card_info_list(m_ctx, &CallRoute::onCard, this));
}

void CallRoute::onCard(pa_context *, const pa_card_info *i, int eol, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (r->m_step != Step::Cards)
        return;
    if (eol == 0 && i) {
        Card c{ QString::fromUtf8(i->name), i->active_profile2 ? QString::fromUtf8(i->active_profile2->name) : QString(), {} };
        for (uint32_t k = 0; k < i->n_profiles; ++k)
            c.profiles << QString::fromUtf8(i->profiles2[k]->name);
        r->m_cards << c;
        r->m_cardIndexes << i->index;
        return;
    }
    if (r->m_op) {
        pa_operation_unref(r->m_op);
        r->m_op = nullptr;
    }
    if (eol < 0) {
        r->giveUp("the cards could not be listed");
        return;
    }
    int found = card(r->m_cards);
    r->m_ucm = false;
    if (found < 0) {
        found = ucmCard(r->m_cards);
        if (found < 0) {
            qInfo() << "voip: no communication mode or earpiece profile here, the call stays on the loudspeaker";
            r->done();
            return;
        }
        r->m_ucm = true;
        r->m_ucmCard = r->m_cards.at(found);
        r->m_ucmCardIndex = r->m_cardIndexes.at(found);
    }
    // a route that was never restored (PulseAudio gave up) keeps what was
    // there before it
    if (r->m_card.isEmpty()) {
        r->m_card = r->m_cards.at(found).name;
        r->m_cardProfile = r->m_cards.at(found).active;
    }
    r->m_targetProfile = r->m_ucm ? ucmCallProfile(r->m_ucmCard, r->m_cardProfile, r->m_routeSpeaker)
                                  : QString::fromLatin1(kCommunication);
    r->setProfile();
}

void CallRoute::setProfile()
{
    m_step = Step::Profile;
    track(pa_context_set_card_profile_by_name(m_ctx, m_card.toUtf8().constData(), m_targetProfile.toUtf8().constData(),
                                              &CallRoute::onSuccess, this));
}

// the speaker button: a port on droid, a profile on mainline
void CallRoute::setSpeakerRoute()
{
    if (m_ucm) {
        m_targetProfile = ucmCallProfile(m_ucmCard, m_cardProfile, m_routeSpeaker);
        setProfile();
    } else {
        setPort(callPort(m_routeSpeaker, m_headsetPort), Step::Port);
    }
}

void CallRoute::listSinks()
{
    m_step = Step::Sinks;
    m_sinks.clear();
    track(pa_context_get_sink_info_list(m_ctx, &CallRoute::onSink, this));
}

void CallRoute::onSink(pa_context *, const pa_sink_info *i, int eol, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (r->m_step != Step::Sinks)
        return;
    if (eol == 0 && i) {
        Sink s{ QString::fromUtf8(i->name), i->active_port ? QString::fromUtf8(i->active_port->name) : QString(), {}, {} };
        for (uint32_t k = 0; k < i->n_ports; ++k) {
            s.ports << QString::fromUtf8(i->ports[k]->name);
            if (i->ports[k]->available != PA_PORT_AVAILABLE_NO)
                s.available << QString::fromUtf8(i->ports[k]->name);
        }
        r->m_sinks << s;
        return;
    }
    if (r->m_op) {
        pa_operation_unref(r->m_op);
        r->m_op = nullptr;
    }
    if (eol < 0) {
        r->giveUp("the sinks could not be listed");
        return;
    }
    const int found = sink(r->m_sinks);
    if (found >= 0) {
        if (r->m_sink.isEmpty()) {
            r->m_sink = r->m_sinks.at(found).name;
            r->m_sinkPort = r->m_sinks.at(found).active;
        }
        r->m_headsetPort = headsetPort(r->m_sinks.at(found));
    } else {
        r->m_headsetPort.clear();
    }
    r->m_routed = true;
    qInfo().noquote() << "voip: call route on" << r->m_card << "was" << r->m_cardProfile << "sink" << r->m_sink << "was" << r->m_sinkPort;
    if (r->m_sink.isEmpty())
        r->done();
    else
        r->setPort(callPort(r->m_routeSpeaker, r->m_headsetPort), Step::Port);
}

void CallRoute::setPort(const QString &port, Step step)
{
    m_step = step;
    m_portName = port;
    track(pa_context_set_sink_port_by_name(m_ctx, m_sink.toUtf8().constData(), port.toUtf8().constData(),
                                           &CallRoute::onSuccess, this));
}

// mainline: the profile switch takes the old sink away and PulseAudio
// moves the call to its fallback, the null sink, where the policy leaves
// it (its route still names the loudspeaker); so our own call streams
// follow the card to the sink of its new profile
void CallRoute::listUcmSink()
{
    m_step = Step::UcmSink;
    m_ucmSinkIndex = PA_INVALID_INDEX;
    track(pa_context_get_sink_info_list(m_ctx, &CallRoute::onUcmSink, this));
}

void CallRoute::onUcmSink(pa_context *, const pa_sink_info *i, int eol, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (r->m_step != Step::UcmSink)
        return;
    if (eol == 0 && i) {
        if (i->card == r->m_ucmCardIndex && r->m_ucmSinkIndex == PA_INVALID_INDEX)
            r->m_ucmSinkIndex = i->index;
        return;
    }
    if (r->m_op) {
        pa_operation_unref(r->m_op);
        r->m_op = nullptr;
    }
    if (eol < 0 || r->m_ucmSinkIndex == PA_INVALID_INDEX) {
        qWarning() << "voip: the card has no sink for the call";
        r->done();
        return;
    }
    r->m_step = Step::UcmInputs;
    r->m_ucmInputs.clear();
    r->track(pa_context_get_sink_input_info_list(r->m_ctx, &CallRoute::onUcmInput, r));
}

void CallRoute::onUcmInput(pa_context *, const pa_sink_input_info *i, int eol, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (r->m_step != Step::UcmInputs)
        return;
    if (eol == 0 && i) {
        const char *pid = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_ID);
        const char *role = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_ROLE);
        if (pid && role && QByteArray(pid) == QByteArray::number(qint64(getpid()))
                && QByteArray(role) == "phone" && i->sink != r->m_ucmSinkIndex)
            r->m_ucmInputs << i->index;
        return;
    }
    if (r->m_op) {
        pa_operation_unref(r->m_op);
        r->m_op = nullptr;
    }
    r->moveNextInput();
}

void CallRoute::moveNextInput()
{
    if (m_ucmInputs.isEmpty()) {
        done();
        return;
    }
    m_step = Step::UcmMove;
    const uint32_t input = m_ucmInputs.takeFirst();
    qInfo() << "voip: call stream" << input << "moved to sink" << m_ucmSinkIndex;
    track(pa_context_move_sink_input_by_index(m_ctx, input, m_ucmSinkIndex, &CallRoute::onSuccess, this));
}

void CallRoute::restoreProfile()
{
    if (m_cardProfile.isEmpty()) {
        restorePort();
        return;
    }
    m_step = Step::RestoreProfile;
    track(pa_context_set_card_profile_by_name(m_ctx, m_card.toUtf8().constData(), m_cardProfile.toUtf8().constData(),
                                              &CallRoute::onSuccess, this));
}

void CallRoute::restorePort()
{
    if (!m_sink.isEmpty()) {
        setPort(CallRoutePlan::restorePort(m_sinkPort), Step::RestorePort);
        return;
    }
    m_step = Step::RestorePort;
    restored();
}

void CallRoute::restored()
{
    qInfo().noquote() << "voip: call route back to" << m_cardProfile << (m_sink.isEmpty() ? QString() : m_portName);
    m_card.clear();
    m_cardProfile.clear();
    m_sink.clear();
    m_sinkPort.clear();
    m_routed = false;
    m_routeSpeaker = false;
    m_ucm = false;
    done();
}

void CallRoute::onSuccess(pa_context *, int success, void *self)
{
    CallRoute *r = static_cast<CallRoute *>(self);
    if (r->m_op) {
        pa_operation_unref(r->m_op);
        r->m_op = nullptr;
    }
    switch (r->m_step) {
    case Step::Profile:
        if (!success)
            qWarning().noquote() << "voip: the profile" << r->m_targetProfile << "was refused";
        if (r->m_ucm) {
            // the profile is the output: nothing more to set
            if (!r->m_routed)
                qInfo().noquote() << "voip: call route on" << r->m_card << "was" << r->m_cardProfile;
            if (success)
                qInfo().noquote() << "voip: call heard on" << r->m_targetProfile << "of" << r->m_card;
            r->m_routed = true;
            r->listUcmSink();
        } else {
            r->listSinks();
        }
        break;
    case Step::Port:
        if (success)
            qInfo().noquote() << "voip: call heard on" << r->m_portName;
        else
            qWarning().noquote() << "voip: the port" << r->m_portName << "was refused";
        r->done();
        break;
    case Step::UcmMove:
        if (!success)
            qWarning() << "voip: the call could not be moved to the card's sink";
        r->moveNextInput();
        break;
    case Step::RestoreProfile:
        if (!success)
            qWarning() << "voip: the card's own mode was refused";
        r->restorePort();
        break;
    case Step::RestorePort:
        if (!success)
            qWarning().noquote() << "voip: the port" << r->m_portName << "was refused";
        r->restored();
        break;
    default:
        break;
    }
}

void CallRoute::onTimeout(pa_mainloop_api *, pa_time_event *, const struct timeval *, void *self)
{
    static_cast<CallRoute *>(self)->giveUp("PulseAudio did not answer in time");
}

// the step ends unfinished: its request is cancelled (no callback of it
// comes any more) and the connection dropped, so whatever PulseAudio
// still holds is not waited for; what was saved before the call stays,
// for the next restore
void CallRoute::giveUp(const char *why)
{
    static const char *const steps[] = { "idle", "connecting", "cards", "profile", "sinks", "port", "restoring the profile", "restoring the port" };
    qWarning().noquote() << "voip: call route given up," << why << "at" << steps[int(m_step)];
    if (m_op) {
        pa_operation_cancel(m_op);
        pa_operation_unref(m_op);
        m_op = nullptr;
    }
    dropContext();
    // the call is over even where its route could not be put back: the
    // next call starts on the earpiece again
    if (m_job.op == Op::Restore) {
        m_routed = false;
        m_routeSpeaker = false;
    }
    done();
}

void CallRoute::dropContext()
{
    if (!m_ctx)
        return;
    pa_context *ctx = m_ctx;
    m_ctx = nullptr;
    pa_context_set_state_callback(ctx, nullptr, nullptr);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
}

void CallRoute::done()
{
    if (m_timer) {
        pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_loop);
        api->time_free(m_timer);
        m_timer = nullptr;
    }
    m_step = Step::Idle;
    next();
}
