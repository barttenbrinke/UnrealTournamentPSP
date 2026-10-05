#!/usr/bin/env bash
#
# install-psp.sh -- put Unreal Tournament on a PSP Memory Stick (or PPSSPP).
#
# Usage: ./install-psp.sh <PSP/GAME directory> [EBOOT.PBP]
#   ./install-psp.sh ~/.config/ppsspp/PSP/GAME      # PPSSPP
#   ./install-psp.sh /Volumes/PSP/PSP/GAME          # Memory Stick
#
# Game data comes from GAME_ASSETS/ (System, Maps, Textures, Sounds, Music of
# the retail v400 CD). If it is missing and GAME_ASSETS_SRC/UT99.iso exists,
# it is extracted from there first.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAMEDIR="${1:?usage: $0 <PSP/GAME directory> [EBOOT.PBP]}"
EBOOT="${2:-$HERE/build-psp/UnrealTournament/EBOOT.PBP}"
DEST="$GAMEDIR/UnrealTournament"
ASSETS="$HERE/GAME_ASSETS"
ISO="$HERE/GAME_ASSETS_SRC/UT99.iso"

if [ ! -d "$ASSETS/System" ]; then
  [ -f "$ISO" ] || { echo "no $ASSETS/System and no $ISO" >&2; exit 1; }
  echo "==> extracting game data from $(basename "$ISO")"
  mkdir -p "$ASSETS"
  for d in System Maps Textures Sounds Music; do
    bsdtar -xf "$ISO" -C "$ASSETS" "$d" 2>/dev/null || bsdtar -xf "$ISO" -C "$ASSETS" "$(echo $d | tr a-z A-Z)" 2>/dev/null || true
  done
fi

echo "==> copying to $DEST"
mkdir -p "$DEST"
for d in System Maps Textures Sounds Music; do
  [ -d "$ASSETS/$d" ] && rsync -rt --exclude '*.dll' --exclude '*.exe' --exclude '*.ini' "$ASSETS/$d" "$DEST/"
done
mkdir -p "$DEST/Save"
cp "$HERE/Config/PSP/UnrealTournament.ini" "$HERE/Config/PSP/User.ini" "$DEST/System/"
cp "$EBOOT" "$DEST/EBOOT.PBP"
rm -f "$DEST/System/UnrealTournament.log"
# PPSSPP has no Media Engine: the ME start-up never returns there, so an
# emulator install keeps the music on the CPU.
case "$GAMEDIR" in
  *ppsspp*) sed -i.bak 's/^MusicME=1/MusicME=0/' "$DEST/System/UnrealTournament.ini" && rm -f "$DEST/System/UnrealTournament.ini.bak" ;;
esac

# macOS writes "._" AppleDouble sidecars on FAT volumes; the PSP lists them as
# "Corrupted Data". Clean them up when we wrote to a real card.
DEV=$(df -P "$DEST" | awk 'NR==2 {print $1}')
if command -v dot_clean >/dev/null && mount | grep -qE "^$DEV on .* \((msdos|exfat)"; then
  VOL=$(df -P "$DEST" | awk 'NR==2 {print $6}')
  if [ "$VOL" != "/" ]; then
    dot_clean -m "$VOL" 2>/dev/null || true
    find "$VOL" -name '.DS_Store' -delete 2>/dev/null || true
    sync
  fi
fi
echo "==> done: $(du -sh "$DEST" | cut -f1) in $DEST"
