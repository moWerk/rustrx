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
#define GST_USE_UNSTABLE_API
#include "Voip.h"
#include "AecFrames.h"
#include "Keys.h"

#include <QDateTime>
#include <QEventLoop>
#include <QDebug>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>

namespace {

QString newId()
{
    return QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

}

// "turn:host:port?transport=udp" with the server's credentials, the way
// webrtcbin's add-turn-server takes it: "turn://user:pass@host:port?..."
QString Voip::turnUrl(const QString &uri, const QString &user, const QString &pass)
{
    const int colon = uri.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return QString();
    const QString scheme = uri.left(colon);
    if (scheme != QLatin1String("turn") && scheme != QLatin1String("turns"))
        return QString();
    QString rest = uri.mid(colon + 1);
    if (rest.startsWith(QLatin1String("//")))
        rest = rest.mid(2);
    return scheme + QLatin1String("://")
           + QString::fromLatin1(QUrl::toPercentEncoding(user)) + QLatin1Char(':')
           + QString::fromLatin1(QUrl::toPercentEncoding(pass)) + QLatin1Char('@') + rest;
}

// the a=mid of each m-line, in order
QStringList Voip::mids(const QString &sdp)
{
    QStringList out;
    static const QRegularExpression mline(QStringLiteral("^m="), QRegularExpression::MultilineOption);
    static const QRegularExpression mid(QStringLiteral("^a=mid:(\\S+)"), QRegularExpression::MultilineOption);
    const QStringList sections = sdp.split(mline);
    for (int i = 1; i < sections.size(); ++i) {
        const QRegularExpressionMatch m = mid.match(sections.at(i));
        out << (m.hasMatch() ? m.captured(1) : QString());
    }
    return out;
}

Voip::Voip(QObject *parent) : QObject(parent)
{
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);
    m_candidateTimer.setSingleShot(true);
    m_candidateTimer.setInterval(Keys::kVoipCandidateBatchMs);
    connect(&m_candidateTimer, &QTimer::timeout, this, &Voip::flushCandidates);
    m_inviteTimer.setSingleShot(true);
    connect(&m_inviteTimer, &QTimer::timeout, this, [this]() {
        // nobody answered in time
        if (m_state == QLatin1String("outgoing"))
            finish(QStringLiteral("invite_timeout"), true, QStringLiteral("invite_timeout"));
        else if (m_state == QLatin1String("incoming"))
            finish(QStringLiteral("invite_timeout"), false);
    });
    m_statsTimer.setInterval(Keys::kVoipStatsMs);
    connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
        if (!m_webrtc)
            return;
        GstPromise *p = gst_promise_new_with_change_func(&Voip::onStatsReply, this, nullptr);
        g_signal_emit_by_name(m_webrtc, "get-stats", nullptr, p);
    });
    m_turnTimer.setSingleShot(true);
    m_turnTimer.setInterval(Keys::kVoipTurnWaitMs);
    connect(&m_turnTimer, &QTimer::timeout, this, [this]() {
        // no answer about TURN: go on without (a LAN call still works)
        if (!m_waitingForTurn)
            return;
        qInfo() << "voip: no TURN credentials in time, going on without";
        m_waitingForTurn = false;
        if (relayMissing())
            return;
        if (m_offerer)
            negotiateAsOfferer();
        else if (m_state == QLatin1String("connecting") && startPipeline())
            applyRemote(QStringLiteral("offer"), m_remoteOffer);
    });
}

Voip::~Voip()
{
    stopPipeline();
}

QVariant Voip::version() const
{
    return m_version.isValid() ? m_version : QVariant(QStringLiteral("1"));
}

void Voip::setState(const QString &state)
{
    if (state == m_state)
        return;
    qInfo().noquote() << "voip: state" << m_state << "->" << state;
    const bool needed = state == QLatin1String("outgoing") || state == QLatin1String("connecting") || state == QLatin1String("connected");
    m_state = state;
    emit audioNeeded(needed);
    emit changed();
}

void Voip::setMuted(bool on)
{
    if (on == m_muted)
        return;
    m_muted = on;
    if (m_volume)
        g_object_set(m_volume, "mute", on ? TRUE : FALSE, nullptr);
    emit changed();
}

void Voip::setSpeaker(bool on)
{
    if (on == m_speaker)
        return;
    m_speaker = on;
    emit speakerChanged(on);
    emit changed();
}

QVariantMap Voip::status() const
{
    return { { "state", m_state }, { "room_id", m_roomId }, { "call_id", m_callId },
             { "remote_level_db", m_lastLevel }, { "packets_in", m_packetsIn },
             { "packets_out", m_packetsOut }, { "end_reason", m_endReason },
             { "remote_candidates", m_candidatesAdded },
             { "turn", m_turnUris.size() },
             // the next call's test settings (tests: they last one call)
             { "test_tone", m_testTone }, { "aec", m_aec } };
}

void Voip::send(const QString &type, QVariantMap content)
{
    content.insert("call_id", m_callId);
    content.insert("party_id", m_partyId);
    content.insert("version", version());
    emit command({ { "cmd", "voip_send" }, { "room_id", m_roomId }, { "type", type }, { "content", content } });
}

