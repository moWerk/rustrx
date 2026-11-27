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

#include "AecFrames.h"
#include "Keys.h"

#include <QByteArray>
#include <QMap>
#include <QAtomicInt>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

typedef struct _GstElement GstElement;
typedef struct _GstPromise GstPromise;
typedef struct _GstPad GstPad;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;
typedef struct _GstCaps GstCaps;
typedef struct SpeexEchoState_ SpeexEchoState;
typedef struct SpeexPreprocessState_ SpeexPreprocessState;
class CallKeepAlive;

// One-to-one calls the classic Matrix way (m.call.invite, answer,
// candidates, select_answer, hangup, reject; VoIP version 1), with
// GStreamer's webrtcbin for the media: the same ICE, DTLS and SRTP every
// WebRTC client speaks, so Element and nheko can call Rustrx and back.
// The events are ordinary room events, encrypted in encrypted rooms like
// any message. The core only carries them ("voip_send" out, "voip" in)
// and fetches the server's TURN credentials; the call itself is here.
//
// One call at a time. Audio only for now: Opus from the microphone, played
// back on the phone's call route.
class Voip : public QObject
{
    Q_OBJECT
    // "idle", "incoming" (ringing here), "outgoing" (ringing there),
    // "connecting" (answered, media not up yet), "connected"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString roomId READ roomId NOTIFY changed)
    Q_PROPERTY(QString peerName READ peerName NOTIFY changed)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)
    // heard on the loudspeaker instead of the earpiece (every call starts on the earpiece)
    Q_PROPERTY(bool speaker READ speaker WRITE setSpeaker NOTIFY changed)
    // when the media came up (ms since the epoch), 0 before
    Q_PROPERTY(qint64 connectedAt READ connectedAt NOTIFY changed)
    // why the last call ended: "", "hangup", "rejected", "answered_elsewhere",
    // "invite_timeout", "ice_failed", "user_media_failed", "send_failed",
    // "replaced" (a quiet ring that gave way to one that rings)
    Q_PROPERTY(QString endReason READ endReason NOTIFY changed)
    // the call that rings here came in a room of more than two (audit 0.8.4
    // NET-4): its notification only, with no ringtone and no call page
    Q_PROPERTY(bool quiet READ quiet NOTIFY changed)

