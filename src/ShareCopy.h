// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_SHARECOPY_H
#define RUSTRX_SHARECOPY_H

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// A shared file goes into Rustrx's own cache when it is shared, and the
// copy is what is sent: a path checked now and read later could meanwhile
// be swapped for a link to Rustrx's own files (audit S7). The share
// sheet's descriptors were copied the same way already.
namespace ShareCopy {

// what a copy is called in dir: the name's last part, or the fallback
inline QString target(const QString &dir, const QString &name, const QString &fallback)
{
    QString file = QFileInfo(name).fileName();
    if (file.isEmpty() || file == QLatin1String(".") || file == QLatin1String(".."))
        file = fallback;
    return dir + QLatin1Char('/') + file;
}

// a new folder of its own under base for one copy (mkdtemp, 0700), so that
// copies of the same name, from one share or another, or one still waiting
// for the handover, never meet (audit IPC5); "" when it cannot be made
inline QString freshDir(const QString &base)
{
    QDir().mkpath(base);
    QByteArray tmpl = QFile::encodeName(base + QLatin1String("/XXXXXX"));
    if (!::mkdtemp(tmpl.data())) {
        qWarning() << "share: cannot make a folder for a shared file";
        return QString();
    }
    return QFile::decodeName(tmpl);
}

namespace detail {
// all of len bytes to out; false on a write error
inline bool writeAll(int out, const char *data, qint64 len)
{
    for (qint64 done = 0; done < len;) {
        const ssize_t w = ::write(out, data + done, size_t(len - done));
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return false;
        done += w;
    }
    return true;
}

// path made as a new file: a file or a link already there is neither
// replaced nor followed; -1 then
inline int create(const QByteArray &local)
{
    const int out = ::open(local.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (out < 0)
        qWarning() << "share: cannot copy a shared file";
    return out;
}
}

// the open file fd (left open) into dir as name, a new file there; at most
// maxBytes. The copy's path, or "" (nothing is left behind then).
inline QString fromDescriptor(int fd, const QString &dir, const QString &name, const QString &fallback,
                              qint64 maxBytes, int chunk)
{
    if (fd < 0)
        return QString();
    QDir().mkpath(dir);
    const QString path = target(dir, name, fallback);
    const QByteArray local = QFile::encodeName(path);
    const int out = detail::create(local);
    if (out < 0)
        return QString();
    ::lseek(fd, 0, SEEK_SET);
    QByteArray buf(chunk, Qt::Uninitialized);
    qint64 total = 0;
    bool ok = true;
    for (;;) {
        const ssize_t n = ::read(fd, buf.data(), size_t(buf.size()));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = n == 0;
            break;
        }
        total += n;
        if (total > maxBytes) {
            qWarning() << "share: larger than" << (maxBytes >> 20) << "MB, not taken";
            ok = false;
            break;
        }
        if (!detail::writeAll(out, buf.constData(), n)) {
            ok = false;
            break;
        }
    }
    ::close(out);
    if (!ok) {
        ::unlink(local.constData());
        return QString();
    }
    qInfo() << "share: received" << total << "bytes";
    return path;
}

// bytes the share itself carries (its "data") into dir as name, made the
// same way as a copy, with the same cap
inline QString fromBytes(const QByteArray &data, const QString &dir, const QString &name, const QString &fallback,
                         qint64 maxBytes)
{
    if (data.size() > maxBytes) {
        qWarning() << "share: larger than" << (maxBytes >> 20) << "MB, not taken";
        return QString();
    }
    QDir().mkpath(dir);
    const QString path = target(dir, name, fallback);
    const QByteArray local = QFile::encodeName(path);
    const int out = detail::create(local);
    if (out < 0)
        return QString();
    const bool ok = detail::writeAll(out, data.constData(), data.size());
    ::close(out);
    if (!ok) {
        ::unlink(local.constData());
        return QString();
    }
    qInfo() << "share: received" << data.size() << "bytes";
    return path;
}

// A path another app names: opened without following a link at its end,
// a regular file only, and only where the file it really is passes
// mayShare (a link further up the path is resolved by the kernel, so the
// check is on what was opened, not on the name). Then copied as above.
template <typename MayShare>
inline QString fromPath(const QString &source, const QString &dir, const QString &fallback,
                        qint64 maxBytes, int chunk, MayShare mayShare)
{
    const int fd = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        qWarning() << "share: refused," << (errno == ELOOP ? "a link" : "cannot be opened");
        return QString();
    }
    struct stat st;
    QString path;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        qWarning() << "share: refused, not a regular file";
    } else {
        const QString real = QFileInfo(QStringLiteral("/proc/self/fd/%1").arg(fd)).symLinkTarget();
        if (real.isEmpty() || !mayShare(real))
            qWarning() << "share: refused, a file of this app";
        else
            path = fromDescriptor(fd, dir, QFileInfo(source).fileName(), fallback, maxBytes, chunk);
    }
    ::close(fd);
    return path;
}

}

#endif