// ---- starting a call ------------------------------------------------------

void Voip::place(const QString &roomId, const QString &invitee, const QString &peerName)
{
    if (m_state != QLatin1String("idle")) {
        qInfo() << "voip: place refused, a call is going on";
        return;
    }
    reset();
    m_roomId = roomId;
    m_invitee = invitee;
    m_peerName = peerName;
    m_callId = newId();
    m_partyId = newId();
    m_offerer = true;
    m_endReason.clear();
    setState(QStringLiteral("outgoing"));
    m_waitingForTurn = true;
    emit command({ { "cmd", "turn_server" } });
    m_turnTimer.start();
}

void Voip::negotiateAsOfferer()
{
    // the pipeline asks for an offer once it plays (on-negotiation-needed)
    if (!startPipeline())
        finish(QStringLiteral("user_media_failed"), true, QStringLiteral("user_media_failed"));
}

void Voip::answer()
{
    if (m_state != QLatin1String("incoming"))
        return;
    m_inviteTimer.stop();
    m_partyId = newId();
    setState(QStringLiteral("connecting"));
    m_waitingForTurn = true;
    emit command({ { "cmd", "turn_server" } });
    m_turnTimer.start();
}

void Voip::handleTurn(const QVariantMap &ev)
{
    m_turnUris.clear();
    for (const QVariant &u : ev.value("uris").toList())
        m_turnUris << u.toString();
    m_turnUser = ev.value("username").toString();
    m_turnPass = ev.value("password").toString();
    qInfo() << "voip: TURN uris" << m_turnUris.size();
    if (!m_waitingForTurn)
        return;
    m_waitingForTurn = false;
    m_turnTimer.stop();
    if (relayMissing())
        return;
    if (m_offerer && m_state == QLatin1String("outgoing")) {
        negotiateAsOfferer();
    } else if (!m_offerer && m_state == QLatin1String("connecting")) {
        if (startPipeline())
            applyRemote(QStringLiteral("offer"), m_remoteOffer);
        else
            finish(QStringLiteral("user_media_failed"), true, QStringLiteral("user_media_failed"));
    }
}

// relay only, and the server gave no relay this call could use: it ends
// here, with its own reason, instead of trying and failing after a while
bool Voip::relayMissing()
{
    if (!m_relayOnly || (m_state != QLatin1String("outgoing") && m_state != QLatin1String("connecting")))
        return false;
    for (const QString &uri : m_turnUris) {
        if (!turnUrl(uri, m_turnUser, m_turnPass).isEmpty())
            return false;
    }
    qInfo() << "voip: relay only, and the server offers no relay";
    // a caller's invite has not gone out yet; an answerer's caller waits
    finish(QStringLiteral("no_relay"), m_state == QLatin1String("connecting"), QStringLiteral("ice_failed"));
    return true;
}

bool Voip::isRelayCandidate(const QString &candidate)
{
    return candidate.contains(QLatin1String(" typ relay"));
}

QString Voip::relayOnlySdp(const QString &sdp)
{
    QStringList kept;
    for (const QString &line : sdp.split(QStringLiteral("\r\n"))) {
        if (line.startsWith(QLatin1String("a=candidate:")) && !isRelayCandidate(line))
            continue;
        kept << line;
    }
    return kept.join(QStringLiteral("\r\n"));
}

void Voip::reject()
{
    if (m_state != QLatin1String("incoming"))
        return;
    // version 1 has a reject of its own; a version 0 caller knows only hangup
    if (version().toString() == QLatin1String("1"))
        send(QStringLiteral("m.call.reject"), {});
    else
        send(QStringLiteral("m.call.hangup"), {});
    finish(QStringLiteral("rejected"), false);
}

void Voip::hangup()
{
    if (m_state == QLatin1String("idle"))
        return;
    if (m_state == QLatin1String("incoming")) {
        reject();
        return;
    }
    finish(QStringLiteral("hangup"), true);
}

void Voip::finish(const QString &reason, bool tellPeer, const QString &wireReason)
{
    if (m_state == QLatin1String("idle"))
        return;
    qInfo().noquote() << "voip: call ends," << reason << "packets in" << m_packetsIn << "out" << m_packetsOut;
    if (tellPeer) {
        QVariantMap content;
        if (!wireReason.isEmpty())
            content.insert("reason", wireReason);
        flushCandidates();
        send(QStringLiteral("m.call.hangup"), content);
    }
    stopPipeline();
    m_endReason = reason;
    // a test's tone or echo setting lasts one call (a tone left on made a
    // later real call send 440 Hz and play nothing)
    m_testTone = false;
    m_aec = true;
    setState(QStringLiteral("idle"));
    reset();
}

void Voip::reset()
{
    m_candidateTimer.stop();
    m_inviteTimer.stop();
    m_statsTimer.stop();
    m_turnTimer.stop();
    m_waitingForTurn = false;
    m_localCandidates.clear();
    m_remoteCandidates.clear();
    m_remoteSet = false;
    m_descSent = false;
    m_offerer = false;
    m_peerParty.clear();
    m_peerUser.clear();
    m_remoteOffer.clear();
    m_version = QVariant();
    m_connectedAt = 0;
    m_lastLevel = -100;
    m_packetsIn = m_packetsOut = 0;
    m_candidatesAdded = 0;
    if (m_muted || m_speaker) {
        m_muted = false;
        if (m_speaker) {
            m_speaker = false;
            emit speakerChanged(false);
        }
        emit changed();
    }
}

