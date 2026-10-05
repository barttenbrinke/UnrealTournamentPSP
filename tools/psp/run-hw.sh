#!/bin/zsh
# run-hw.sh <label> <seconds> [game args...] -- run psplink_host/UnrealTournament.prx on
# the PSP over PSPLink (PSPLink running on the console, USB connected), save the
# transcript as $PSP_WORK/hw-<label>.log and summarise it. Game data is served
# from $PSP_HOST/UnrealTournament over USB (./install-psp.sh $PSP_HOST) unless
# the args say otherwise: the map URL must come first.
#   tools/psp/run-hw.sh ctf 150 'CTF-Face?Game=Botpack.CTFGame'
S="${PSP_WORK:-$HOME/.ut-psp}"; mkdir -p "$S"
H="${PSP_HOST:-$S/psplink_host}"
L=$1; T=$2; shift 2
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
cp "$HERE/build-psplink/UnrealTournament/UnrealTournament.prx" "$H/" 2>/dev/null
pkill usbhostfs_pc; pkill -f pspsh_drive.py; pkill pspsh; sleep 1; rm -f $S/pspsh.in $S/pspsh.log
(cd $S && usbhostfs_pc $H > $S/usbhostfs.log 2>&1 &)
for i in $(seq 1 600); do grep -q "Connected to device" $S/usbhostfs.log && break; sleep 1; done
grep -q "Connected to device" $S/usbhostfs.log || { echo "$L: NO USB"; pkill usbhostfs_pc; exit 1; }
(cd $S && /usr/bin/python3 "$HERE/tools/psp/pspsh_drive.py" "$S" > $S/pspsh_drive.err 2>&1 &)
sleep 20; echo "ls" > $S/pspsh.in; sleep 4
ROOT="-ROOT=host0:/UnrealTournament/System/"
[[ "$*" == *-ROOT=* ]] && ROOT=""
echo "./UnrealTournament.prx $* $ROOT" > $S/pspsh.in; sleep $T
# 'reset', never 'exit': after an exit the USB link is gone until the cable is reseated
echo "reset" > $S/pspsh.in; sleep 20
pkill pspsh; pkill -f pspsh_drive.py; pkill usbhostfs_pc; sleep 2
cp $S/pspsh.log $S/hw-$L.log
echo "== $L ($*): $(grep -a -c 'frames in' $S/hw-$L.log) intervals, exceptions: $(grep -a -c -i 'exception' $S/hw-$L.log)"
grep -a -E "Critical|PSPDEATH|Exception|Startup time" $S/hw-$L.log | head -8
grep -a -E "PSPPERF: 100 frames" $S/hw-$L.log | sed 's/.*PSPPERF: //' | cut -c1-110 | tail -12
