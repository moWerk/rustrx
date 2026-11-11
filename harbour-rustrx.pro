# Rustrx: a Matrix client for Sailfish OS. The Matrix side is librustrx
# (core/, Rust, matrix-rust-sdk), built separately and shipped in
# /usr/share/harbour-rustrx/lib; this project is the Silica UI around it.
TARGET = harbour-rustrx

isEmpty(VERSION): VERSION = 0.3.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

QT += dbus multimedia

CONFIG += sailfishapp sailfishapp_i18n sailfishapp_i18n_idbased sailfishapp_i18n_unfinished

SOURCES += src/main.cpp \
    src/Rustrx.cpp \
    src/RoomModel.cpp \
    src/TimelineModel.cpp \
    src/VoiceRecorder.cpp

HEADERS += src/Rustrx.h \
    src/RoomModel.h \
    src/TimelineModel.h \
    src/VoiceRecorder.h \
    src/rustrx_core.h

# the prebuilt core: lib/librustrx.so at build time, next to the data at run time
LIBS += -L$$PWD/lib -lrustrx
QMAKE_LFLAGS += -Wl,-rpath,/usr/share/$${TARGET}/lib
corelib.files = lib/librustrx.so
corelib.path = /usr/share/$${TARGET}/lib
INSTALLS += corelib

# notifications open their room through this service, also when the app is closed
dbusservice.files = dbus/net.mowerk.harbour_rustrx.service
dbusservice.path = /usr/share/dbus-1/services
INSTALLS += dbusservice

# background mode: started with the session when the user switched it on
autostart.files = systemd/harbour-rustrx.service
autostart.path = /usr/lib/systemd/user
INSTALLS += autostart

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
