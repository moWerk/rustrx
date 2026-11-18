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
#include "Media.h"
#include "Exif.h"
#include "Keys.h"
#include "Mp4.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

// pictures prepared for sending (scaled, thumbnails, avatars)
static QString outgoingDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + QLatin1String(Keys::kOutgoingDir);
    QDir().mkpath(dir);
    return dir;
}

Media::Media(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QString Media::localPath(const QString &path)
{
    return path.startsWith(QLatin1String("file://")) ? QUrl(path).toLocalFile() : path;
}

bool Media::downscaleImages() const
{
    return m_settings->value(QLatin1String(Keys::kDownscaleImages), Keys::kDownscaleImagesDefault).toBool();
}

void Media::setDownscaleImages(bool on)
{
    if (on == downscaleImages())
        return;
    m_settings->setValue(QLatin1String(Keys::kDownscaleImages), on);
    emit settingsChanged();
}

bool Media::stripLocation() const
{
    return m_settings->value(QLatin1String(Keys::kStripLocation), Keys::kStripLocationDefault).toBool();
}

void Media::setStripLocation(bool on)
{
    if (on == stripLocation())
        return;
    m_settings->setValue(QLatin1String(Keys::kStripLocation), on);
    emit settingsChanged();
}

QString Media::emotePath(const QString &mxc)
{
    const QString path = m_emotePaths.value(mxc);
    if (!path.isEmpty())
        return QStringLiteral("file://") + path;
    if (mxc.startsWith(QLatin1String("mxc://")) && !m_emotesAsked.contains(mxc)) {
        m_emotesAsked.insert(mxc);
        emit command({ { "cmd", "fetch_emote" }, { "mxc", mxc } });
    }
    return QString();
}

void Media::emoteArrived(const QString &mxc, const QString &path)
{
    m_emotePaths.insert(mxc, path);
    emit emoteReady(mxc, QStringLiteral("file://") + path);
}

void Media::clearEmotes()
{
    m_emotePaths.clear();
    m_emotesAsked.clear();
}

QString Media::prepareAvatar(const QString &path, const QString &cmd, QSize *size)
{
    QImageReader reader(localPath(path));
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        emit errorOccurred(cmd, reader.errorString());
        return QString();
    }
    if (qMax(image.width(), image.height()) > Keys::kAvatarMaxEdge)
        image = image.scaled(Keys::kAvatarMaxEdge, Keys::kAvatarMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // PNG keeps a transparent picture (an icon) as it is; photos go as JPEG
    const bool alpha = image.hasAlphaChannel();
    const QString out = outgoingDir() + "/avatar-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + (alpha ? ".png" : ".jpg");
    if (!image.save(out, alpha ? "PNG" : "JPG", alpha ? -1 : Keys::kAvatarJpegQuality)) {
        emit errorOccurred(cmd, QStringLiteral("cannot write ") + out);
        return QString();
    }
    if (size)
        *size = image.size();
    return out;
}

void Media::setRoomAvatar(const QString &roomId, const QString &path)
{
    QSize size;
    const QString out = prepareAvatar(path, QStringLiteral("set_room_avatar"), &size);
    if (out.isEmpty())
        return;
    emit command({{"cmd", "set_room_avatar"}, {"room_id", roomId}, {"path", out},
                  {"w", size.width()}, {"h", size.height()}});
}

void Media::setOwnAvatar(const QString &path)
{
    const QString out = prepareAvatar(path, QStringLiteral("set_own_avatar"), nullptr);
    if (!out.isEmpty())
        emit command({{"cmd", "set_own_avatar"}, {"path", out}});
}

void Media::sendFile(const QString &roomId, const QString &path, const QString &caption,
                     const QString &threadRoot)
{
    const QString p = localPath(path);
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", p}};
    if (!threadRoot.isEmpty())
        cmd.insert("thread_root", threadRoot);
    if (!caption.trimmed().isEmpty())
        cmd.insert("caption", caption.trimmed());

    // pictures go out the way Element expects them: upright, at most 2048 px
    // on the long side when the setting asks for it, with their size and a
    // thumbnail, so the other side shows a preview instead of a bare file
    QImageReader reader(p);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format();
    if (!format.isEmpty() && format != "gif") {
        QImage image = reader.read();
        if (!image.isNull()) {
            const QString stem = outgoingDir() + "/" + QString::number(QDateTime::currentMSecsSinceEpoch());
            const bool shrink = downscaleImages()
                    && qMax(image.width(), image.height()) > Keys::kUploadMaxEdge;
            if (shrink) {
                // re-encoded: no metadata at all, the place neither
                image = image.scaled(Keys::kUploadMaxEdge, Keys::kUploadMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QString out = stem + ".jpg";
                if (image.save(out, "JPEG", Keys::kUploadJpegQuality))
                    cmd.insert("path", out);
            } else if (stripLocation() && format == "jpeg") {
                // as it is, the place out of its EXIF; one that cannot be
                // read is re-encoded, so no place slips through
                QFile in(p);
                QByteArray bytes = in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
                const QString out = stem + ".jpg";
                QFile file(out);
                if (!bytes.isEmpty() && Exif::stripLocation(bytes) && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()) {
                    file.close();
                    cmd.insert("path", out);
                } else if (image.save(out, "JPEG", Keys::kReencodeJpegQuality)) {
                    cmd.insert("path", out);
                }
            }
            cmd.insert("w", image.width());
            cmd.insert("h", image.height());
            const QImage thumb = image.scaled(Keys::kThumbEdge, Keys::kThumbEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QString thumbPath = stem + "-thumb.jpg";
            if (thumb.save(thumbPath, "JPEG", Keys::kThumbJpegQuality)) {
                cmd.insert("thumb_path", thumbPath);
                cmd.insert("thumb_w", thumb.width());
                cmd.insert("thumb_h", thumb.height());
            }
        }
    }
    // the type from the content too: a shared file's name may have no
    // extension the core knows, and a picture then went out as a file
    cmd.insert("mime", QMimeDatabase().mimeTypeForFile(cmd.value("path").toString()).name());
    m_outgoing.append({ cmd, nullptr });
    if (cmd.value("mime").toString().startsWith(QLatin1String("video/")))
        prepareVideo(m_outgoing.last().cmd, outgoingDir() + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()));
    sendInOrder();
}

// videos go out the way other clients send them: with their size, their
// length and a frame as thumbnail, so the other side shows a preview
void Media::prepareVideo(QVariantMap &cmd, const QString &stem)
{
    const QString path = cmd.value("path").toString();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const Mp4::Info info = Mp4::read(file);
        if (info.width > 0 && info.height > 0) {
            cmd.insert("w", info.width);
            cmd.insert("h", info.height);
        }
        if (info.durationMs >= 0)
            cmd.insert("duration_ms", info.durationMs);
    }

    const QString thumbPath = stem + "-thumb.jpg";
    auto *frame = new QProcess(this);
    m_outgoing.last().frame = frame;
    auto done = [this, frame, thumbPath](bool ok) {
        for (Outgoing &o : m_outgoing) {
            if (o.frame != frame)
                continue;
            o.frame = nullptr;
            const QImage thumb(thumbPath);
            if (ok && !thumb.isNull()) {
                o.cmd.insert("thumb_path", thumbPath);
                o.cmd.insert("thumb_w", thumb.width());
                o.cmd.insert("thumb_h", thumb.height());
            } else {
                QFile::remove(thumbPath);
            }
            break;
        }
        frame->deleteLater();
        sendInOrder();
    };
    connect(frame, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    // a crash or a kill ends in finished too; a helper that never started does not
    connect(frame, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done(false);
    });
    // upright (the helper reads the rotation), within the picture thumbnail's box
    frame->start(QLatin1String(Keys::kVideoFrameTool),
                 { "-w", QString::number(Keys::kThumbEdge), "-h", QString::number(Keys::kThumbEdge),
                   "-o", thumbPath, path });
    QTimer::singleShot(Keys::kVideoFrameMs, frame, [frame]() { frame->kill(); });
}

void Media::sendInOrder()
{
    while (!m_outgoing.isEmpty() && !m_outgoing.first().frame)
        emit command(m_outgoing.takeFirst().cmd);
}

void Media::sendVoice(const QString &roomId, const QString &path, qint64 durationMs, const QString &caption)
{
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", path}, {"voice", true}, {"duration_ms", durationMs}};
    if (!caption.isEmpty())
        cmd.insert("caption", caption);
    m_outgoing.append({ cmd, nullptr });
    sendInOrder();
}

void Media::getMedia(const QString &roomId, const QString &eventId, bool thumb)
{
    emit command({{"cmd", "get_media"}, {"room_id", roomId}, {"event_id", eventId}, {"thumb", thumb}});
}

void Media::exportMedia(const QString &roomId, const QString &eventId)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QLatin1Char('/') + QLatin1String(Keys::kDownloadDir);
    emit command({{"cmd", "get_media"}, {"room_id", roomId}, {"event_id", eventId}, {"thumb", false}, {"dest_dir", dir}});
}
