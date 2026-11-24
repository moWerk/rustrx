# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_debuglog
SOURCES += tst_debuglog.cpp $$ROOT/src/DebugLog.cpp
HEADERS += $$ROOT/src/DebugLog.h
