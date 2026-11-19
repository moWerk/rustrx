# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_handover
QT += dbus
SOURCES += tst_handover.cpp $$ROOT/src/Handover.cpp
HEADERS += $$ROOT/src/Handover.h