public:
    explicit Voip(QObject *parent = nullptr);
    ~Voip() override;

    QString state() const { return m_state; }
    QString roomId() const { return m_roomId; }
    QString peerName() const { return m_peerName; }
    bool muted() const { return m_muted; }
    void setMuted(bool on);
    bool speaker() const { return m_speaker; }
    void setSpeaker(bool on);
    qint64 connectedAt() const { return m_connectedAt; }
    QString endReason() const { return m_endReason; }
    bool quiet() const { return m_quiet; }
    // the last call ended as "answered_elsewhere" because another device of
    // ours declined it (a reject or hangup), not answered it: a missed call
    Q_INVOKABLE bool declinedElsewhere() const { return m_declinedElsewhere; }

    void setOwnUserId(const QString &id) { m_ownUserId = id; }

    // call someone in a room (a direct chat: invitee is the other person)
    Q_INVOKABLE void place(const QString &roomId, const QString &invitee, const QString &peerName);
    Q_INVOKABLE void answer();
    Q_INVOKABLE void reject();
    Q_INVOKABLE void hangup();

    // tests: a tone instead of the microphone, and the far end's level and
    // packet counts in the log and in status(); for the next call only
    void setTestTone(bool on, int hz = 440) { m_testTone = on; m_toneHz = hz; }
    // echo cancelling (Speex) on the microphone; on unless a test turns it off
    void setEchoCancelling(bool on) { m_aec = on; }
    // only relay (TURN) candidates: the peer sees the server's address,
    // never this phone's; with no relay to be had the call ends
    void setRelayOnly(bool on) { m_relayOnly = on; }
    // tests: the lifetime our next invites carry (Keys::kVoipInviteLifetimeMs)
    void setInviteLifetime(int ms) { m_inviteLifetime = ms; }
    // held from ringing or calling until the call ends (not owned)
    void setKeepAlive(CallKeepAlive *k) { m_keepAlive = k; }
    QVariantMap status() const;
    // tests: the running call's pipeline, or null
    GstElement *pipeline() const { return m_pipe; }

    // a TURN uri with credentials as webrtcbin takes it, or "" for one it
    // cannot use; the a=mid of each m-line of an SDP (both for tests too)
    static QString turnUrl(const QString &uri, const QString &user, const QString &pass);
    static QStringList mids(const QString &sdp);
    // a candidate through a relay; an SDP without the others' lines
    static bool isRelayCandidate(const QString &candidate);
    static QString relayOnlySdp(const QString &sdp);
    // a candidate's type (host, srflx, prflx, relay), "" for none or another word;
    // the log counts them, never their addresses
    static QString candidateType(const QString &candidate);
    // the far end's limits: a bigger SDP is refused as one that does not
    // parse, candidates past the cap are dropped (a few dozen is a lot)
    static const int kMaxRemoteSdp = 64 * 1024;
    static const int kMaxRemoteCandidates = 200;
    // a pad's caps the far end's player takes: RTP, audio, Opus
    static bool isFarAudio(const GstCaps *caps);

    // a ringing invite as it stands, to hand over from the stand-in to the
    // app (which never saw the event), and taken up there; "answer" in it
    // answers at once (Answer was pressed in the notification)
    QVariantMap ringing() const;
    // quitting: hang up a running call and wait up to waitMs for it to leave
    void endForQuit(int waitMs);
    void takeRinging(const QVariantMap &invite);

    // from the core
    void handleEvent(const QVariantMap &ev);
    void handleTurn(const QVariantMap &ev);
    // the core sent one of our events ("voip_sent"), or could not
    void handleSent(const QVariantMap &ev);
    void sendFailed();

signals:
    void changed();
    void command(const QVariantMap &cmd);
    // an invite arrived: ring (the UI shows the call and a notification)
    // for ringMs: what is left of its lifetime
    void incoming(const QString &roomId, const QString &roomName, const QString &peerName, int ringMs);
    // the call wants the phone's call audio (from placing to the end)
    void audioNeeded(bool on);
    void speakerChanged(bool on);
    // the far end's audio has a player now (its stream follows shortly)
    void farAudioStarted();
    // the core sent this call's hangup
    void hangupSent();

private slots:
    // from GStreamer's threads, queued onto this one
    void onOfferCreated(const QString &sdp, int gen);
    void onAnswerCreated(const QString &sdp, int gen);
    void onIceCandidate(int mline, const QString &candidate, int gen);
    void onIceGatheringDone(int gen);
    void onIceState(int state, int gen);
    void onRemoteLevel(double rms);
    void onStats(int packetsIn, int packetsOut, const QString &pair, int gen);
    void onMediaError(const QString &what, int gen);

