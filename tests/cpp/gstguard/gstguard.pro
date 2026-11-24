# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
QT += network
CONFIG += link_pkgconfig
PKGCONFIG += gstreamer-1.0
TARGET = tst_gstguard
SOURCES += tst_gstguard.cpp
HEADERS += $$ROOT/src/GstGuard.h
