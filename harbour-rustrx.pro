# Rustrx: a Matrix client for Sailfish OS. The Matrix side is librustrx
# (core/, Rust, matrix-rust-sdk), built separately and shipped in
# /usr/share/harbour-rustrx/lib; this project is the Silica UI around it.
TARGET = harbour-rustrx

isEmpty(VERSION): VERSION = 0.2.2
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

CONFIG += sailfishapp sailfishapp_i18n sailfishapp_i18n_idbased sailfishapp_i18n_unfinished

SOURCES += src/main.cpp \
    src/Rustrx.cpp \
    src/RoomModel.cpp \
    src/TimelineModel.cpp

HEADERS += src/Rustrx.h \
    src/RoomModel.h \
    src/TimelineModel.h \
    src/rustrx_core.h

# the prebuilt core: lib/librustrx.so at build time, next to the data at run time
LIBS += -L$$PWD/lib -lrustrx
QMAKE_LFLAGS += -Wl,-rpath,/usr/share/$${TARGET}/lib
corelib.files = lib/librustrx.so
corelib.path = /usr/share/$${TARGET}/lib
INSTALLS += corelib

DISTFILES += qml/harbour-rustrx.qml \
    qml/pages/*.qml \
    qml/components/*.qml \
    qml/cover/*.qml \
    rpm/harbour-rustrx.spec \
    harbour-rustrx.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# qsTrId() with //% engineering English: the id based build keeps the
# unfinished entries, so the default .qm carries that English.
TRANSLATIONS += translations/harbour-rustrx.ts
