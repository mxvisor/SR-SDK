# SR-SDK

A build kit and a shared runtime for native ports of DOS and Windows 9x games, made
with [SR](https://github.com/M-HT/SR), the static recompiler by Roman Pauer. A port
built with it runs the game's original code, recompiled, as a native SDL2 program
on Linux (x86-64, arm64), Windows (x86-64) and Android (arm64).

SR turns a game's original executable into source code: x86, ARM or x64
assembler, or *llasm*, an IR that LLVM compiles for any target. A playable port
also needs a runtime around that code: the DOS, DPMI, Win32, DirectX, video, sound,
input and C library services the game used to call. Each of SR's own ports carries a
full copy of that runtime. SR-SDK keeps the shared part in one place, together with
the build, so a game repository holds only what is specific to its game.

## How a port is made

1. **Input.** The original executable, taken from the game's disc image (`.iso`,
   `.bin/.cue`, `.nrg`, `.mds`, `.ccd`, or an installer it unpacks) and checked
   against its SHA-256.
2. **Recompilation.** SR (`SR/SR` for DOS LE programs with DOS/4GW, `SR/SRW` for
   Win32 PE programs and their DLLs) writes llasm.
3. **llasm → LLVM.** `llasm/llasm.py` lowers it to LLVM IR; `opt` and `llc` compile it
   for each device. Every build is position-independent.
4. **Runtime.** The recompiled code is linked with `runtime/` and the game's own
   `game/` code, against SDL2 and SDL2_mixer.
5. **Distribution.** `make distrib` makes the release archives: the program and the
   game directory, as the game's own installer would make it, from the disc image;
   `make apk` the Android package.

