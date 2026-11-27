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
#ifndef NAMEPROOF_H
#define NAMEPROOF_H

#include "Keys.h"
#include "NotifyGate.h"

#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <fcntl.h>
#include <unistd.h>

// Whether the process that holds one of Rustrx's bus names is a Rustrx
// (audit 0.8.4 IPC2 for the push connector's name, IPC6 for the app's).
// Another app may take either name first; a check of its PID cannot work
// from inside the sandbox (NotifyGate). So the holder is asked to prove
// that it can read the data folder's secret (NotifyGate::secret), without
// the secret ever going over the bus: it answers a fresh nonce with an
// HMAC over the name, its own unique bus name and the nonce. A squatter
// cannot make one; relaying the question to a real Rustrx does not help,
// because a Rustrx answers only for a name it holds itself, and the
// answer is bound to the unique name that gave it.
namespace NameProof {

// HMAC-SHA256 under the secret, over what is proven ("" without a secret)
inline QString answer(const QString &secret, const QString &name, const QString &owner, const QString &nonce)
{
    if (secret.isEmpty())
        return QString();
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, secret.toLatin1());
    mac.addData(name.toUtf8());
    mac.addData("\n", 1);
    mac.addData(owner.toUtf8());
    mac.addData("\n", 1);
    mac.addData(nonce.toUtf8());
    return QString::fromLatin1(mac.result().toHex());
}

inline QString nonce()
{
    QByteArray raw(32, 0);
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly) || random.read(raw.data(), raw.size()) != raw.size())
        return QString();
    return QString::fromLatin1(raw.toHex());
}

// A second launch's way to make the stand-in leave (activateWith): proof
// of the secret bound to the stand-in's unique name, so the holder of the
// name learns nothing it could use elsewhere, a squatter included
inline QString leaveToken(const QString &secret, const QString &owner)
{
    return answer(secret, QStringLiteral("activate"), owner, QString());
}

// The answering side, at Keys::kProofPath in every Rustrx process, from
// the moment it holds its bus name: proof only for one of `names` that
// this connection holds itself.
class Responder : public QObject, protected QDBusContext
{
    Q_OBJECT
    // Keys::kProofInterface; Q_CLASSINFO takes a literal only
    Q_CLASSINFO("D-Bus Interface", "net.mowerk.harbour_rustrx.Proof")
public:
    Responder(const QString &secretPath, const QStringList &names, QObject *parent = nullptr)
        : QObject(parent), m_secretPath(secretPath), m_names(names) {}
public slots:
    Q_SCRIPTABLE QString proveName(const QString &name, const QString &nonce)
    {
        if (!calledFromDBus() || !m_names.contains(name) || nonce.size() < 32 || nonce.size() > 128)
            return QString();
        const QString self = connection().baseService();
        if (connection().interface()->serviceOwner(name).value() != self)
            return QString();
        return answer(NotifyGate::secret(m_secretPath), name, self, nonce);
    }
private:
    QString m_secretPath;
    QStringList m_names;
};

enum class Holder {
    Free,     // nobody holds the name (any more)
    Rustrx,   // a Rustrx process does, by its proof
    Other     // something else: a wrong answer, or none within the wait
};

