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
#ifndef MEDIA_H
#define MEDIA_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVariantMap>

class QSettings;

// Files in and out: pictures and recordings to send (prepared in the
// outgoing cache folder), avatars, media to fetch or save to Downloads,
// and the custom emoji's cached pictures. Its commands go to the core
// through Rustrx (the command signal), which shows its API as its own.
class Media : public QObject
{
    Q_OBJECT

public:
    // settings: Rustrx's, which outlives every use here
    Media(QSettings *settings, QObject *parent = nullptr);

    // a file:// URL as a local path; a path stays as it is
    static QString localPath(const QString &path);

    bool downscaleImages() const;
    void setDownscaleImages(bool on);
    bool stripLocation() const;
    void setStripLocation(bool on);

    // a custom emoji's cached picture (file://), or "" while it is fetched
    // (emoteReady follows)
    QString emotePath(const QString &mxc);
    // the core's `emote` event: the picture is in the media folder
    void emoteArrived(const QString &mxc, const QString &path);
    // the media folder was cleared, or the account logged out
    void clearEmotes();

    void sendFile(const QString &roomId, const QString &path, const QString &caption,
                  const QString &threadRoot);
    void sendVoice(const QString &roomId, const QString &path, qint64 durationMs,
                   const QString &caption);
    void setRoomAvatar(const QString &roomId, const QString &path);
    void setOwnAvatar(const QString &path);
    void getMedia(const QString &roomId, const QString &eventId, bool thumb);
    void exportMedia(const QString &roomId, const QString &eventId);

signals:
    void command(const QVariantMap &cmd);
    void errorOccurred(const QString &cmd, const QString &message);
    void emoteReady(const QString &mxc, const QString &path);
    void settingsChanged();

private:
    // an avatar picture upright, at most 512 px, in the cache; its path and size
    QString prepareAvatar(const QString &path, const QString &cmd, QSize *size);

    QSettings *m_settings;
    QHash<QString, QString> m_emotePaths;
    QSet<QString> m_emotesAsked;
};

#endif
