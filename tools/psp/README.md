# PSP debugging helpers

For developing against a real PSP over [PSPLink](https://github.com/pspdev/psplinkusb). None of this
is needed to build or play.

1. Build the PSPLink variant (log mirrored to pspsh over USB):
   `cmake -G "Unix Makefiles" -B build-psplink -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_PRX=ON -DPSPLINK=ON Source && cmake --build build-psplink`
2. Put a copy of the game where PSPLink's host filesystem serves it:
   `./install-psp.sh ~/.ut-psp/psplink_host` (so the card needs no copy; loading is slower over USB).
3. Start PSPLink on the PSP, connect USB, and run:
   `tools/psp/run-hw.sh <label> <seconds> '<map URL> [switches]'`
   e.g. `tools/psp/run-hw.sh ctf 160 'CTF-Face?Game=Botpack.CTFGame -EXECAT=60:press_Joy11'`

The map URL must come first. The transcript lands in `~/.ut-psp/hw-<label>.log` with a frame-rate
summary. `-ROOT=<dir>/` picks where the game data is read from (default for these runs:
`host0:/UnrealTournament/System/`). `-EXECAT=<secs>:<command>;...` runs console commands at set times;
`press_<Key>` sends a real key press (`_` stands for a space). Resolve a crash address with
`psp-addr2line -f -C -i -e build-psplink/UnrealTournament/UnrealTournament <addr>`.

If pspsh stops answering, the PSP is sitting on a crash: power-cycle it and start PSPLink again.
