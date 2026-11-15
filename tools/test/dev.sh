# usage: sh dev.sh '<json>'  (on the phone, as defaultuser)
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/dbus/user_bus_socket
gdbus call --session --dest net.mowerk.harbour-rustrx --object-path /net/mowerk/harbour_rustrx --method net.mowerk.harbour_rustrx.dev "'$1'"
