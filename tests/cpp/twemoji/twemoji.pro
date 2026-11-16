# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_twemoji
# Twemoji.cpp is included by the test itself, to reach inText and linkEnd
SOURCES += tst_twemoji.cpp
HEADERS += $$ROOT/src/Twemoji.h