// The asking side, without blocking: done() once. untilFree: a proven
// Rustrx is waited for until it lets go (a process on its way out still
// holds the connector's name for a moment), done(Rustrx) only if it still
// holds the name at the end of the wait.
class Check : public QObject
{
    Q_OBJECT
public:
    Check(const QDBusConnection &bus, const QString &name, const QString &secret, int waitMs, bool untilFree,
          QObject *parent = nullptr)
        : QObject(parent), m_bus(bus), m_name(name), m_secret(secret), m_waitMs(waitMs), m_untilFree(untilFree)
    {
        m_poll.setSingleShot(true);
        connect(&m_poll, &QTimer::timeout, this, &Check::ask);
    }
    void start()
    {
        m_clock.start();
        ask();
    }

signals:
    void done(NameProof::Holder holder);

private:
    int left() const { return int(qMax<qint64>(0, m_waitMs - m_clock.elapsed())); }
    void finish(Holder h)
    {
        if (m_done)
            return;
        m_done = true;
        m_poll.stop();
        emit done(h);
    }
    void again()
    {
        if (left() <= 0) {
            finish(m_proven ? Holder::Rustrx : Holder::Other);
            return;
        }
        m_poll.start(qMin(Keys::kProofPollMs, left()));
    }
    void ask()
    {
        if (m_done)
            return;
        QDBusMessage owner = QDBusMessage::createMethodCall(QString::fromLatin1(Keys::kDbusService), QString::fromLatin1(Keys::kDbusPath),
                                                            QString::fromLatin1(Keys::kDbusService), QStringLiteral("GetNameOwner"));
        owner << m_name;
        auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(owner, qMax(1, left())), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QString> reply = *w;
            w->deleteLater();
            if (reply.isError()) {
                if (reply.error().name() == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
                    finish(Holder::Free);
                else
                    again();
                return;
            }
            prove(reply.value());
        });
    }
    void prove(const QString &owner)
    {
        if (owner == m_bus.baseService()) {
            finish(Holder::Rustrx);
            return;
        }
        // no secret to check against (the data folder unwritable): no
        // squat can be told from a Rustrx, and none is claimed
        if (m_secret.isEmpty()) {
            qWarning() << "name proof: no secret to check" << m_name << "with";
            finish(Holder::Rustrx);
            return;
        }
        const QString n = nonce();
        QDBusMessage call = QDBusMessage::createMethodCall(owner, QString::fromLatin1(Keys::kProofPath),
                                                           QString::fromLatin1(Keys::kProofInterface), QStringLiteral("proveName"));
        call << m_name << n;
        call.setAutoStartService(false);
        auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, qMax(1, left())), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, owner, n](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QString> reply = *w;
            w->deleteLater();
            // still starting (no object yet), busy, or gone meanwhile: asked again
            if (reply.isError() || reply.value().isEmpty()) {
                again();
                return;
            }
            if (!NotifyGate::sameSecret(answer(m_secret, m_name, owner, n), reply.value())) {
                qWarning().noquote() << "name proof:" << m_name << "is held by" << owner << "with a wrong proof";
                finish(Holder::Other);
                return;
            }
            m_proven = true;
            if (m_untilFree)
                again();
            else
                finish(Holder::Rustrx);
        });
    }

    QDBusConnection m_bus;
    QString m_name;
    QString m_secret;
    int m_waitMs;
    bool m_untilFree;
    bool m_proven = false;
    bool m_done = false;
    QElapsedTimer m_clock;
    QTimer m_poll;
};

// the same, waiting for the answer (only where nothing else must run
// meanwhile: a push-woken process that will leave)
inline Holder holderOf(const QDBusConnection &bus, const QString &name, const QString &secret, int waitMs)
{
    Check check(bus, name, secret, waitMs, false);
    Holder holder = Holder::Other;
    QEventLoop loop;
    QObject::connect(&check, &Check::done, &loop, [&](Holder h) {
        holder = h;
        loop.quit();
    });
    check.start();
    loop.exec();
    return holder;
}

// The first time in this boot that the note at `path` is asked about
// (boot_id, or once a day where it cannot be read); it is noted then.
inline bool firstThisBoot(const QString &path, const QString &bootIdPath = QStringLiteral("/proc/sys/kernel/random/boot_id"))
{
    QFile id(bootIdPath);
    QByteArray boot = id.open(QIODevice::ReadOnly) ? id.read(64).trimmed() : QByteArray();
    QFile note(path);
    if (note.open(QIODevice::ReadOnly)) {
        const QByteArray seen = note.read(64).trimmed();
        note.close();
        if (!boot.isEmpty() && seen == boot)
            return false;
        if (boot.isEmpty() && QFileInfo(path).lastModified().msecsTo(QDateTime::currentDateTime()) < 24 * 3600 * 1000)
            return false;
    }
    if (boot.isEmpty())
        boot = "unknown";
    const QByteArray file = QFile::encodeName(path);
    ::unlink(file.constData());
    const int fd = ::open(file.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0) {
        const ssize_t n = ::write(fd, boot.constData(), size_t(boot.size()));
        Q_UNUSED(n);
        ::close(fd);
    }
    return true;
}

} // namespace NameProof

#endif