// An event of the call could not be sent (no network, refused). Before
// our invite or answer is out the call cannot come about: it ends instead
// of showing "Calling…" or "Connecting…" until a timeout (an answer had
// none). Later failures (a candidates batch) leave it to ICE.
void Voip::sendFailed()
{
    if (m_descSent || (m_state != QLatin1String("outgoing") && m_state != QLatin1String("connecting")))
        return;
    finish(QStringLiteral("send_failed"), false);
}

// Rustrx is quitting: a running call is hung up, and the process waits
// (a little) until the hangup has left, so the other side stops ringing or
// talking to nobody
void Voip::endForQuit(int waitMs)
{
    if (m_state == QLatin1String("idle") || m_state == QLatin1String("incoming"))
        return;
    QEventLoop loop;
    QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
    connect(this, &Voip::hangupSent, &loop, &QEventLoop::quit);
    hangup();
    loop.exec();
    qInfo() << "voip: hung up on quitting";
}

QVariantMap Voip::ringing() const
{
    if (m_state != QLatin1String("incoming"))
        return QVariantMap();
    return { { "room_id", m_roomId }, { "call_id", m_callId }, { "party_id", m_peerParty },
             { "version", version() }, { "offer", m_remoteOffer }, { "peer_name", m_peerName },
             { "expires_at", nowMs() + m_inviteTimer.remainingTime() } };
}

void Voip::takeRinging(const QVariantMap &invite)
{
    const qint64 left = invite.value("expires_at").toLongLong() - nowMs();
    if (invite.isEmpty() || left <= 0 || m_state != QLatin1String("idle"))
        return;
    reset();
    m_roomId = invite.value("room_id").toString();
    m_callId = invite.value("call_id").toString();
    m_peerParty = invite.value("party_id").toString();
    m_version = invite.value("version");
    m_remoteOffer = invite.value("offer").toString();
    m_peerName = invite.value("peer_name").toString();
    m_endReason.clear();
    m_inviteTimer.start(int(left));
    setState(QStringLiteral("incoming"));
    qInfo() << "voip: ringing call taken over from the stand-in, answer" << invite.value("answer").toBool();
    if (invite.value("answer").toBool())
        answer();
}

// ---- the core's events ----------------------------------------------------

void Voip::handleEvent(const QVariantMap &ev)
{
    const QString type = ev.value("type").toString();
    const QVariantMap c = ev.value("content").toMap();
    const QString callId = c.value("call_id").toString();
    const QString party = c.value("party_id").toString();
    const bool own = ev.value("own").toBool();
    // our own events come back from the server
    if (!party.isEmpty() && party == m_partyId)
        return;

    if (type == QLatin1String("m.call.invite")) {
        if (own)
            return;   // our other device is calling someone
        const QString invitee = c.value("invitee").toString();
        if (!invitee.isEmpty() && invitee != m_ownUserId)
            return;
        // for nobody in particular: only in a direct chat (in a big room
        // anyone could ring every member's phone)
        if (invitee.isEmpty() && !ev.value("is_dm").toBool()) {
            qInfo() << "voip: invite for nobody in particular outside a direct chat, not ringing";
            return;
        }
        // the sender's lifetime, within reason (a huge one rang for weeks,
        // past INT_MAX never stopped)
        const qint64 lifetime = qBound(qint64(0), c.value("lifetime").toLongLong(), qint64(Keys::kVoipInviteLifetimeMaxMs));
        const qint64 age = ev.value("age_ms").toLongLong();
        if (age >= lifetime) {
            qInfo() << "voip: invite too old" << age << lifetime;
            return;
        }
        if (m_state != QLatin1String("idle")) {
            // busy: the caller hears so instead of ringing for a minute
            if (callId != m_callId) {
                qInfo() << "voip: invite while busy, answered busy";
                emit command({ { "cmd", "voip_send" }, { "room_id", ev.value("room_id") }, { "type", "m.call.hangup" },
                               { "content", QVariantMap{ { "call_id", callId }, { "party_id", newId() },
                                                         { "version", c.value("version") }, { "reason", "user_busy" } } } });
            }
            return;
        }
        reset();
        m_roomId = ev.value("room_id").toString();
        m_callId = callId;
        m_peerParty = party;
        m_peerUser = ev.value("sender").toString();
        m_version = c.value("version");
        m_remoteOffer = c.value("offer").toMap().value("sdp").toString();
        m_peerName = ev.value("sender_name").toString();
        m_endReason.clear();
        m_inviteTimer.start(int(lifetime - age));
        setState(QStringLiteral("incoming"));
        emit incoming(m_roomId, ev.value("room_name").toString(), m_peerName);
        return;
    }

    if (callId != m_callId || m_state == QLatin1String("idle"))
        return;
    // only the person we call, or who calls us: someone else in the room
    // who saw the call's id cannot answer, hang up or send candidates
    const QString sender = ev.value("sender").toString();
    if (!own && !m_peerUser.isEmpty() && sender != m_peerUser) {
        qInfo() << "voip: an event for this call from someone else in the room, ignored";
        return;
    }

    if (type == QLatin1String("m.call.answer")) {
        if (own) {
            // we answered on another device
            if (m_state == QLatin1String("incoming"))
                finish(QStringLiteral("answered_elsewhere"), false);
            return;
        }
        if (!m_offerer || m_state != QLatin1String("outgoing"))
            return;
        if (!m_invitee.isEmpty() && sender != m_invitee)
            return;
        m_inviteTimer.stop();
        m_peerParty = party;
        m_peerUser = sender;
        setState(QStringLiteral("connecting"));
        applyRemote(QStringLiteral("answer"), c.value("answer").toMap().value("sdp").toString());
        if (version().toString() == QLatin1String("1"))
            send(QStringLiteral("m.call.select_answer"), { { "selected_party_id", party } });
    } else if (type == QLatin1String("m.call.select_answer")) {
        // the caller took another device's answer
        if (!m_offerer && c.value("selected_party_id").toString() != m_partyId)
            finish(QStringLiteral("answered_elsewhere"), false);
    } else if (type == QLatin1String("m.call.candidates")) {
        if (!m_peerParty.isEmpty() && !party.isEmpty() && party != m_peerParty)
            return;
        addRemoteCandidates(c.value("candidates").toList());
    } else if (type == QLatin1String("m.call.hangup")) {
        if (own && m_state == QLatin1String("incoming"))
            finish(QStringLiteral("answered_elsewhere"), false);
        else if (!own && (m_peerParty.isEmpty() || party.isEmpty() || party == m_peerParty))
            finish(QStringLiteral("hangup"), false);
    } else if (type == QLatin1String("m.call.reject")) {
        if (own && m_state == QLatin1String("incoming"))
            finish(QStringLiteral("answered_elsewhere"), false);
        else if (!own && m_offerer && m_state == QLatin1String("outgoing"))
            finish(QStringLiteral("rejected"), false);
    }
}

