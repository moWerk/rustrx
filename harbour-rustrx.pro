# Rustrx: a Matrix client for Sailfish OS. The Matrix side is librustrx
# (core/, Rust, matrix-rust-sdk), built separately and shipped in
# /usr/share/harbour-rustrx/lib; this project is the Silica UI around it.
TARGET = harbour-rustrx

isEmpty(VERSION): VERSION = 0.4.3
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

QT += dbus multimedia network
# calls: GStreamer's webrtcbin (Voip.cpp)
CONFIG += link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0
# a call's audio as a call (the audio policy, CallAudio.cpp)
PKGCONFIG += libresourceqt5

CONFIG += sailfishapp sailfishapp_i18n sailfishapp_i18n_idbased sailfishapp_i18n_unfinished

SOURCES += src/main.cpp \
    src/Rustrx.cpp \
    src/Handover.cpp \
    src/Media.cpp \
    src/ShareAdaptor.cpp \
    src/RoomLists.cpp \
    src/RoomModel.cpp \
    src/TimelineModel.cpp \
    src/Twemoji.cpp \
    src/UnifiedPush.cpp \
    src/VoiceRecorder.cpp \
    src/DebugLog.cpp \
    src/FrameStats.cpp \
    src/KeyboardSlide.cpp \
    src/Voip.cpp \
    src/CallAudio.cpp

HEADERS += src/Rustrx.h \
    src/Handover.h \
    src/Media.h \
    src/Keys.h \
    src/ShareAdaptor.h \
    src/RoomLists.h \
    src/RoomModel.h \
    src/TimelineModel.h \
    src/Twemoji.h \
    src/UnifiedPush.h \
    src/VoiceRecorder.h \
    src/DebugLog.h \
    src/FrameStats.h \
    src/KeyboardSlide.h \
    src/Voip.h \
    src/CallAudio.h \
    src/LocalOnly.h \
    src/Exif.h \
    src/Mp4.h \
    src/rustrx_core.h

# the prebuilt core: lib/librustrx.so at build time, next to the data at run time
LIBS += -L$$PWD/lib -lrustrx
QMAKE_LFLAGS += -Wl,-rpath,/usr/share/$${TARGET}/lib
corelib.files = lib/librustrx.so
corelib.path = /usr/share/$${TARGET}/lib
INSTALLS += corelib

# notifications open their room through this service, also when the app is closed
# and a push (UnifiedPush) wakes it through its connector name, without a window
dbusservice.files = dbus/net.mowerk.harbour_rustrx.service dbus/org.unifiedpush.Connector.rustrx.service
dbusservice.path = /usr/share/dbus-1/services
INSTALLS += dbusservice

# notification categories: a message vibrates, a call rings
categories.files = notificationcategories/x-harbour-rustrx.message.conf notificationcategories/x-harbour-rustrx.call.conf
categories.path = /usr/share/lipstick/notificationcategories
INSTALLS += categories

# background mode: started with the session when the user switched it on
autostart.files = systemd/harbour-rustrx.service systemd/harbour-rustrx-open.service
autostart.path = /usr/lib/systemd/user
INSTALLS += autostart

# secrets/secrets.pro (built by the spec): the sailfish-secrets plugin
DISTFILES += qml/harbour-rustrx.qml \
    qml/Service.qml \
    qml/call-bridge.js \
    qml/pages/*.qml \
    qml/pages/settings/*.qml \
    qml/components/*.qml \
    qml/cover/*.qml \
    qml/singletons/*.qml \
    rpm/harbour-rustrx.spec \
    harbour-rustrx.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# qsTrId() with //% engineering English: the id based build keeps the
# unfinished entries, so the default .qm carries that English.
TRANSLATIONS += translations/harbour-rustrx.ts
