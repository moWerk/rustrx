# SPDX-License-Identifier: GPL-3.0-or-later
# The golden file's generator; make-golden.sh builds it with OLD set to a
# folder holding Twemoji.cpp and Twemoji.h of 84311d4^.
QT = core qml
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = gen
isEmpty(OLD): error("OLD must name the folder with the old Twemoji sources")
INCLUDEPATH += $$OLD
SOURCES += gen.cpp $$OLD/Twemoji.cpp
HEADERS += $$OLD/Twemoji.h
DEFINES += RUSTRX_ROOT=\\\"$$clean_path($$PWD/../../../..)\\\"