// ---- the pipeline ---------------------------------------------------------

bool Voip::startPipeline()
{
    stopPipeline();
    m_pipeGen.ref();
    const QString source = m_testTone
            ? QStringLiteral("audiotestsrc is-live=true wave=sine freq=%1 volume=0.3").arg(m_toneHz)
            : QStringLiteral("pulsesrc stream-properties=\"props,media.role=(string)phone\"");
    // with echo cancelling, the microphone goes through this process
    // (capture -> Speex -> aec) and on into the encoder
    const QString aecCaps = QStringLiteral("audio/x-raw,format=S16LE,rate=%1,channels=1,layout=interleaved").arg(Keys::kVoipAecRate);
    const QString mic = m_aec
            ? QStringLiteral("%1 ! audioconvert ! audioresample ! %2 ! appsink name=capture emit-signals=true sync=false "
                             "appsrc name=aec is-live=true format=time do-timestamp=true caps=%2 ! audioconvert ! audioresample")
                  .arg(source, aecCaps)
            : source + QStringLiteral(" ! audioconvert ! audioresample");
    const QString desc = QStringLiteral(
            "webrtcbin name=w bundle-policy=max-bundle latency=%1 "
            "%2 ! volume name=mic ! queue ! opusenc bitrate=%3 ! "
            "rtpopuspay pt=111 ! application/x-rtp,media=audio,encoding-name=OPUS,payload=111,clock-rate=48000 ! w.")
            .arg(Keys::kVoipJitterMs).arg(mic).arg(Keys::kVoipOpusBitrate);
    GError *err = nullptr;
    m_pipe = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (m_pipe)
        gst_object_ref_sink(m_pipe);
    if (err) {
        qWarning() << "voip: pipeline:" << err->message;
        g_error_free(err);
        if (m_pipe)
            gst_object_unref(m_pipe);
        m_pipe = nullptr;
        return false;
    }
    m_webrtc = gst_bin_get_by_name(GST_BIN(m_pipe), "w");
    m_volume = gst_bin_get_by_name(GST_BIN(m_pipe), "mic");
    g_object_set(m_volume, "mute", m_muted ? TRUE : FALSE, nullptr);
    if (m_aec) {
        const int frame = Keys::kVoipAecRate * Keys::kVoipAecFrameMs / 1000;
        m_echo = speex_echo_state_init(frame, Keys::kVoipAecRate * Keys::kVoipAecTailMs / 1000);
        int rate = Keys::kVoipAecRate;
        speex_echo_ctl(m_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
        m_denoise = speex_preprocess_state_init(frame, Keys::kVoipAecRate);
        speex_preprocess_ctl(m_denoise, SPEEX_PREPROCESS_SET_ECHO_STATE, m_echo);
        // only the echo's remains go; noise suppression on top took the
        // far end's tone down 15 dB and made voices crackle (the author)
        int off = 0;
        speex_preprocess_ctl(m_denoise, SPEEX_PREPROCESS_SET_DENOISE, &off);
        speex_preprocess_ctl(m_denoise, SPEEX_PREPROCESS_SET_AGC, &off);
        m_micIn.clear();
        m_farIn.clear();
        m_aecSrc = gst_bin_get_by_name(GST_BIN(m_pipe), "aec");
        GstElement *capture = gst_bin_get_by_name(GST_BIN(m_pipe), "capture");
        g_signal_connect(capture, "new-sample", G_CALLBACK(&Voip::onMicSample), this);
        gst_object_unref(capture);
    }

    // TURN from the homeserver; its host doubles as the STUN server, except
    // with relay only, where STUN would only find the address to hide
    if (m_relayOnly)
        gst_util_set_object_arg(G_OBJECT(m_webrtc), "ice-transport-policy", "relay");
    bool stun = m_relayOnly;
    for (const QString &uri : m_turnUris) {
        const QString url = turnUrl(uri, m_turnUser, m_turnPass);
        if (url.isEmpty())
            continue;
        gboolean ok = FALSE;
        g_signal_emit_by_name(m_webrtc, "add-turn-server", url.toUtf8().constData(), &ok);
        if (!stun && uri.startsWith(QLatin1String("turn:"))) {
            QString host = uri.mid(5).section(QLatin1Char('?'), 0, 0);
            g_object_set(m_webrtc, "stun-server", (QStringLiteral("stun://") + host).toUtf8().constData(), nullptr);
            stun = true;
        }
    }

    g_signal_connect(m_webrtc, "on-negotiation-needed", G_CALLBACK(&Voip::onNegotiationNeeded), this);
    g_signal_connect(m_webrtc, "on-ice-candidate", G_CALLBACK(&Voip::onIceCandidateCb), this);
    g_signal_connect(m_webrtc, "pad-added", G_CALLBACK(&Voip::onPadAdded), this);
    g_signal_connect(m_webrtc, "notify::ice-connection-state", G_CALLBACK(&Voip::onIceStateCb), this);
    g_signal_connect(m_webrtc, "notify::ice-gathering-state", G_CALLBACK(&Voip::onGatheringCb), this);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipe));
    m_busWatch = gst_bus_add_watch(bus, reinterpret_cast<GstBusFunc>(&Voip::onBusMessage), this);
    gst_object_unref(bus);

    if (gst_element_set_state(m_pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "voip: the pipeline does not play";
        stopPipeline();
        return false;
    }
    qInfo() << "voip: pipeline playing, test tone" << m_testTone << "TURN" << m_turnUris.size();
    return true;
}

