# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_timelineapply
SOURCES += tst_timelineapply.cpp $$ROOT/src/TimelineModel.cpp $$ROOT/src/Twemoji.cpp
HEADERS += $$ROOT/src/TimelineModel.h $$ROOT/src/Twemoji.h
