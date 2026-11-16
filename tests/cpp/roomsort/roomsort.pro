# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
TARGET = tst_roomsort
SOURCES += tst_roomsort.cpp $$ROOT/src/RoomLists.cpp $$ROOT/src/RoomModel.cpp $$ROOT/src/Twemoji.cpp
HEADERS += $$ROOT/src/RoomLists.h $$ROOT/src/RoomModel.h $$ROOT/src/Twemoji.h
