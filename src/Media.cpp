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
#include "Png.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QRunnable>
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

namespace {

// a video's metadata out of a copy of it (Mp4::strip), off the GUI thread:
// a copy of a long video takes seconds. The result goes back queued.
class VideoStrip : public QRunnable
{
public:
    VideoStrip(QObject *media, int id, const QString &in, const QString &out, const QString &stem, int level)
        : m_media(media), m_id(id), m_in(in), m_out(out), m_stem(stem), m_level(level) {}

    void run() override
    {
        QFile::remove(m_out);
        // the copy writable even when the original is not
        bool ok = QFile::copy(m_in, m_out)
                && QFile::setPermissions(m_out, QFile::ReadOwner | QFile::WriteOwner);
        if (ok) {
            QFile file(m_out);
            ok = file.open(QIODevice::ReadWrite) && Mp4::strip(file, m_level);
        }
        if (!ok)
            QFile::remove(m_out);
        QMetaObject::invokeMethod(m_media, "videoStripped", Qt::QueuedConnection,
                                  Q_ARG(int, m_id), Q_ARG(bool, ok), Q_ARG(QString, m_out), Q_ARG(QString, m_stem));
    }

private:
    QObject *m_media;
    int m_id;
    QString m_in, m_out, m_stem;
    int m_level;
};

}

Media::Media(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // the switch before the levels: on meant all of it
    const QString old = QLatin1String(Keys::kStripLocationOld);
    if (m_settings->contains(old)) {
        if (!m_settings->contains(QLatin1String(Keys::kMetadataLevel)))
            m_settings->setValue(QLatin1String(Keys::kMetadataLevel), m_settings->value(old).toBool() ? 2 : 0);
        m_settings->remove(old);
    }
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

int Media::metadataLevel() const
{
    return qBound(0, m_settings->value(QLatin1String(Keys::kMetadataLevel), Keys::kMetadataLevelDefault).toInt(), 2);
}

void Media::setMetadataLevel(int level)
{
    level = qBound(0, level, 2);
    if (level == metadataLevel())
        return;
    m_settings->setValue(QLatin1String(Keys::kMetadataLevel), level);
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

// prepared files the core has long read: away, before they pile up
static void sweepOutgoing()
{
    const QDateTime before = QDateTime::currentDateTime().addMSecs(-Keys::kOutgoingKeepMs);
    for (const QFileInfo &f : QDir(outgoingDir()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (f.lastModified() < before)
            QFile::remove(f.filePath());
    }
}

void Media::sendFile(const QString &roomId, const QString &path, const QString &caption,
                     const QString &threadRoot)
{
    sweepOutgoing();
    const QString p = localPath(path);
    QVariantMap cmd{{"cmd", "send_file"}, {"room_id", roomId}, {"path", p}};
    if (!threadRoot.isEmpty())
        cmd.insert("thread_root", threadRoot);
    if (!caption.trimmed().isEmpty())
        cmd.insert("caption", caption.trimmed());

    // pictures go out the way Element expects them: upright, at most 2048 px
    // on the long side when the setting asks for it, with their size and a
    // thumbnail, so the other side shows a preview instead of a bare file
    const int level = metadataLevel();
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
                // re-encoded: no metadata at all
                image = image.scaled(Keys::kUploadMaxEdge, Keys::kUploadMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QString out = stem + ".jpg";
                if (image.save(out, "JPEG", Keys::kUploadJpegQuality))
                    cmd.insert("path", out);
            } else if (level >= 1) {
                // a JPEG or PNG as it is, its metadata out (Exif.h, Png.h);
                // one that cannot be read and every other format are
                // re-encoded, so nothing slips through
                const bool jpeg = format == "jpeg";
                const bool png = format == "png";
                QByteArray bytes;
                QFile in(p);
                if ((jpeg || png) && in.open(QIODevice::ReadOnly))
                    bytes = in.readAll();
                const bool asPng = png || (!jpeg && image.hasAlphaChannel());
                const QString out = stem + (asPng ? ".png" : ".jpg");
                QFile file(out);
                if (!bytes.isEmpty() && (jpeg ? Exif::strip(bytes, level) : Png::strip(bytes, level))
                        && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()) {
                    file.close();
                    cmd.insert("path", out);
                } else if (asPng ? image.save(out, "PNG") : image.save(out, "JPEG", Keys::kReencodeJpegQuality)) {
                    cmd.insert("path", out);
                } else {
                    // the original would go with what the setting keeps back
                    emit errorOccurred(QStringLiteral("send_file"), QStringLiteral("cannot write ") + out);
                    return;
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
    const QString mime = QMimeDatabase().mimeTypeForFile(cmd.value("path").toString()).name();
    cmd.insert("mime", mime);
    m_outgoing.append({ cmd, nullptr });
    if (mime.startsWith(QLatin1String("video/"))) {
        const QString stem = outgoingDir() + "/" + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (level >= 1) {
            // a copy is stripped, and the send waits for it (videoStripped)
            const int id = ++m_lastStrip;
            m_outgoing.last().strip = id;
            const QString suffix = QFileInfo(p).suffix();
            const QString out = stem + "-" + QString::number(id) + "." + (suffix.isEmpty() ? QStringLiteral("mp4") : suffix);
            m_strips.start(new VideoStrip(this, id, p, out, stem, level));
        } else {
            prepareVideo(m_outgoing.last(), stem);
        }
    }
    sendInOrder();
}

void Media::videoStripped(int id, bool ok, const QString &out, const QString &stem)
{
    for (int i = 0; i < m_outgoing.size(); ++i) {
        if (m_outgoing.at(i).strip != id)
            continue;
        if (ok) {
            m_outgoing[i].strip = 0;
            m_outgoing[i].cmd.insert("path", out);
            prepareVideo(m_outgoing[i], stem);
        } else {
            // never the original instead: it says what the setting keeps back
            // (a format Mp4.h does not know, a damaged file, a full disk)
            m_outgoing.removeAt(i);
            emit errorOccurred(QStringLiteral("send_file"),
                               QStringLiteral("the video's metadata could not be removed, it was not sent"));
        }
        break;
    }
    sendInOrder();
}

// videos go out the way other clients send them: with their size, their
// length and a frame as thumbnail, so the other side shows a preview
void Media::prepareVideo(Outgoing &item, const QString &stem)
{
    QVariantMap &cmd = item.cmd;
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
    item.frame = frame;
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
    while (!m_outgoing.isEmpty() && !m_outgoing.first().frame && !m_outgoing.first().strip)
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
