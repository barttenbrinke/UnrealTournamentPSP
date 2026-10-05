#!/usr/bin/env bash
#
# run-ppsspp.sh -- deploy the current build to PPSSPP, run it for N seconds,
# then print the tail of UnrealTournament.log and any crash stack PPSSPP
# reported. Extra arguments are passed to the game as its command line.
#
# Usage: tools/run-ppsspp.sh [seconds] [game args...]
#
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="${1:-40}"; shift || true
P="$HOME/.config/ppsspp/PSP/GAME/UnrealTournament"
PPSSPP=/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
OUT="${TMPDIR:-/tmp}/ut-ppsspp.out"

pkill -f PPSSPPSDL 2>/dev/null; sleep 1
cp "$HERE/build-psp/UnrealTournament/EBOOT.PBP" "$P/EBOOT.PBP"
rm -f "$P/System/UnrealTournament.log"
# PPSSPP passes no argv beyond the EBOOT path; game switches go in a file the
# launcher reads (System/cmdline.txt), if any were given.
if [ $# -gt 0 ]; then echo "$*" > "$P/System/cmdline.txt"; else rm -f "$P/System/cmdline.txt"; fi
("$PPSSPP" "$P/EBOOT.PBP" > "$OUT" 2>&1 &)
sleep "$SECS"
if [ -n "${SHOT:-}" ]; then
  # Capture the emulator window itself, wherever it is on screen.
  WID=$("$HERE/tools/winid" 2>/dev/null)
  if [ -n "$WID" ]; then screencapture -x -o -l "$WID" "$SHOT"; else screencapture -x "$SHOT"; fi
fi
pkill -f PPSSPPSDL 2>/dev/null; sleep 1
echo "=== log (last 40 lines)"
tail -40 "$P/System/UnrealTournament.log" 2>/dev/null
echo "=== emulator faults"
grep -E "Invalid access|Crash|BREAK|Unknown syscall|Exception" "$OUT" | grep -v "^$" | head -5
grep -A25 "Invalid access" "$OUT" | grep -E "^\S+ \(" | head -25
