// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_CALLROUTEPLAN_H
#define RUSTRX_CALLROUTEPLAN_H

#include <QList>
#include <QString>
#include <QStringList>

// CallRoute's choices, apart from PulseAudio (tested in tests/cpp/callroute):
// which card goes into its communication mode, which sink and port the
// call is heard on, and where the sink goes back to afterwards.
namespace CallRoutePlan {

static const char kCommunication[] = "communication";
static const char kEarpiece[] = "output-earpiece";
static const char kSpeaker[] = "output-speaker";
static const char kHeadset[] = "output-wired_headset";
static const char kHeadphone[] = "output-wired_headphone";
static const char kParking[] = "output-parking";

struct Card { QString name, active; QStringList profiles; };
// ports: all of them; available: those not known to be unplugged
struct Sink { QString name, active; QStringList ports, available; };

// the card with a communication mode (the last one listed, as before),
// or -1: a phone without one is left alone
inline int card(const QList<Card> &cards)
{
    int found = -1;
    for (int i = 0; i < cards.size(); ++i)
        if (cards.at(i).profiles.contains(QLatin1String(kCommunication)))
            found = i;
    return found;
}

// the first sink with an earpiece, or -1
inline int sink(const QList<Sink> &sinks)
{
    for (int i = 0; i < sinks.size(); ++i)
        if (sinks.at(i).ports.contains(QLatin1String(kEarpiece)))
            return i;
    return -1;
}

// a plugged headset's port on that sink (the call is heard there, not on
// the earpiece), or ""
inline QString headsetPort(const Sink &s)
{
    if (s.available.contains(QLatin1String(kHeadset)))
        return QLatin1String(kHeadset);
    if (s.available.contains(QLatin1String(kHeadphone)))
        return QLatin1String(kHeadphone);
    return QString();
}

// where the call is heard
inline QString callPort(bool speaker, const QString &headset)
{
    if (speaker)
        return QLatin1String(kSpeaker);
    return headset.isEmpty() ? QLatin1String(kEarpiece) : headset;
}

// Mainline ports (ALSA UCM, the Pro1-X #2): each output is a profile of
// its own, "HiFi (Earpiece, Mic)", "HiFi (Mic, Speaker)", "HiFi
// (Headphones, Mic)", so the call switches profiles and no port. The
// "Voice Call (...)" ones are the modem's DSP path and stay the policy's
// (memeline session, 2026-11-19: xpolicy.conf maps card_profile_earpiece
// to "HiFi (Earpiece, Mic)" there).
static const char kUcmHiFi[] = "HiFi (";

// the card's HiFi profile for one output ("Earpiece", "Speaker",
// "Headphones"), or ""
inline QString ucmProfile(const Card &c, const char *output)
{
    for (const QString &p : c.profiles)
        if (p.startsWith(QLatin1String(kUcmHiFi)) && p.contains(QLatin1String(output)))
            return p;
    return QString();
}

// a card with a HiFi earpiece profile (the last one listed), or -1
inline int ucmCard(const QList<Card> &cards)
{
    int found = -1;
    for (int i = 0; i < cards.size(); ++i)
        if (!ucmProfile(cards.at(i), "Earpiece").isEmpty())
            found = i;
    return found;
}

// the profile the call is heard on: headphones when they were the output
// before the call (their jack does not report itself there), else the
// earpiece, or the loudspeaker on the speaker button
inline QString ucmCallProfile(const Card &c, const QString &before, bool speaker)
{
    if (before.startsWith(QLatin1String(kUcmHiFi)) && before.contains(QLatin1String("Headphones")))
        return before;
    const QString p = ucmProfile(c, speaker ? "Speaker" : "Earpiece");
    return p.isEmpty() ? before : p;
}

// where the sink goes back to: "parking" is no output at all, the
// loudspeaker instead (the policy moves it on from there for whatever
// plays next)
inline QString restorePort(const QString &saved)
{
    return saved.isEmpty() || saved == QLatin1String(kParking) ? QLatin1String(kSpeaker) : saved;
}

}

#endif
