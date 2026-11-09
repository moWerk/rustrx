#!/bin/bash
# Start Rustrx on the Jolla Phone 2026 from the shell with the session's
# environment and the core's log on, log to /tmp/rustrx.log on the phone.
# usage: tools/run-on-phone.sh [start|stop|log]
set -euo pipefail
S=/tmp/claude-1000/-home-mo-sailfish/afced4d4-4500-4c4e-93ee-d8693735723a/scratchpad
export SSH_ASKPASS=$S/askpass-jp26.sh SSH_ASKPASS_REQUIRE=force
ssh="ssh -F $HOME/sailfish/work/ssh/config jp26"
case ${1:-start} in
start)
  $ssh 'for p in $(pgrep -x harbour-rustrx); do kill $p; done; sleep 1
    systemctl --user show-environment | grep -E "^(WAYLAND_DISPLAY|XDG_RUNTIME_DIR|DBUS_SESSION_BUS_ADDRESS|QT_|EGL_|QML|HYBRIS|LD_)" > /tmp/rustrx.env
    set -a; . /tmp/rustrx.env; set +a
    export RUSTRX_LOG=${RUSTRX_LOG:-info,matrix_sdk=info,matrix_sdk_crypto=info,matrix_sdk_ui=info}
    cd ~; nohup /usr/bin/harbour-rustrx > /tmp/rustrx.log 2>&1 &
    sleep 3; pgrep -ax harbour-rustrx || { echo "not running"; tail -20 /tmp/rustrx.log; }'
  ;;
stop)
  $ssh 'for p in $(pgrep -x harbour-rustrx); do kill $p; done; echo stopped'
  ;;
log)
  # the session's own log, access tokens and keys masked
  $ssh 'grep -vE "not accessible|libselinux|mali_kbase|large_page_conf" /tmp/rustrx.log' \
    | sed -E 's/(access_token|refresh_token|syt_|mat_)[A-Za-z0-9_=-]+/\1<masked>/g' | tail -${2:-60}
  ;;
esac
