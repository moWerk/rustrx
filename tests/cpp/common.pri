# SPDX-License-Identifier: GPL-3.0-or-later
# What every C++ test shares: the host's Qt 5 with QtTest, the app's
# sources by path (only the ones a test needs), and where the tree is, so
# a test finds qml/twemoji and its golden files from any build folder.
QT += core gui testlib
QT -= widgets
CONFIG += console c++11 testcase
CONFIG -= app_bundle

ROOT = $$clean_path($$PWD/../..)
INCLUDEPATH += $$ROOT/src $$PWD
DEFINES += RUSTRX_ROOT=\\\"$$ROOT\\\"
# midRef and friends are deprecated in 5.15, not in the phone's 5.6
DEFINES += QT_NO_DEPRECATED_WARNINGS
