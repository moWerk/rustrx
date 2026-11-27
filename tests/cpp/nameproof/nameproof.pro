# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
QT += dbus
TARGET = tst_nameproof
SOURCES += tst_nameproof.cpp
HEADERS += $$ROOT/src/NameProof.h $$ROOT/src/NotifyGate.h
