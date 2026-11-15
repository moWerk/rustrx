# usage: sh calltest.sh ROOM_ID SECONDS   (as defaultuser, on the phone)
L=~/.local/share/net.mowerk/harbour-rustrx/logs/app.log
D=~/.cache/rustrx-test/dev.sh
mark="calltest-$(date +%s)"
sh $D "{\"ui\":\"open_room\",\"room_id\":\"$1\"}"; sleep 3
sh $D "{\"cmd\":\"call_start\",\"room_id\":\"$1\",\"skip_lobby\":true}"
sleep ${2:-40}
n=$(grep -a -n "dev call_start" $L | tail -1 | cut -d: -f1)
[ -n "$n" ] && tail -n +$n $L | grep -a -E "call page|call widget|call_|widget|WARN|ERROR" | grep -a -v -E "Mismatch in the device|command call_message|cmd=call_message" | sed -E "s/#\?[^ )]*/#?…/g" | cut -c12-330