void Voip::stopPipeline()
{
    if (!m_pipe)
        return;
    // whatever the old pipeline still has queued here is dropped
    m_pipeGen.ref();
    if (m_busWatch) {
        g_source_remove(m_busWatch);
        m_busWatch = 0;
    }
    g_signal_handlers_disconnect_by_data(m_webrtc, this);
    gst_element_set_state(m_pipe, GST_STATE_NULL);
    if (m_aecSrc)
        gst_object_unref(m_aecSrc);
    m_aecSrc = nullptr;
    {
        QMutexLocker lock(&m_aecLock);
        if (m_denoise)
            speex_preprocess_state_destroy(m_denoise);
        if (m_echo)
            speex_echo_state_destroy(m_echo);
        m_denoise = nullptr;
        m_echo = nullptr;
    }
    gst_object_unref(m_volume);
    gst_object_unref(m_webrtc);
    gst_object_unref(m_pipe);
    m_pipe = m_webrtc = m_volume = nullptr;
}

void Voip::applyRemote(const QString &type, const QString &sdp)
{
    if (!m_webrtc)
        return;
    GstSDPMessage *msg = nullptr;
    if (gst_sdp_message_new_from_text(sdp.toUtf8().constData(), &msg) != GST_SDP_OK) {
        qWarning() << "voip: remote SDP does not parse";
        finish(QStringLiteral("ice_failed"), true, QStringLiteral("ice_failed"));
        return;
    }
    const bool offer = type == QLatin1String("offer");
    GstWebRTCSessionDescription *desc = gst_webrtc_session_description_new(
            offer ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, msg);
    g_signal_emit_by_name(m_webrtc, "set-remote-description", desc, nullptr);
    gst_webrtc_session_description_free(desc);
    m_remoteSet = true;
    // candidates that came before the description
    const QVariantList waiting = m_remoteCandidates;
    m_remoteCandidates.clear();
    addRemoteCandidates(waiting);
    if (offer) {
        GstPromise *p = gst_promise_new_with_change_func(&Voip::onAnswerReply, this, nullptr);
        g_signal_emit_by_name(m_webrtc, "create-answer", nullptr, p);
    }
}

void Voip::addRemoteCandidates(const QVariantList &candidates)
{
    if (!m_remoteSet || !m_webrtc) {
        m_remoteCandidates += candidates;
        return;
    }
    const QStringList remoteMids = mids(m_offerer ? QString() : m_remoteOffer);
    for (const QVariant &v : candidates) {
        const QVariantMap c = v.toMap();
        const QString line = c.value("candidate").toString();
        if (line.isEmpty())
            continue;   // the end of the far end's candidates
        int index = c.contains("sdpMLineIndex") ? c.value("sdpMLineIndex").toInt() : -1;
        if (index < 0)
            index = qMax(0, remoteMids.indexOf(c.value("sdpMid").toString()));
        g_signal_emit_by_name(m_webrtc, "add-ice-candidate", guint(index), line.toUtf8().constData());
        ++m_candidatesAdded;
    }
}

