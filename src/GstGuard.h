// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTRX_GSTGUARD_H
#define RUSTRX_GSTGUARD_H

#include <QByteArray>
#include <QtGlobal>

#include <gst/gst.h>

// Received audio and video must not make GStreamer fetch from the network:
// a "voice message" that is an HLS or DASH playlist (typefound by content,
// whatever its name) had playbin's adaptive demuxers load every address in
// it (audit 0.8.4 UC4). Their rank goes to none in this process, so
// autoplugging never picks them and such a file fails as any unplayable
// one does. Nothing Rustrx plays needs them; webrtcbin neither.
namespace GstGuard {

static const char *const kPlaylistDemuxers[] = {
    "hlsdemux", "hlsdemux2", "dashdemux", "dashdemux2", "mssdemux", "mssdemux2"
};

// GST_PLUGIN_FEATURE_RANK, read by gst_init (GStreamer 1.18 and later):
// set in main() before anything starts GStreamer, so it holds for
// QtMultimedia's own lazy start too, without starting it here (perf: gst)
inline void setEnvironment()
{
    QByteArray ranks;
    for (const char *name : kPlaylistDemuxers)
        ranks += (ranks.isEmpty() ? "" : ",") + QByteArray(name) + ":NONE";
    const QByteArray before = qgetenv("GST_PLUGIN_FEATURE_RANK");
    qputenv("GST_PLUGIN_FEATURE_RANK", before.isEmpty() ? ranks : before + ',' + ranks);
}

// the same through the registry, once GStreamer is up (for a GStreamer
// that ignores the variable); how many there were
inline int demote()
{
    int demoted = 0;
    for (const char *name : kPlaylistDemuxers) {
        if (GstPluginFeature *f = gst_registry_lookup_feature(gst_registry_get(), name)) {
            gst_plugin_feature_set_rank(f, GST_RANK_NONE);
            gst_object_unref(f);
            ++demoted;
        }
    }
    return demoted;
}

} // namespace GstGuard

#endif
