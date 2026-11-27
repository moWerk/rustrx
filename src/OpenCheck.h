// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_OPENCHECK_H
#define RUSTRX_OPENCHECK_H

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QString>
#include <QStringList>

// Whether a received file, saved to Downloads, may be handed to the
// phone's app for its type (audit UC5). The name the sender chose and the
// file's bytes must both say one of the kinds below: pictures, audio,
// video, PDF, plain text, OpenDocument and MS Office documents. The bytes
// may say less than the name (an .docx is a zip by its bytes, an old .doc
// an OLE file), but never something else: an installer named .jpg is an
// installer by its bytes, and stays in Downloads. Anything else is only
// saved. A denylist by the name's suffix was bypassed by trailing dots,
// invisible characters, or no suffix at all.
namespace OpenCheck {

// a kind the phone's apps may open, by the type its name gives
inline bool allowedKind(const QMimeType &t)
{
    const QString n = t.name();
    static const QStringList exact = {
        QStringLiteral("image/jpeg"), QStringLiteral("image/png"), QStringLiteral("image/gif"),
        QStringLiteral("image/webp"), QStringLiteral("application/pdf"), QStringLiteral("text/plain"),
        QStringLiteral("application/msword"), QStringLiteral("application/vnd.ms-excel"),
        QStringLiteral("application/vnd.ms-powerpoint"),
    };
    if (exact.contains(n))
        return true;
    // a playlist lists addresses, and a player would fetch them (audit
    // UC4): text or XML, or one of the playlist types that say neither
    static const QStringList playlists = {
        QStringLiteral("audio/x-mpegurl"), QStringLiteral("audio/x-scpls"), QStringLiteral("audio/x-ms-asx"),
        QStringLiteral("audio/x-ms-wax"), QStringLiteral("video/x-ms-wvx"), QStringLiteral("video/vnd.mpegurl"),
        QStringLiteral("audio/x-pn-realaudio"), QStringLiteral("audio/x-iriver-pla"),
    };
    if (playlists.contains(n) || t.inherits(QStringLiteral("text/plain"))
            || t.inherits(QStringLiteral("application/xml")))
        return false;
    return n.startsWith(QLatin1String("audio/")) || n.startsWith(QLatin1String("video/"))
            || n.startsWith(QLatin1String("application/vnd.oasis.opendocument."))
            || n.startsWith(QLatin1String("application/vnd.openxmlformats-officedocument."));
}

inline bool mayOpen(const QString &path)
{
    static const QMimeDatabase db;
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        return false;
    const QMimeType byName = db.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    const QMimeType byContent = db.mimeTypeForFile(info, QMimeDatabase::MatchContent);
    if (!byName.isValid() || !byContent.isValid() || !allowedKind(byName))
        return false;
    // what the bytes say is the name's type or one it belongs to (but not
    // the bare "some bytes" or "some text" every file is); for sound and
    // video any allowed sound or video (an .ogg's bytes say Opus in Ogg,
    // an .m4a's often MP4 video)
    if (byContent == byName)
        return true;
    if (byContent.name() == QLatin1String("application/octet-stream")
            || byContent.name() == QLatin1String("text/plain"))
        return false;
    if (byName.inherits(byContent.name()))
        return true;
    auto media = [](const QMimeType &t) {
        return t.name().startsWith(QLatin1String("audio/")) || t.name().startsWith(QLatin1String("video/"));
    };
    return media(byName) && media(byContent) && allowedKind(byContent);
}

} // namespace OpenCheck

#endif