private:
    void send(const QString &type, QVariantMap content);
    void setState(const QString &state);
    bool startPipeline();
    void stopPipeline();
    void negotiateAsOfferer();
    bool relayMissing();
    void applyRemote(const QString &type, const QString &sdp);
    void addRemoteCandidates(const QVariantList &candidates);
    void flushCandidates();
    void finish(const QString &reason, bool tellPeer, const QString &wireReason = QString());
    void reset();
    QVariant version() const;
    void logCandidates(const char *when) const;

    static void onNegotiationNeeded(GstElement *webrtc, void * self);
    static void onIceCandidateCb(GstElement *webrtc, unsigned mline, char *candidate, void * self);
    static void onPadAdded(GstElement *webrtc, GstPad *pad, void * self);
    static void onIceStateCb(GstElement *webrtc, void * pspec, void * self);
    static void onGatheringCb(GstElement *webrtc, void * pspec, void * self);
    static void onOfferReply(GstPromise *promise, void * self);
    static void onAnswerReply(GstPromise *promise, void * self);
    static void onStatsReply(GstPromise *promise, void * self);
    static int onBusMessage(GstBus *bus, GstMessage *msg, void * self);
    static int onMicSample(GstElement *sink, void *self);
    static int onFarSample(GstElement *sink, void *self);
    static void onUplinkOverrun(GstElement *queue, void *self);
    static void onDownlinkOverrun(GstElement *queue, void *self);

    QString m_ownUserId;
    QString m_state = QStringLiteral("idle");
    QString m_roomId;
    QString m_callId;
    QString m_partyId;          // ours, for this call
    QString m_peerParty;        // the answer we picked, or the caller's
    QString m_peerUser;         // who that is: events of this call from others are ignored
    QString m_invitee;
    QString m_peerName;
    QString m_remoteOffer;
    QVariant m_version;         // the invite's: 0 (a number) or "1"
    bool m_offerer = false;
    bool m_remoteSet = false;
    bool m_descSent = false;    // our invite or answer is out: candidates may follow
    bool m_muted = false;
    bool m_speaker = false;
    bool m_testTone = false;
    int m_toneHz = 440;
    qint64 m_connectedAt = 0;
    QString m_endReason;
    bool m_declinedElsewhere = false;
    bool m_quiet = false;
    QVariantList m_localCandidates;   // waiting to go out in one event
    QVariantList m_remoteCandidates;  // waiting for the remote description
    QStringList m_turnUris;
    QString m_turnUser, m_turnPass;
    bool m_turnAsked = false;
    bool m_waitingForTurn = false;
    bool m_relayOnly = false;

    GstElement *m_pipe = nullptr;
    GstElement *m_webrtc = nullptr;
    GstElement *m_volume = nullptr;   // the microphone's, for mute
    unsigned m_busWatch = 0;
    // the running pipeline's number: its callbacks carry it, and one from
    // a pipeline that was stopped meanwhile (a call ended) is dropped
    QAtomicInt m_pipeGen;

    // echo cancelling: the microphone and the far end in 10 ms frames
    bool m_aec = true;
    GstElement *m_aecSrc = nullptr;
    SpeexEchoState *m_echo = nullptr;
    SpeexPreprocessState *m_denoise = nullptr;
    QMutex m_aecLock;
    QByteArray m_micIn, m_farIn;
    AecFrames::Clock m_micClock;

    QTimer m_candidateTimer;   // batches candidates into one event
    QTimer m_inviteTimer;      // the invite's lifetime
    QTimer m_statsTimer;       // packet counts while connected
    QTimer m_turnTimer;        // stop waiting for TURN credentials
    QTimer m_iceFailTimer;     // failed ICE ends the call after this
    double m_lastLevel = -100;
    int m_packetsIn = 0, m_packetsOut = 0;
    int m_candidatesAdded = 0;
    int m_inviteLifetime = Keys::kVoipInviteLifetimeMs;
    // candidate types seen this call, ours as gathered and the far end's
    // (trickled or in its SDP), and the pair ICE took ("local/remote")
    QMap<QString, int> m_localTypes, m_remoteTypes;
    QString m_pairTypes;
    bool m_pairLogged = false;   // once at the first stats, "unknown" where webrtcbin names none
    int m_remoteMlines = 0;     // the remote description's m-lines, 0 before it
    // the far end's audio has its player; another pad goes to a fakesink
    QAtomicInt m_farLinked;
    qint64 m_lastLevelLog = 0;
    // sound the leaky queues dropped (before the encoder, before the
    // player), counted on GStreamer's threads; warnings logged sparingly
    QAtomicInt m_dropsUp, m_dropsDown;
    QAtomicInt m_micGaps;   // captured sound missing (AecFrames::Clock)
    qint64 m_lastWarnLog = 0;
    int m_warnings = 0;
    CallKeepAlive *m_keepAlive = nullptr;
    bool m_cpuHeld = false;
};
