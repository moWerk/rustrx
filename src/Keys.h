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
#ifndef RUSTRX_KEYS_H
#define RUSTRX_KEYS_H

#include <QtGlobal>

// The names, limits and timeouts the C++ side uses, in one place. Plain
// char arrays: wrap them in QLatin1String or QString::fromLatin1 where a
// QString is wanted. The QML side has its own (qml/singletons/Tunables.qml),
// the core its own (core/).
namespace Keys {

// the JSON protocol this UI speaks, see docs/PROTOCOL.md (core: PROTO in core/src/consts.rs)
static const int kProto = 2;

// QSettings (Rustrx::settingsFile(): ~/.config/net.mowerk/harbour-rustrx/harbour-rustrx.conf), each key with its default
static const char kDebugLog[] = "debugLog";
static const bool kDebugLogDefault = true;
static const char kDownscaleImages[] = "downscaleImages";
static const bool kDownscaleImagesDefault = true;
// the place a photo was taken, out of it before it is sent (Exif.h)
// stored under its 0.4 name, when it removed only the place: a choice made then stays
static const char kStripMetadata[] = "stripLocation";
static const bool kStripMetadataDefault = true;
// a JPEG re-encoded because its EXIF could not be read
static const int kReencodeJpegQuality = 95;
// the invitations a notification was shown for
static const char kInvitesNotified[] = "invitesNotified";
// groups from before 0.4.7, removed at login: drafts/<room id> (moved into
// the store), threadsSeen/<root id without $> (the receipts tell now)
static const char kDrafts[] = "drafts";
static const char kThreadsSeen[] = "threadsSeen";
// per tab: tabs/<kind>/<key>, else the key itself as the shared value
static const char kTabs[] = "tabs";
static const char kSortOrder[] = "sortOrder";
static const char kSortOrderDefault[] = "activity"; // or "unread", "name"
static const char kFavsOnTop[] = "favsOnTop";
static const bool kFavsOnTopDefault = false;
static const char kHideMuted[] = "hideMuted";
static const bool kHideMutedDefault = false;
static const char kLowLast[] = "lowLast";
static const bool kLowLastDefault = true;

// our D-Bus names. The bus name has a hyphen, the path and the interface
// cannot. Also in dbus/net.mowerk.harbour_rustrx.service and the .desktop
// file; the interface again in Rustrx.h's Q_CLASSINFO (a literal only).
static const char kBusName[] = "net.mowerk.harbour-rustrx";
static const char kObjectPath[] = "/net/mowerk/harbour_rustrx";
static const char kInterface[] = "net.mowerk.harbour_rustrx";
// the share sheet calls /share/<method>, the method named in the desktop
// file's X-Share-Methods; /share stays for direct callers
static const char kSharePath[] = "/share";
static const char kShareRoomPath[] = "/share/room";

// UnifiedPush (the connector's own name is UnifiedPush::kConnectorName)
static const char kConnectorPath[] = "/org/unifiedpush/Connector";
static const char kDistributorPrefix[] = "org.unifiedpush.Distributor.";
static const char kDistributorPath[] = "/org/unifiedpush/Distributor";
static const char kDistributorInterface[] = "org.unifiedpush.Distributor2";
// where the distributors' activation files are
static const char kDbusServicesDir[] = "/usr/share/dbus-1/services";
// a push server's Matrix gateway, and the project's common one
static const char kPushNotifyPath[] = "/_matrix/push/v1/notify";
static const char kCommonGateway[] = "https://matrix.gateway.unifiedpush.org/_matrix/push/v1/notify";

// system services
static const char kDbusService[] = "org.freedesktop.DBus";
static const char kDbusPath[] = "/org/freedesktop/DBus";
static const char kSystemdService[] = "org.freedesktop.systemd1";
static const char kSystemdPath[] = "/org/freedesktop/systemd1";
static const char kSystemdManager[] = "org.freedesktop.systemd1.Manager";
// our user units (systemd/)
static const char kStandInUnit[] = "harbour-rustrx.service";
static const char kOpenUnit[] = "harbour-rustrx-open.service";
// set by the stand-in unit and by the connector's activation file
static const char kEnvBackground[] = "RUSTRX_BACKGROUND";
static const char kEnvPushWake[] = "RUSTRX_PUSH_WAKE";

// files and directories
// in the config folder: the stand-in's handover, and the background mode
// flag (systemd/harbour-rustrx.service checks it with ConditionPathExists)
static const char kHandoverFile[] = "handover.json";
static const char kBackgroundFlag[] = "background";
// in the data folder: push state, the dev command flag, the debug logs
static const char kPushStateFile[] = "push.json";
static const char kDevCommandsFlag[] = "dev-commands";
static const char kLogDir[] = "logs";
// in the cache folder: pictures prepared for sending, recordings, shares
static const char kOutgoingDir[] = "outgoing";
static const char kVoiceDir[] = "voice";
static const char kSharedDir[] = "shared";
// in Downloads: saved media
static const char kDownloadDir[] = "Rustrx";
// the sailfish-secrets plugin, loaded only where it can be
static const char kSecretsPlugin[] = "lib/librustrx-secrets.so";
static const char kSecretsSymbol[] = "rustrx_secrets_store_key";
// the debug log's names (DebugLog::install, DebugLog::collect)
static const char kLogApp[] = "app";
static const char kLogBackground[] = "background";
static const char kLogPush[] = "push";

// timeouts and delays, in milliseconds
// the earlier process's bus name must go within this, looked at this often
static const int kNameGoneTimeoutMs = 10000;
static const int kNameGonePollMs = 100;
// the running process answers activate within this
static const int kActivateTimeoutMs = 3000;
// a running Rustrx still starting has the bus name but not yet its object:
// asked again for this long before it counts as leaving
static const int kActivateRetryMs = 10000;
// at exit, the core gets this long to stop before the bus name goes
static const int kShutdownMs = 3000;
// glibc's malloc arenas: by default up to 8 per core, one per new thread
// (tokio's blocking threads come and go for every store access); a
// measured 55 arenas held 242 MB for 83 MB in use. Two keep it close.
static const int kMallocArenas = 2;
// a push-woken process quits this long after the last push, at the latest
// after kPushMaxLifeMs (the same numbers as the ring lifetime in call.rs,
// which is unrelated)
static const int kPushIdleMs = 30000;
static const int kPushMaxLifeMs = 120000;
static const int kStartUnitTimeoutMs = 5000;
// time for the close to reach the notification server before quitting
static const int kQuitDelayMs = 300;
static const int kListNamesTimeoutMs = 2000;
static const int kRegisterTimeoutMs = 10000;

// pictures and media
// avatars: the longest side, JPEG quality (a transparent one goes as PNG)
static const int kAvatarMaxEdge = 512;
static const int kAvatarJpegQuality = 90;
// pictures sent with "downscale images" on: the longest side, JPEG quality
static const int kUploadMaxEdge = 2048;
static const int kUploadJpegQuality = 88;
// the thumbnail sent with a picture
static const int kThumbEdge = 800;
static const int kThumbJpegQuality = 80;
// a video's first frame for its thumbnail: Sailfish's own helper (the
// Videos permission lets the sandbox run it), given this long before the
// video goes without
static const char kVideoFrameTool[] = "/usr/bin/thumbnaild-video";
static const int kVideoFrameMs = 5000;
// a custom emoji's size until QML binds emoteSize (core: height="32" in sent HTML)
static const int kDefaultEmoteSize = 32;
// the longest reaction key shown before it is cut with an ellipsis
static const int kReactionKeyMax = 24;
// shared files are copied in chunks of this
static const int kShareChunk = 1 << 20;
// a shared file is copied up to this size (a pipe or /dev/zero ends there),
// and copies older than this are removed at start
static const qint64 kShareMaxBytes = qint64(512) << 20;
static const int kSharedKeepHours = 24;
// Opus records at this rate
static const int kVoiceSampleRate = 48000;
// the debug log: about this big, then it becomes the older file; the size
// is looked at every kLogCheckEveryLines lines
static const qint64 kLogMaxSize = 1024 * 1024;
static const int kLogCheckEveryLines = 50;

// calls (Voip): an invite rings this long (Element's value); candidates go
// out together after this pause; TURN credentials are waited for this
// long; the far end's level and the packet counts are noted this often
static const int kVoipInviteLifetimeMs = 60000;
static const int kVoipCandidateBatchMs = 300;
static const int kVoipTurnWaitMs = 3000;
static const int kVoipStatsMs = 2000;
static const int kVoipLevelMs = 500;
// the receiving jitter buffer (ms) and the Opus bitrate (bit/s)
static const int kVoipJitterMs = 100;
static const int kVoipOpusBitrate = 32000;
// echo cancelling (Speex): its rate, frame and how long an echo it learns
static const int kVoipAecRate = 16000;
static const int kVoipAecFrameMs = 10;
static const int kVoipAecTailMs = 300;
// the far end's audio waits at most this long for the microphone's frames
static const int kVoipAecQueueMs = 120;

}

#endif
