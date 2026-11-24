# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_privatesettings
SOURCES += tst_privatesettings.cpp $$ROOT/src/PrivateSettings.cpp
HEADERS += $$ROOT/src/PrivateSettings.h
