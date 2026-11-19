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

#include <QDebug>
#include <QStringList>
#include <pulse/pulseaudio.h>

namespace {

const char kCommunication[] = "communication";
const char kEarpiece[] = "output-earpiece";
const char kSpeaker[] = "output-speaker";
const char kHeadset[] = "output-wired_headset";
const char kHeadphone[] = "output-wired_headphone";

struct Card { QString name, active; bool communication = false; };
struct Sink { QString name, active; QStringList ports, available; };

void wait(pa_threaded_mainloop *loop, pa_operation *op)
{
    if (!op)
        return;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(loop);
    pa_operation_unref(op);
}

}

CallRoute::CallRoute(QObject *parent) : QObject(parent)
{
}

CallRoute::~CallRoute()
{
    if (m_active)
        restore();
    if (m_loop) {
        pa_threaded_mainloop_stop(m_loop);
        if (m_ctx) {
            pa_context_disconnect(m_ctx);
            pa_context_unref(m_ctx);
        }
        pa_threaded_mainloop_free(m_loop);
    }
}

bool CallRoute::connectPulse()
{
    if (m_ctx && pa_context_get_state(m_ctx) == PA_CONTEXT_READY)
        return true;
    if (!m_loop) {
        m_loop = pa_threaded_mainloop_new();
        pa_threaded_mainloop_start(m_loop);
    }
    pa_threaded_mainloop_lock(m_loop);
    if (m_ctx) {
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx);
    }
    m_ctx = pa_context_new(pa_threaded_mainloop_get_api(m_loop), "Rustrx call route");
    pa_context_set_state_callback(m_ctx, [](pa_context *, void *loop) {
        pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(loop), 0);
    }, m_loop);
    bool ok = pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0;
    while (ok) {
        const pa_context_state_t s = pa_context_get_state(m_ctx);
        if (s == PA_CONTEXT_READY)
            break;
        if (!PA_CONTEXT_IS_GOOD(s)) {
            ok = false;
            break;
        }
        pa_threaded_mainloop_wait(m_loop);
    }
    pa_threaded_mainloop_unlock(m_loop);
    if (!ok)
        qWarning() << "voip: no PulseAudio for the call's route";
    return ok;
}

void CallRoute::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    if (!on) {
        restore();
        m_speaker = false;
        return;
    }
    if (!connectPulse())
        return;
    // the card with a communication mode, and the sink with an earpiece
    struct Found { pa_threaded_mainloop *loop; QList<Card> cards; QList<Sink> sinks; } f{ m_loop, {}, {} };
    pa_threaded_mainloop_lock(m_loop);
    wait(m_loop, pa_context_get_card_info_list(m_ctx, [](pa_context *, const pa_card_info *i, int eol, void *p) {
        Found *f = static_cast<Found *>(p);
        if (eol) { pa_threaded_mainloop_signal(f->loop, 0); return; }
        Card c{ QString::fromUtf8(i->name), i->active_profile2 ? QString::fromUtf8(i->active_profile2->name) : QString(), false };
        for (uint32_t k = 0; k < i->n_profiles; ++k)
            if (qstrcmp(i->profiles2[k]->name, kCommunication) == 0)
                c.communication = true;
        f->cards << c;
    }, &f));
    pa_threaded_mainloop_unlock(m_loop);
    for (const Card &c : f.cards) {
        if (!c.communication)
            continue;
        m_card = c.name;
        m_cardProfile = c.active;
    }
    if (m_card.isEmpty()) {
        qInfo() << "voip: no communication mode here, the call stays on the loudspeaker";
        return;
    }
    pa_threaded_mainloop_lock(m_loop);
    wait(m_loop, pa_context_set_card_profile_by_name(m_ctx, m_card.toUtf8().constData(), kCommunication,
        [](pa_context *, int, void *loop) { pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(loop), 0); }, m_loop));
    wait(m_loop, pa_context_get_sink_info_list(m_ctx, [](pa_context *, const pa_sink_info *i, int eol, void *p) {
        Found *f = static_cast<Found *>(p);
        if (eol) { pa_threaded_mainloop_signal(f->loop, 0); return; }
        Sink s{ QString::fromUtf8(i->name), i->active_port ? QString::fromUtf8(i->active_port->name) : QString(), {}, {} };
        for (uint32_t k = 0; k < i->n_ports; ++k) {
            s.ports << QString::fromUtf8(i->ports[k]->name);
            if (i->ports[k]->available != PA_PORT_AVAILABLE_NO)
                s.available << QString::fromUtf8(i->ports[k]->name);
        }
        f->sinks << s;
    }, &f));
    pa_threaded_mainloop_unlock(m_loop);
    for (const Sink &s : f.sinks) {
        if (s.ports.contains(QLatin1String(kEarpiece))) {
            m_sink = s.name;
            m_sinkPort = s.active;
            // a plugged headset is heard there, not on the earpiece
            if (s.available.contains(QLatin1String(kHeadset)))
                m_headsetPort = QLatin1String(kHeadset);
            else if (s.available.contains(QLatin1String(kHeadphone)))
                m_headsetPort = QLatin1String(kHeadphone);
            else
                m_headsetPort.clear();
            break;
        }
    }
    qInfo().noquote() << "voip: call route on" << m_card << "was" << m_cardProfile << "sink" << m_sink << "was" << m_sinkPort;
    apply();
}

void CallRoute::setSpeaker(bool on)
{
    m_speaker = on;
    if (m_active)
        apply();
}

void CallRoute::apply()
{
    if (m_sink.isEmpty() || !m_ctx)
        return;
    const QString port = m_speaker ? QLatin1String(kSpeaker) : m_headsetPort.isEmpty() ? QLatin1String(kEarpiece) : m_headsetPort;
    pa_threaded_mainloop_lock(m_loop);
    wait(m_loop, pa_context_set_sink_port_by_name(m_ctx, m_sink.toUtf8().constData(), port.toUtf8().constData(),
        [](pa_context *, int, void *loop) { pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(loop), 0); }, m_loop));
    pa_threaded_mainloop_unlock(m_loop);
    qInfo().noquote() << "voip: call heard on" << port;
}

void CallRoute::restore()
{
    if (m_card.isEmpty() || !m_ctx)
        return;
    pa_threaded_mainloop_lock(m_loop);
    if (!m_cardProfile.isEmpty())
        wait(m_loop, pa_context_set_card_profile_by_name(m_ctx, m_card.toUtf8().constData(), m_cardProfile.toUtf8().constData(),
            [](pa_context *, int, void *loop) { pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(loop), 0); }, m_loop));
    // "parking" is no output at all: the loudspeaker instead, the policy
    // moves it on from there for whatever plays next
    const QString port = m_sinkPort.isEmpty() || m_sinkPort == QLatin1String("output-parking") ? QLatin1String(kSpeaker) : m_sinkPort;
    if (!m_sink.isEmpty())
        wait(m_loop, pa_context_set_sink_port_by_name(m_ctx, m_sink.toUtf8().constData(), port.toUtf8().constData(),
            [](pa_context *, int, void *loop) { pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(loop), 0); }, m_loop));
    pa_threaded_mainloop_unlock(m_loop);
    qInfo().noquote() << "voip: call route back to" << m_cardProfile << port;
    m_card.clear();
    m_sink.clear();
}