void Voip::flushCandidates()
{
    m_candidateTimer.stop();
    if (m_localCandidates.isEmpty() || m_state == QLatin1String("idle"))
        return;
    send(QStringLiteral("m.call.candidates"), { { "candidates", m_localCandidates } });
    m_localCandidates.clear();
}

// ---- GStreamer's callbacks (its threads) ----------------------------------

void Voip::onNegotiationNeeded(GstElement *webrtc, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    if (!v->m_offerer)
        return;   // the answerer waits for the offer
    GstPromise *p = gst_promise_new_with_change_func(&Voip::onOfferReply, self, nullptr);
    g_signal_emit_by_name(webrtc, "create-offer", nullptr, p);
}

static QString takeDescription(GstPromise *promise, const char *field, GstElement *webrtc)
{
    QString text;
    if (gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED) {
        const GstStructure *reply = gst_promise_get_reply(promise);
        GstWebRTCSessionDescription *desc = nullptr;
        if (reply && gst_structure_get(reply, field, GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &desc, nullptr) && desc) {
            g_signal_emit_by_name(webrtc, "set-local-description", desc, nullptr);
            gchar *sdp = gst_sdp_message_as_text(desc->sdp);
            text = QString::fromUtf8(sdp);
            g_free(sdp);
            gst_webrtc_session_description_free(desc);
        }
    }
    gst_promise_unref(promise);
    return text;
}

void Voip::onOfferReply(GstPromise *promise, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    const QString sdp = takeDescription(promise, "offer", v->m_webrtc);
    QMetaObject::invokeMethod(v, "onOfferCreated", Qt::QueuedConnection, Q_ARG(QString, sdp), Q_ARG(int, v->m_pipeGen.load()));
}

void Voip::onAnswerReply(GstPromise *promise, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    const QString sdp = takeDescription(promise, "answer", v->m_webrtc);
    QMetaObject::invokeMethod(v, "onAnswerCreated", Qt::QueuedConnection, Q_ARG(QString, sdp), Q_ARG(int, v->m_pipeGen.load()));
}

void Voip::onIceCandidateCb(GstElement *, unsigned mline, char *candidate, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    QMetaObject::invokeMethod(v, "onIceCandidate", Qt::QueuedConnection,
                              Q_ARG(int, int(mline)), Q_ARG(QString, QString::fromUtf8(candidate)), Q_ARG(int, v->m_pipeGen.load()));
}

void Voip::onIceStateCb(GstElement *webrtc, void *, void *self)
{
    GstWebRTCICEConnectionState state = GST_WEBRTC_ICE_CONNECTION_STATE_NEW;
    g_object_get(webrtc, "ice-connection-state", &state, nullptr);
    Voip *v = static_cast<Voip *>(self);
    QMetaObject::invokeMethod(v, "onIceState", Qt::QueuedConnection, Q_ARG(int, int(state)), Q_ARG(int, v->m_pipeGen.load()));
}

void Voip::onGatheringCb(GstElement *webrtc, void *, void *self)
{
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(webrtc, "ice-gathering-state", &state, nullptr);
    if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE)
        QMetaObject::invokeMethod(static_cast<Voip *>(self), "onIceGatheringDone", Qt::QueuedConnection,
                                  Q_ARG(int, static_cast<Voip *>(self)->m_pipeGen.load()));
}

// the far end's audio: Opus out of RTP, to the call route (or, in a test,
// only measured)
void Voip::onPadAdded(GstElement *, GstPad *pad, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;
    const QString sink = v->m_testTone
            ? QStringLiteral("fakesink sync=false")
            // at full volume: Sailfish's stream restore starts the "phone"
            // role 15 dB down, the far end was barely audible
            : QStringLiteral("pulsesink volume=1.0 stream-properties=\"props,media.role=(string)phone\"");
    // with echo cancelling, what is played also goes to Speex as the echo
    // to take out of the microphone
    const QString ref = v->m_aec
            ? QStringLiteral(" far. ! queue ! audioconvert ! audioresample ! "
                             "audio/x-raw,format=S16LE,rate=%1,channels=1,layout=interleaved ! "
                             "appsink name=farref emit-signals=true sync=false").arg(Keys::kVoipAecRate)
            : QString();
    const QString desc = QStringLiteral(
            "queue ! rtpopusdepay ! opusdec ! audioconvert ! audioresample ! "
            "level interval=%1 post-messages=true ! tee name=far ! queue ! %2%3")
            .arg(qint64(Keys::kVoipLevelMs) * 1000000).arg(sink, ref);
    GError *err = nullptr;
    GstElement *bin = gst_parse_bin_from_description(desc.toUtf8().constData(), TRUE, &err);
    if (err) {
        const QString what = QString::fromUtf8(err->message);
        g_error_free(err);
        if (bin)
            gst_object_unref(bin);
        QMetaObject::invokeMethod(v, "onMediaError", Qt::QueuedConnection, Q_ARG(QString, what), Q_ARG(int, v->m_pipeGen.load()));
        return;
    }
    if (v->m_aec) {
        GstElement *far = gst_bin_get_by_name(GST_BIN(bin), "farref");
        if (far && GST_IS_APP_SINK(far))
            g_signal_connect(far, "new-sample", G_CALLBACK(&Voip::onFarSample), v);
        if (far)
            gst_object_unref(far);
    }
    gst_bin_add(GST_BIN(v->m_pipe), bin);
    gst_element_sync_state_with_parent(bin);
    GstPad *sinkPad = gst_element_get_static_pad(bin, "sink");
    if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK)
        QMetaObject::invokeMethod(v, "onMediaError", Qt::QueuedConnection, Q_ARG(QString, QStringLiteral("the far end's audio does not link")), Q_ARG(int, v->m_pipeGen.load()));
    gst_object_unref(sinkPad);
}

