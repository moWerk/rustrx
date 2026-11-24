# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
QT += dbus
TARGET = tst_notifygate
SOURCES += tst_notifygate.cpp
HEADERS += $$ROOT/src/NotifyGate.h
