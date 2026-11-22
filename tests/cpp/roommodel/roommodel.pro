# SPDX-License-Identifier: GPL-3.0-or-later
include(../common.pri)
QT += qml
TARGET = tst_roommodel
SOURCES += tst_roommodel.cpp $$ROOT/src/RoomLists.cpp $$ROOT/src/RoomModel.cpp $$ROOT/src/Twemoji.cpp
HEADERS += $$ROOT/src/RoomLists.h $$ROOT/src/RoomModel.h $$ROOT/src/Twemoji.h
