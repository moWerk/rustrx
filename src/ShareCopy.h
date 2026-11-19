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
    // created new: an earlier copy of the same name goes, and a link put
    // in its place is not followed
    ::unlink(local.constData());
    const int out = ::open(local.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (out < 0) {
        qWarning() << "share: cannot copy a shared file";
        return QString();
    }
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
        for (ssize_t done = 0; done < n;) {
            const ssize_t w = ::write(out, buf.constData() + done, size_t(n - done));
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0) {
                ok = false;
                break;
            }
            done += w;
        }
        if (!ok)
            break;
    }
    ::close(out);
    if (!ok) {
        ::unlink(local.constData());
        return QString();
    }
    qInfo() << "share: received" << total << "bytes";
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