// ---- echo cancelling (Speex), on GStreamer's threads ---------------------

static QByteArray takeSample(GstElement *sink)
{
    QByteArray out;
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
        return out;
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        out = QByteArray(reinterpret_cast<const char *>(map.data), int(map.size));
        gst_buffer_unmap(buf, &map);
    }
    gst_sample_unref(sample);
    return out;
}

// What the far end says, as it goes to the loudspeaker: Speex's echo
// reference, queued until the microphone's next frame takes it. It comes
// in bursts (the network); the queue is kept short, so the echo in the
// microphone stays within what the canceller learns (kVoipAecTailMs).
int Voip::onFarSample(GstElement *sink, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    const QByteArray data = takeSample(sink);
    QMutexLocker lock(&v->m_aecLock);
    if (!v->m_echo)
        return GST_FLOW_OK;
    AecFrames::queueFar(v->m_farIn, data, AecFrames::farQueueBytes());
    return GST_FLOW_OK;
}

// the microphone: the echo out, then noise suppression, on to the encoder
int Voip::onMicSample(GstElement *sink, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    const QByteArray data = takeSample(sink);
    QByteArray clean;
    {
        QMutexLocker lock(&v->m_aecLock);
        if (!v->m_echo || !v->m_aecSrc)
            return GST_FLOW_OK;
        v->m_micIn += data;
        // each microphone frame with one frame of the far end (silence
        // when none came): the two in step, whatever the network does
        clean = AecFrames::pair(v->m_micIn, v->m_farIn, AecFrames::frameBytes(), [v](const char *mic, const char *far, char *out) {
            speex_echo_cancellation(v->m_echo, reinterpret_cast<const spx_int16_t *>(mic),
                                    reinterpret_cast<const spx_int16_t *>(far), reinterpret_cast<spx_int16_t *>(out));
            speex_preprocess_run(v->m_denoise, reinterpret_cast<spx_int16_t *>(out));
        });
    }
    if (clean.isEmpty())
        return GST_FLOW_OK;
    GstBuffer *buf = gst_buffer_new_allocate(nullptr, gsize(clean.size()), nullptr);
    gst_buffer_fill(buf, 0, clean.constData(), gsize(clean.size()));
    gst_app_src_push_buffer(GST_APP_SRC(v->m_aecSrc), buf);
    return GST_FLOW_OK;
}

void Voip::onStatsReply(GstPromise *promise, void *self)
{
    int in = 0, out = 0;
    if (gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED) {
        const GstStructure *reply = gst_promise_get_reply(promise);
        const int n = reply ? gst_structure_n_fields(reply) : 0;
        for (int i = 0; i < n; ++i) {
            const GValue *val = gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
            if (!GST_VALUE_HOLDS_STRUCTURE(val))
                continue;
            const GstStructure *s = gst_value_get_structure(val);
            GstWebRTCStatsType type;
            if (!gst_structure_get(s, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, nullptr))
                continue;
            guint64 packets = 0;
            if (type == GST_WEBRTC_STATS_INBOUND_RTP && gst_structure_get_uint64(s, "packets-received", &packets))
                in += int(packets);
            else if (type == GST_WEBRTC_STATS_OUTBOUND_RTP && gst_structure_get_uint64(s, "packets-sent", &packets))
                out += int(packets);
        }
    }
    gst_promise_unref(promise);
    Voip *v = static_cast<Voip *>(self);
    QMetaObject::invokeMethod(v, "onStats", Qt::QueuedConnection, Q_ARG(int, in), Q_ARG(int, out), Q_ARG(int, v->m_pipeGen.load()));
}

