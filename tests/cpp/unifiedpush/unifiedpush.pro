# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
QT += dbus network
TARGET = tst_unifiedpush
SOURCES += tst_unifiedpush.cpp $$ROOT/src/UnifiedPush.cpp
HEADERS += $$ROOT/src/UnifiedPush.h $$ROOT/src/NameProof.h
