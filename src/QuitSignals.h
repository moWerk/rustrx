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
#ifndef QUITSIGNALS_H
#define QUITSIGNALS_H

#include <QEvent>
#include <QObject>
#include <QSocketNotifier>

#include <csignal>
#include <cerrno>
#include <functional>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// SIGTERM (systemctl --user stop, the session's end), SIGINT and SIGHUP
// end Rustrx the way its Quit does, not at once: the core stops before the
// bus name goes (the store's lock). The handler only writes the signal's
// number into a socket pair; the event loop reads it there and quits. The
// first signal counts, later ones are read and dropped: the quit is running.
namespace QuitSignals {

inline int *fds()
{
    static int pair[2] = { -1, -1 };
    return pair;
}

inline volatile sig_atomic_t &received()
{
    static volatile sig_atomic_t sig = 0;
    return sig;
}

inline void handler(int sig)
{
    const int saved = errno;
    if (!received())
        received() = sig;
    const char c = char(sig);
    if (::write(fds()[0], &c, 1) < 0) {
        // full or closed: the first one is in already
    }
    errno = saved;
}

// the handlers, once the process holds what a quit must give back (its bus
// name); a signal before the event loop runs waits in the socket
inline bool install()
{
    int *pair = fds();
    if (pair[0] >= 0)
        return true;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        return false;
    ::fcntl(pair[0], F_SETFL, ::fcntl(pair[0], F_GETFL) | O_NONBLOCK);
    ::fcntl(pair[1], F_SETFL, ::fcntl(pair[1], F_GETFL) | O_NONBLOCK);
    struct sigaction sa = {};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : { SIGTERM, SIGINT, SIGHUP })
        ::sigaction(sig, &sa, nullptr);
    return true;
}

// reads the socket on its thread's event loop (its own event(): the
// activated signal's overloads differ between Qt 5.6 and 5.15)
class Watcher : public QSocketNotifier
{
public:
    Watcher(int fd, const std::function<void(int)> &onQuit, QObject *parent)
        : QSocketNotifier(fd, QSocketNotifier::Read, parent), m_onQuit(onQuit) {}

protected:
    bool event(QEvent *e) override
    {
        if (e->type() != QEvent::SockAct)
            return QSocketNotifier::event(e);
        char buf[16];
        while (::read(socket(), buf, sizeof buf) > 0) {
        }
        if (!m_done) {
            m_done = true;
            m_onQuit(int(received()));
        }
        return true;
    }

private:
    std::function<void(int)> m_onQuit;
    bool m_done = false;
};

// onQuit(signal) runs on parent's thread, once, for the first signal
inline QSocketNotifier *watch(QObject *parent, const std::function<void(int)> &onQuit)
{
    if (fds()[1] < 0)
        return nullptr;
    return new Watcher(fds()[1], onQuit, parent);
}

} // namespace QuitSignals

#endif
