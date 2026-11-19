# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_voip
CONFIG += link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0
SOURCES += tst_voip.cpp $$ROOT/src/Voip.cpp
HEADERS += $$ROOT/src/Voip.h
