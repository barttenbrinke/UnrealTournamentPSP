#!/usr/bin/env bash
#
# build-psp.sh -- configure and build the PSP EBOOT (build-psp/UnrealTournament/EBOOT.PBP).
# Needs the pspdev toolchain (https://github.com/pspdev/pspdev) with psp-gcc on PATH.
#
#   ./build-psp.sh            card build
#   ./build-psp.sh --release  also copy the EBOOT to the repository root
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PSPDEV="${PSPDEV:-$(dirname "$(dirname "$(command -v psp-gcc)")")}"
TOOLCHAIN="$PSPDEV/psp/share/pspdev.cmake"
[[ -f "$TOOLCHAIN" ]] || { echo "error: toolchain file not found at $TOOLCHAIN (is PSPDEV set?)" >&2; exit 1; }
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

# BUILD_PRX: the EBOOT carries a stripped, relocatable PRX (as in the Unreal
# port) instead of the ELF with its debug info; keep build-psp/UnrealTournament/
# UnrealTournament for psp-addr2line.
if [[ ! -f "$HERE/build-psp/CMakeCache.txt" ]]; then
  cmake -G "Unix Makefiles" -B "$HERE/build-psp" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_PRX=ON -DPSP_HEAPCHECK=OFF "$HERE/Source"
fi
cmake --build "$HERE/build-psp" -j"$JOBS"
echo "==> $HERE/build-psp/UnrealTournament/EBOOT.PBP ($(du -h "$HERE/build-psp/UnrealTournament/EBOOT.PBP" | cut -f1))"

if [[ "${1:-}" == "--release" ]]; then
  cp "$HERE/build-psp/UnrealTournament/EBOOT.PBP" "$HERE/EBOOT.PBP"
  echo "==> copied to $HERE/EBOOT.PBP"
fi
