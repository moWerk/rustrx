# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_renderhtml
SOURCES += tst_renderhtml.cpp $$ROOT/src/TimelineModel.cpp $$ROOT/src/Twemoji.cpp
HEADERS += $$ROOT/src/TimelineModel.h $$ROOT/src/Twemoji.h $$PWD/../timeline.h
