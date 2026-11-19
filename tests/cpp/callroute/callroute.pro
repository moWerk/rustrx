# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_callroute
CONFIG += link_pkgconfig
QT += network
# a missing libpulse must fail the build loudly, never look like a pass
!packagesExist(libpulse): error("tst_callroute needs the libpulse development files")
PKGCONFIG += libpulse
SOURCES += tst_callroute.cpp $$ROOT/src/CallRoute.cpp
HEADERS += $$ROOT/src/CallRoute.h $$ROOT/src/CallRoutePlan.h
