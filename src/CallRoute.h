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

#include <QObject>
#include <QString>

struct pa_threaded_mainloop;
struct pa_context;

// Where a call is heard, on phones with Android's audio (droid): Sailfish's
// audio policy keeps an app's streams on the loudspeaker as media, and
// does not give apps its call mode. During a call this switches the
// phone's card to its communication mode (the one Android VoIP apps use:
// earpiece, and on many phones the phone's own echo cancelling) and picks
// the earpiece or the loudspeaker; afterwards both go back as they were.
// The call's streams join the policy's "aliencall" group (App Support's
// calls) by the rule rpm/harbour-rustrx-xpolicy.conf installs, so the
// policy leaves them there. A phone without a communication mode (the
// mainline Pro¹-X) is left alone.
class CallRoute : public QObject
{
    Q_OBJECT
public:
    explicit CallRoute(QObject *parent = nullptr);
    ~CallRoute() override;

    void setActive(bool on);
    void setSpeaker(bool on);

private:
    bool connectPulse();
    void apply();
    void restore();

    pa_threaded_mainloop *m_loop = nullptr;
    pa_context *m_ctx = nullptr;
    bool m_active = false;
    bool m_speaker = false;
    // what was there before the call
    QString m_card, m_cardProfile, m_sink, m_sinkPort;
    QString m_headsetPort;   // a plugged headset's port, if any
};
