// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_WIPE_H
#define RUSTRX_WIPE_H

#include <QByteArray>
#include <cstddef>

// Key material out of memory once it is handed on. A plain memset before
// the buffer goes out of scope is a dead store the compiler may drop; the
// writes through a volatile pointer it must keep. (explicit_bzero would
// do the same, but needs a glibc check on every Sailfish OS release.)
namespace Wipe {

inline void bytes(void *p, std::size_t n)
{
    volatile unsigned char *v = static_cast<volatile unsigned char *>(p);
    while (n--)
        *v++ = 0;
}

// the array's buffer, then the array emptied. Only where this array is
// the buffer's one owner: a shared one detaches in data(), and only the
// copy is wiped.
inline void array(QByteArray &a)
{
    if (!a.isEmpty())
        bytes(a.data(), std::size_t(a.size()));
    a.clear();
}

}

#endif