A game describes itself in `game/game.cfg` (the platform, the modules, the devices,
the program's name) and `game/Game_hooks.h` (what the runtime asks of it); everything
else is SR-SDK's.

## What the runtime gives a port

- **DOS** (`runtime/dos`): DOS/4GW programs started at their own entry; INT 21h, DPMI,
  conventional memory and the PSP; the PIT and the INT 8 → INT 1Ch chain; the keyboard
  controller, BIOS keyboard and INT 33h mouse; VGA mode 13h (also unchained, "mode X") and
  its DAC, mode 12h, VESA; MSCDEX; an emulated Sound Blaster 2.0 with DMA and its interrupt.
- **Win32** (`runtime/win32/winapi`): kernel32, user32, gdi32, the registry, threads,
  the window menu and the game's dialogs, GDI with DIB sections and palettes,
  winmm and MCI (CD audio), MIDI output, DirectDraw, DirectInput, DirectSound,
  Video for Windows (AVI), the game's own DLLs recompiled beside it.
- **3D**: Direct3D 1 and 2 drawn with OpenGL or a software rasteriser; 3dfx Glide 2
  drawn with OpenGL; OpenGL ES 3 on Android. Mods such as a higher internal resolution
  and multisampling, off by default.
- **Disc and files**: the game directory, a write root over it, drive D: from a cue
  sheet or from a copy of the disc, its audio tracks as ogg/flac/wav files.
- **Sound and music**: one SDL_mixer device for CD audio, digital sound and MIDI;
  XMIDI/MIDI synthesized with TinySoundFont, FM music on an emulated OPL2.
- **The port's own frame**: a resizable window or full screen that keeps the picture's
  shape; picture filters through OpenGL; an on-screen panel on one key (F12) for the
  port's settings, saved in `<program>.cfg`; gamepads; on-screen buttons on Android; the
  gyroscope of a phone as the mouse of a DOS game (a mod);
  crash reports with the guest's registers.
- **Checks**: `tools/regress/run_all.sh` builds and runs the ports a workspace lists under
  Linux and Wine, at different load addresses, and compares SR's output for SR's own
  reference games.

## Ports

The games ported with SR-SDK so far. This is the one place the SDK names them: nothing
else in it is written for one game. A name links to the port's repository, where it is
published.

<!-- ports -->
| Game | Original | Builds | State |
|---|---|---|---|
| [Comix Zone](https://github.com/mxvisor/comix_zone-recomp) | Windows 95: WinG, Miles | Linux, Windows, Android | plays |
| [Cyberia](https://github.com/mxvisor/cyberia-recomp) | DOS, DOS/4GW | Linux, Windows, Android | plays |
| [Ignition](https://github.com/mxvisor/ignition-recomp) | three programs: DOS; Windows, DirectDraw; Windows, Glide (3dfx) | Linux, Windows, Android | all three play |
| [Nightmare Creatures](https://github.com/mxvisor/nightmare_creatures-recomp) | Windows, Direct3D | Linux, Windows, Android | menus, levels, fighting; a full playthrough not checked |
| [Take No Prisoners](https://github.com/mxvisor/take_no_prisoners-recomp) | Windows, DirectDraw and Direct3D | Linux, Windows, Android | plays, with known bugs |
| Mass Destruction | DOS, DOS/4GW | Linux, Windows, Android | plays; an enhanced renderer (mod) |
| Incoming | Windows, Direct3D | Linux, Windows, Android | plays |
| Need for Speed III: Hot Pursuit | Windows, Direct3D and Glide | Linux, Windows | menus and races |
| Pitfall: The Mayan Adventure | Windows 95, GDI | Linux, Windows | plays; the settings dialog not yet |
| Syndicate | DOS (with its intro program) | Linux, Windows, Android | intro, menus, a mission's start |
| The Lion King | DOS, DOS/4G | Linux, Windows, Android | plays: levels, sound (DSMI) |
| Ecstatica II | two programs: DOS, DOS/4GW; Windows 95 (the official patch's): DirectDraw, DirectSound, MIDI, its music driver's DLL | DOS: Linux, Windows, Android; Windows 95: Linux, Windows | work in progress: in both programs the intro with music and sound, the menu |
<!-- /ports -->

## A game repository

```
game repository
├── game/            the game's runtime, hooks and SR inputs (game.cfg, recomp/)
├── SConstruct       one line: SConscript('SR-SDK/scons/game.py')
├── Makefile         one line: include SR-SDK/scons/game.mk
└── SR-SDK/          this repository, a submodule
    └── SR/          SR itself, a submodule
```

Each port is a repository of its own, with SR-SDK as a submodule. No game data is part
of it: the build and the game need the user's own copy of the game's disc.

```sh
SR-SDK/tools/new_game.sh DIR NAME dos|win32 --image IMAGE --exe disc:NAME   # a new port's skeleton
make linux        # build/bin/x64-linux/<program>-x64
make windows      # the MinGW-w64 build, with the SDL DLLs
make distrib      # the release archive: both programs and the game directory
make apk          # Android, arm64 (ANDROID_HOME)
./run-sdl.sh      # run the port on build/distrib/data; it writes into build/run
```

## Requirements

- Python 3 and [SCons](https://scons.org/)
- a C/C++ compiler: GCC or Clang for Linux, MinGW-w64 for Windows (it also
  cross-builds from Linux)
- LLVM's `opt` and `llc`, for the llasm backend
- SDL2 and SDL2_mixer
- for a game with MIDI music (`soundfont=` in its `game/distrib.cfg`): FluidR3_GM, the package
  `fluid-soundfont-gm` (`soundfont-fluid` on Arch), or the file in `$SR_SOUNDFONT`
- for Android: the Android SDK with an NDK and a JDK, CMake, Ninja and curl
  (SDL itself is downloaded and built by `tools/android_sdl.sh`)

No D compiler is needed: llasm IR is translated by `llasm/llasm.py`, a Python version of
SR's `llasm` that is developed here.

## Layout and documentation

| Directory | What | Documentation |
|---|---|---|
| `SR/` | submodule: the recompiler, the fork [mxvisor/SR](https://github.com/mxvisor/SR), branch `master` (upstream plus fixes that can be offered back to it) | SR's own |
| `llasm/` | `llasm.py`, the Python port of SR's llasm, and its tests | [`llasm/README.md`](llasm/README.md) |
| `scons/` | the build of one game (`game.py`, `game.mk`, `release.SConscript`): `game.cfg`, the stages, the `make` targets, several programs of one game | [`scons/README.md`](scons/README.md) |
| `runtime/` | the runtime: the guest's memory, the hooks of a game, the window | [`runtime/README.md`](runtime/README.md) |
| `runtime/core/` | what the DOS and the Win32 ports share: files, the disc, sound, music, input | [`runtime/core/README.md`](runtime/core/README.md) |
| `runtime/dos/` | the runtime of a DOS/4GW game and its hooks | [`runtime/dos/README.md`](runtime/dos/README.md) |
| `runtime/win32/` | the Win32 API layer, Glide and Direct3D on OpenGL, and their hooks | [`runtime/win32/README.md`](runtime/win32/README.md) |
| `tools/` | a new port, disc images, the distrib copy, Android, running the original | [`tools/README.md`](tools/README.md) |
| `tools/analysis/` | the work on a game's SR input (LE/PE, coverage) | [`tools/analysis/README.md`](tools/analysis/README.md) |
| `tools/regress/` | regression checks: SR's output, builds, smoke tests, llasm hashes | [`tools/regress/README.md`](tools/regress/README.md) |
| `android/` | what the APK adds to SDL's android-project | [`tools/README.md`](tools/README.md#android) |
| `extern/` | submodules: [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIDI), [emu8950](https://github.com/digital-sound-antiques/emu8950) (OPL2) | [`runtime/core/README.md`](runtime/core/README.md#extern) |
| `skeleton/` | a starting point for a new game repository -- to come | |

[`PORTING_DOS.md`](PORTING_DOS.md) walks through a DOS port: what SR stops at, the crashes
of a first run, the flags, the sound, the traps.

## License

MIT, see [`LICENSE`](LICENSE). SR-SDK contains files taken from SR and
derived from it; they keep Roman Pauer's copyright, and files changed here add
their own line. [`NOTICE.md`](NOTICE.md) names the third-party code (SR, xBR,
TinySoundFont, emu8950, printf, blast, SDL's Android project) with its license texts.

Thanks to Roman Pauer for SR and its ports, on which all of this stands.
