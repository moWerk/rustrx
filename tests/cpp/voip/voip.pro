# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_voip
CONFIG += link_pkgconfig
# a missing GStreamer must fail the build loudly, never look like a pass
!packagesExist(gstreamer-webrtc-1.0 gstreamer-app-1.0 speexdsp): error("tst_voip needs gstreamer-webrtc-1.0, gstreamer-app-1.0 and speexdsp development files")
PKGCONFIG += gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-app-1.0 speexdsp
SOURCES += tst_voip.cpp $$ROOT/src/Voip.cpp
HEADERS += $$ROOT/src/Voip.h
