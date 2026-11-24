# The sailfish-secrets part, a plugin of its own: the app loads it at run
# time (QLibrary), so Rustrx still installs and runs where sailfish-secrets
# is missing (community ports); the package does not declare the library.
TEMPLATE = lib
TARGET = rustrx-secrets
CONFIG += plugin link_pkgconfig
QT = core
PKGCONFIG += sailfishsecrets

SOURCES += ../src/SecretStore.cpp
HEADERS += ../src/SecretStore.h ../src/StoreKeyGuard.h

target.path = /usr/share/harbour-rustrx/lib
INSTALLS += target
