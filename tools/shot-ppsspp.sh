#!/usr/bin/env bash
# shot-ppsspp.sh <seconds> <name> "<map URL and switches>" -- run in PPSSPP's
# software renderer (so emulated VRAM holds the frame), let the driver write
# System/shot-<name>.ppm after <seconds>, convert it to PNG next to it.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="$1"; NAME="$2"; shift 2
INI="$HOME/.config/ppsspp/PSP/SYSTEM/ppsspp.ini"
P="$HOME/.config/ppsspp/PSP/GAME/UnrealTournament/System"
sed -i '' 's/^SoftwareRendering = False/SoftwareRendering = True/' "$INI"
rm -f "$P/shot-$NAME.ppm"
"$HERE/tools/run-ppsspp.sh" $((SECS + 15)) "$* -SHOTAT=$SECS -SHOTNAME=$NAME" > /dev/null 2>&1
sed -i '' 's/^SoftwareRendering = True/SoftwareRendering = False/' "$INI"
grep PSPSHOT "$P/UnrealTournament.log"
[ -f "$P/shot-$NAME.ppm" ] && /usr/bin/python3 "$HERE/tools/ppm2png.py" "$P/shot-$NAME.ppm" "$P/shot-$NAME.png" && echo "$P/shot-$NAME.png"
