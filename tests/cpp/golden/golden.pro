# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_golden
SOURCES += tst_golden.cpp $$ROOT/src/TimelineModel.cpp $$ROOT/src/RoomModel.cpp $$ROOT/src/Twemoji.cpp
HEADERS += $$ROOT/src/TimelineModel.h $$ROOT/src/RoomModel.h $$ROOT/src/Twemoji.h $$PWD/../timeline.h