// the bus runs on the main loop (Qt's glib dispatcher)
int Voip::onBusMessage(GstBus *, GstMessage *msg, void *self)
{
    Voip *v = static_cast<Voip *>(self);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gst_message_parse_error(msg, &err, nullptr);
        const QString what = QString::fromUtf8(err ? err->message : "unknown");
        if (err)
            g_error_free(err);
        v->onMediaError(QStringLiteral("%1: %2").arg(QString::fromUtf8(GST_OBJECT_NAME(msg->src)), what), v->m_pipeGen.load());
        break;
    }
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *s = gst_message_get_structure(msg);
        if (s && gst_structure_has_name(s, "level")) {
            const GValue *rms = gst_structure_get_value(s, "rms");
            double db = -100;
            // level gives a GValueArray (deprecated in GLib, still what it sends)
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            if (rms && GST_VALUE_HOLDS_ARRAY(rms) && gst_value_array_get_size(rms) > 0) {
                db = g_value_get_double(gst_value_array_get_value(rms, 0));
            } else if (rms && G_VALUE_HOLDS(rms, g_value_array_get_type())) {
                GValueArray *a = static_cast<GValueArray *>(g_value_get_boxed(rms));
                if (a && a->n_values > 0)
                    db = g_value_get_double(g_value_array_get_nth(a, 0));
            }
G_GNUC_END_IGNORE_DEPRECATIONS
            v->onRemoteLevel(db);
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

// ---- back on this thread --------------------------------------------------

void Voip::onOfferCreated(const QString &sdp, int gen)
{
    if (gen != m_pipeGen.load())
        return;   // from a pipeline that is gone
    if (sdp.isEmpty() || m_state != QLatin1String("outgoing")) {
        if (sdp.isEmpty())
            finish(QStringLiteral("user_media_failed"), false);
        return;
    }
    // webrtcbin should gather only relays then; what it finds anyway stays here
    const QString out = m_relayOnly ? relayOnlySdp(sdp) : sdp;
    QVariantMap content{ { "lifetime", Keys::kVoipInviteLifetimeMs },
                         { "offer", QVariantMap{ { "type", "offer" }, { "sdp", out } } } };
    if (!m_invitee.isEmpty())
        content.insert("invitee", m_invitee);
    send(QStringLiteral("m.call.invite"), content);
    m_inviteTimer.start(Keys::kVoipInviteLifetimeMs);
}

// The core sends each command on its own, so a candidates event could
// overtake the invite or answer it belongs to, and the far end would drop
// it as a call it does not know: candidates wait until the core says the
// invite or answer is out.
void Voip::handleSent(const QVariantMap &ev)
{
    const QString type = ev.value("type").toString();
    if (type == QLatin1String("m.call.hangup") && ev.value("call_id").toString() == m_callId)
        emit hangupSent();
    if (ev.value("call_id").toString() != m_callId || m_state == QLatin1String("idle"))
        return;
    if (type == QLatin1String("m.call.invite") || type == QLatin1String("m.call.answer")) {
        m_descSent = true;
        if (!m_localCandidates.isEmpty() && !m_candidateTimer.isActive())
            m_candidateTimer.start();
    }
}

void Voip::onAnswerCreated(const QString &sdp, int gen)
{
    if (gen != m_pipeGen.load())
        return;
    if (m_state != QLatin1String("connecting"))
        return;
    if (sdp.isEmpty()) {
        finish(QStringLiteral("ice_failed"), true, QStringLiteral("ice_failed"));
        return;
    }
    const QString out = m_relayOnly ? relayOnlySdp(sdp) : sdp;
    send(QStringLiteral("m.call.answer"), { { "answer", QVariantMap{ { "type", "answer" }, { "sdp", out } } } });
}

void Voip::onIceCandidate(int mline, const QString &candidate, int gen)
{
    if (gen != m_pipeGen.load())
        return;
    if (m_state == QLatin1String("idle"))
        return;
    if (m_relayOnly && !isRelayCandidate(candidate))
        return;
    m_localCandidates << QVariantMap{ { "candidate", candidate }, { "sdpMLineIndex", mline } };
    // not before the invite or answer is out (it starts the timer then)
    if (m_descSent && !m_candidateTimer.isActive())
        m_candidateTimer.start();
}

void Voip::onIceGatheringDone(int gen)
{
    if (gen != m_pipeGen.load())
        return;
    // the end of our candidates, as an empty one (VoIP version 1)
    if (m_state == QLatin1String("idle"))
        return;
    m_localCandidates << QVariantMap{ { "candidate", QString() } };
    if (m_descSent && !m_candidateTimer.isActive())
        m_candidateTimer.start();
}

void Voip::onIceState(int state, int gen)
{
    if (gen != m_pipeGen.load())
        return;
    qInfo() << "voip: ICE state" << state;
    if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED || state == GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED) {
        if (m_state == QLatin1String("connecting")) {
            m_connectedAt = nowMs();
            setState(QStringLiteral("connected"));
            m_statsTimer.start();
        }
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_FAILED) {
        finish(QStringLiteral("ice_failed"), true, m_connectedAt ? QStringLiteral("ice_timeout") : QStringLiteral("ice_failed"));
    }
}

void Voip::onRemoteLevel(double rms)
{
    m_lastLevel = rms;
    const qint64 now = nowMs();
    if (now - m_lastLevelLog >= Keys::kVoipStatsMs) {
        m_lastLevelLog = now;
        qInfo() << "voip: far end level" << qRound(rms) << "dB";
    }
}

void Voip::onStats(int packetsIn, int packetsOut, int gen)
{
    if (gen != m_pipeGen.load())
        return;
    m_packetsIn = packetsIn;
    m_packetsOut = packetsOut;
    qInfo() << "voip: packets in" << packetsIn << "out" << packetsOut;
}

void Voip::onMediaError(const QString &what, int gen)
{
    if (gen != m_pipeGen.load())
        return;
    qWarning().noquote() << "voip: media error" << what;
    if (m_state != QLatin1String("idle"))
        finish(QStringLiteral("user_media_failed"), true, QStringLiteral("user_media_failed"));
}
