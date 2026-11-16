# SPDX-License-Identifier: GPL-3.0-or-later
# Restart Rustrx as the app (not the stand-in) and wait until it answers.
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/dbus/user_bus_socket
B="gdbus call --session --dest net.mowerk.harbour-rustrx --object-path /net/mowerk/harbour_rustrx --method net.mowerk.harbour_rustrx"
$B.quit >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -x harbour-rustrx >/dev/null || break; sleep 1; done
# closing the app starts the stand-in (background mode): stop it too
systemctl --user stop harbour-rustrx 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -x harbour-rustrx >/dev/null || break; systemctl --user stop harbour-rustrx 2>/dev/null; sleep 1; done
pgrep -x harbour-rustrx >/dev/null && echo "still running"
$B.open >/dev/null 2>&1
for i in $(seq 1 25); do r=$(sh ~/.cache/rustrx-test/dev.sh '{"state":true}' 2>&1); case "$r" in *'"stand_in":false'*'running'*|*'running'*'"stand_in":false'*) break;; esac; sleep 1; done
echo "$r"
