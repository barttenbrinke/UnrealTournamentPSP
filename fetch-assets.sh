#!/usr/bin/env bash
#
# fetch-assets.sh -- download the Unreal Tournament v400 CD image from
# archive.org and unpack the game data into GAME_ASSETS/.
#
# The port needs the retail v400 data: later System/*.u packages (GOTY, the
# 436 patch, OldUnreal 469) do not load on a v400 engine. The image is
# https://archive.org/details/ut-99_202512 (UT99.iso, 759 MB).
#
# Usage: ./fetch-assets.sh
# Then:  ./install-psp.sh <PSP/GAME directory>
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/GAME_ASSETS_SRC"
ISO="$SRC/UT99.iso"
MD5=461272d9e8bef59a72aa3b1806b38b3f
ITEM=ut-99_202512

cat <<'EOF'
Unreal Tournament is (c) Epic Games. This downloads a copy of the retail
CD from archive.org for use with this fan port; only use it if you own the
game. Press Enter to continue or Ctrl-C to stop.
EOF
read -r _

md5_of() { if command -v md5 >/dev/null; then md5 -q "$1"; else md5sum "$1" | cut -d' ' -f1; fi; }

mkdir -p "$SRC"
if [ -f "$ISO" ] && [ "$(md5_of "$ISO")" = "$MD5" ]; then
  echo "==> $ISO already downloaded"
else
  # archive.org's download link redirects to one storage node, which often
  # answers 500 or crawls at ~100 KB/s per connection; both nodes that hold
  # the item serve it directly, so spread connections over both.
  NODES=$(curl -fsSL "https://archive.org/metadata/$ITEM" | sed -n 's/.*"workable_servers":\[\([^]]*\)\].*/\1/p' | tr -d '"' | tr ',' ' ')
  DIR=$(curl -fsSL "https://archive.org/metadata/$ITEM" | sed -n 's/.*"dir":"\([^"]*\)".*/\1/p')
  URLS=""
  for n in $NODES; do URLS="$URLS https://$n$DIR/UT99.iso"; done
  [ -n "$URLS" ] || URLS="https://archive.org/download/$ITEM/UT99.iso"
  echo "==> downloading UT99.iso (759 MB)"
  if command -v aria2c >/dev/null; then
    aria2c -c -x16 -s32 -k2M --max-tries=0 --retry-wait=5 --console-log-level=warn \
      -d "$SRC" -o UT99.iso $URLS
  else
    echo "    (install aria2 for a much faster parallel download: brew/apt install aria2)"
    set -- $URLS
    until curl -fL -C - --retry 5 -o "$ISO" "$1"; do echo "    retrying..."; sleep 10; done
  fi
  [ "$(md5_of "$ISO")" = "$MD5" ] || { echo "error: checksum mismatch for $ISO" >&2; exit 1; }
fi

echo "==> unpacking System, Maps, Textures, Sounds, Music into GAME_ASSETS/"
mkdir -p "$HERE/GAME_ASSETS"
if command -v bsdtar >/dev/null; then
  bsdtar -xf "$ISO" -C "$HERE/GAME_ASSETS" System Maps Textures Sounds Music
elif command -v 7z >/dev/null; then
  7z x -y -o"$HERE/GAME_ASSETS" "$ISO" System Maps Textures Sounds Music >/dev/null
else
  echo "error: need bsdtar (libarchive-tools) or 7z to unpack the ISO" >&2; exit 1
fi
chmod -R u+w "$HERE/GAME_ASSETS"
echo "==> done: $(du -sh "$HERE/GAME_ASSETS" | cut -f1) in GAME_ASSETS/"
