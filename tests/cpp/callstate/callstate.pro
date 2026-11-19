# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_callstate
QT += dbus
SOURCES += tst_callstate.cpp $$ROOT/src/CallState.cpp
HEADERS += $$ROOT/src/CallState.h
