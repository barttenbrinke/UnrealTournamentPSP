# Unreal Tournament (UT99) on the PSP

A port of the original *Unreal Tournament* (v400) to the PSP-2000 and later models, from a homebrew
EBOOT. It is early work: it boots, loads deathmatch and CTF maps with bots, and has sound and music,
but so far it has mostly been tested in the PPSSPP emulator. Expect bugs, and expect it to be slower
on a real console than in the emulator.

It is built on [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) (the UT v400 engine as ported
to the Dreamcast) and reuses the PSP renderer, file and audio layers of the
[Unreal (1998) PSP port](https://github.com/barttenbrinke/UE1).

## Install

You need a PSP-2000, 3000, Go or Street with custom firmware that runs homebrew, a Memory Stick with
about 650 MB free, and a computer (macOS, Linux, or Windows with WSL) to run two shell scripts.

```
git clone https://github.com/barttenbrinke/UnrealTournamentPSP.git && cd UnrealTournamentPSP
./fetch-assets.sh                                   # downloads the UT v400 CD image from archive.org (~760 MB)
./install-psp.sh /Volumes/<your stick>/PSP/GAME     # copies the EBOOT, the game data and the settings
```

`fetch-assets.sh` uses `aria2c` when it is installed (much faster on archive.org) and needs `bsdtar`
or `7z` to unpack the image. The port needs the original **v400** data: later patches (GOTY, 436,
469) are not compatible with this engine. If you own the CD, copy its `System`, `Maps`, `Textures`,
`Sounds` and `Music` folders into `GAME_ASSETS/` instead. `EBOOT.PBP` in the repository is the current
build.

For PPSSPP, install to `~/.config/ppsspp/PSP/GAME` (the installer moves the music off the Media Engine,
which PPSSPP does not emulate). A file `System/cmdline.txt` with a map URL, for example
`CTF-Face?Game=Botpack.CTFGame`, starts straight into that map.

## Controls

| Button | In game | In the menus |
|---|---|---|
| Analog stick | move / strafe | move the cursor |
| Triangle / Cross | look up / down | Enter / click |
| Square / Circle | turn left / right | right click / back |
| R / L | fire / alt-fire | |
| D-pad up / down | jump / duck | |
| D-pad left / right | previous / next weapon | |
| Start | menu | close |
| Select | scores | |

In deathmatch you start waiting for the match: press R (fire) to begin.

## Building

Install [pspdev](https://github.com/pspdev/pspdev), then `./build-psp.sh` (`--release` also copies
the EBOOT to the repository root). `tools/run-ppsspp.sh` runs a build in PPSSPP and prints the log.

Unreal Tournament is (c) Epic Games. This is an unofficial fan project and contains no game data.
