#!/usr/bin/env bash
# shots-ppsspp.sh <seconds> "<switches>" -- like shot-ppsspp.sh, but for any
# number of "-EXECAT=...;secs:shot_<label>" shots in one run (software
# renderer, so emulated VRAM holds the frame). Prints the PNG paths.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="$1"; shift
INI="$HOME/.config/ppsspp/PSP/SYSTEM/ppsspp.ini"
P="$HOME/.config/ppsspp/PSP/GAME/UnrealTournament/System"
sed -i '' -E 's/^(SoftwareRendering|SoftwareRenderer) = False/\1 = True/' "$INI"
rm -f "$P"/shot-*.ppm "$P"/shot-*.png
"$HERE/tools/run-ppsspp.sh" "$SECS" "$*" > /dev/null 2>&1
sed -i '' -E 's/^(SoftwareRendering|SoftwareRenderer) = True/\1 = False/' "$INI"
for f in "$P"/shot-*.ppm; do
  [ -f "$f" ] && /usr/bin/python3 "$HERE/tools/ppm2png.py" "$f" "${f%.ppm}.png" && echo "${f%.ppm}.png"
done
