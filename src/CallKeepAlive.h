// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_CALLKEEPALIVE_H
#define RUSTRX_CALLKEEPALIVE_H

// Keeps the CPU up while a call is not idle (Voip holds it): MCE's cpu
// keepalive in the app (CallState), a stand-in that only counts in
// tests/cpp/voip.
class CallKeepAlive
{
public:
    virtual ~CallKeepAlive() {}
    virtual void holdCpu(bool on) = 0;
};

#endif
