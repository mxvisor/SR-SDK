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
  its DAC, mode 12h; MSCDEX; an emulated Sound Blaster 2.0 with DMA and its interrupt.
- **Win32** (`runtime/win32/winapi`): kernel32, user32, gdi32, the registry, threads,
  the window menu and the game's dialogs, GDI with DIB sections and palettes,
  winmm and MCI (CD audio), DirectDraw,
  DirectInput, DirectSound, Video for Windows (AVI).
- **3D**: Direct3D 1 and 2 drawn with OpenGL or a software rasteriser; 3dfx Glide 2
  drawn with OpenGL; OpenGL ES 3 on Android. Mods such as a higher internal resolution
  and multisampling, off by default.
- **Disc and files**: the game directory, a write root over it, drive D: from a cue
  sheet or from a copy of the disc, its audio tracks as ogg/flac/wav files.
- **Sound and music**: one SDL_mixer device for CD audio, digital sound and MIDI;
  XMIDI/MIDI synthesized with TinySoundFont.
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
| Ecstatica II | DOS, DOS/4GW | Linux, Windows, Android | work in progress: the intro with music and sound |
<!-- /ports -->

## A game repository

```
game repository
├── game/            the game's runtime, hooks and SR inputs (game.cfg, recomp/)
├── SConstruct       one line: SConscript('SR-SDK/scons/game.py')
└── SR-SDK/          this repository, a submodule
    └── SR/          SR itself, a submodule
```

Each port is a repository of its own, with SR-SDK as a submodule. No game data is part
of it: the build and the game need the user's own copy of the game's disc.

SR is used as it is: the submodule `SR/` is the fork
[mxvisor/SR](https://github.com/mxvisor/SR), branch `master`, which is upstream
plus fixes that can be offered back to it. SR-SDK does not change SR.

`extern/tsf/` is the [TinySoundFont](https://github.com/schellingb/TinySoundFont)
submodule: `tsf.h` (MIT) and `tml.h` (Zlib), header-only. Its directory is on
the game's include path, for ports that synthesize MIDI music in the audio
callback; SDL2_mixer's own MIDI backend is then not needed.

`extern/emu8950/` is the [emu8950](https://github.com/digital-sound-antiques/emu8950)
submodule (MIT): an emulator of the YM3812, the OPL2 FM chip of the Ad Lib, in C, for
the FM music of the DOS games (`runtime/core/Game_opl.c`).

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

No D compiler is needed: llasm IR is translated by `llasm/llasm.py`, a Python
version of SR's `llasm` that is developed here; on SR's reference games its output is byte-identical to the D original (see
[`llasm/README.md`](llasm/README.md)). `llasm.py` and `opt` make one bitcode per `.llasm` for all
devices, `build/obj/ir/*.bc`; each device runs only `llc` on it, into `build/obj/<device>/gen/*.o`.
To find a procedure of the original in a debugger, delete `build/obj/ir/*.bc` and build once with
`LLASM_SYMBOLS=1`: every procedure and every label of the data keeps its name (`loc_XXXX`, the
address in the original) in the object, for `nm` and `gdb`, as a local symbol (the modules of
`dll_modules=` have labels of the same names); the variable is not part of the command, so SCons
does not see it change.

## Starting a new port

`tools/new_game.sh DIR NAME dos|win32 [--exe PATH|disc:NAME] [--image IMAGE]` makes the skeleton of a game
repository: `SConstruct`, `Makefile`, `.gitignore`, `game/game.cfg`,
`game/Game_hooks.h`, `game/recomp/Game-main/default/` (`sha256`, `SR.cfg`), the
launchers (`run-sdl.sh`, `run-sdl-wine.sh`, `run-orig-dosbox.sh` or
`run-orig-wine.sh`), a git repository and SR-SDK (with SR) as a submodule. With
`--exe` the original's sha256 is written, and `scons stage=input` then passes. With
`--image` the disc is described in `game/distrib.cfg`, and `--exe disc:NAME` takes the
executable from the disc (`build/disc/`, unpacked by `tools/disc_tree.py`) instead of a
file unpacked by hand. The disc image is the one path outside the repository: the
launchers run the game on `build/distrib/data` (`make distrib-data`, made on the first
run) and the port writes into `build/run`.
For a DOS program `tools/sr_run.py` runs SR twice when it must: SR's `--list_handlers=FILE`
lists the routines the program gives to INT 21h AH=25h (EDX loaded with an address, directly or
through a wrapper routine that takes it from its stack argument), and the second run takes them as
code, so the game's `fixup_interpret_as_code.sci` is for what this pattern misses. A DOS4GW program
also gets `esp_dword_aligned`, `ebp_dword_aligned` and `code_writable` in its `SR.cfg` from
`scons/game.py`. Keys of `SR.cfg` a game may need (off by default; `SR/SR/SR_main.c`):
`int_stub_table=yes` (a WATCOM library whose int386x() returns into a table of 256 stubs
`int N / ret`: SR finds the table and gives the instruction that takes its address a stub of
its own), `exact_push16=yes` (a 16-bit push or pop moves esp by 2, as the CPU does: for code
that measures the distance of a pointer from esp after such pushes), `self_modifying_code=yes`
(the program writes immediates and displacements of its own instructions: each such instruction
gets a copy in the data segment `smc`, the writes go to the copy and the instruction reads its
operand from it; SR and SRW; [PORTING_DOS.md](PORTING_DOS.md#4-srs-first-run-scons-stagerecomp)) and
`global_prefix=<name>` (before every global name of the output; set by `scons/game.py` for
`exe_modules=`).
In SRW's `instruction_replacements.sci`, a replacement whose llasm ends with `endp` (after a `tcall`
of its own) ends its procedure: the bytes after it are code only when something else reaches them,
and a `loc_X` of data that it names gets a label. So a `call` whose callee pops the return address
as the address of a table after the call (WATCOM's library: `call X` / `X: pop edi / ret`) becomes
`mov edi, loc_<table>|POP tmp1|tcall tmp1|endp`.
Next come `main.c`, SR's inputs (`SR.cfg`, `*.sci`) and the game's hooks. The
`TODO` lines of the launchers name what to fill in. SR's warnings about the game's
`fixup_interpret_as_code.sci` (a label of it that the code reads as memory: a table, not code) are
shown on the build's output as `SR: ...`. [`PORTING_DOS.md`](PORTING_DOS.md) walks through a DOS port:
what SR stops at, the crashes of a first run, the flags, the sound, the traps.

## Running the original

`run-orig-dosbox.sh` (DOS games) and `run-orig-wine.sh` (Windows games) of a game
repository run the original as the reference for the port, on the game directory of
the distrib copy (`build/distrib/data`): DOSBox mounts it as `C:` with an overlay and
the disc's cue sheet (`build/cd/`) as `D:`; Wine runs on a `fuse-overlayfs` mount of it
(a copy when it is missing), and `D:` is the unpacked disc (`build/disc`). What the
original writes lands in `build/run/orig-*`. `--headless[=N]` takes screenshots under
Xvfb (`build/shots/orig-*`), `--keys=` sends keys. A run in a window keeps what the
earlier runs wrote (saves, settings), a headless one starts clean, as the key sequences
expect; `--fresh` and `--keep-overlay` choose otherwise. The common part is
`tools/run_orig_common.sh`.

## Using it in a game repository

```sh
git submodule add https://github.com/mxvisor/SR-SDK.git SR-SDK
git submodule update --init --recursive
echo "SConscript('SR-SDK/scons/game.py')" > SConstruct
```

Everything about the game is in `game/game.cfg`:

```ini
platform=dos            # selects runtime/dos and SR; win32: runtime/win32 and SRW
archs=llasm             # SR's output type
modules=Game-main       # the modules of the original, game/recomp/<module>/
exe_modules=INTRO       # dos only: other programs the game runs (INT 21h AH=4Bh), in the same
                        # binary: game/recomp/<NAME>/, NAME.EXE of the disc (input_<NAME>=),
                        # their global names get the prefix <name>_ (SR.cfg global_prefix)
dll_modules=DRV         # win32 only: DLLs of the original recompiled as modules of their own
                        # (game/recomp/<NAME>/, NAME.DLL of the disc): LoadLibrary hands them out,
                        # and the program's imports of them (its import table) link to them, their
                        # DllMain run before its entry (tools/dll_exports.py)
variants=default        # recognized versions of the original, by sha256
input_exe=../path/to/GAME.EXE
link=separate
devices=x64-linux,x64-windows
winapi=yes              # win32 only: the Win32 API layer, runtime/win32/winapi (default yes)
glide=no                # win32 only: Glide 2.x on OpenGL, runtime/win32/glide
d3d_gl=no               # win32 only: Direct3D on OpenGL, runtime/win32/gl
runtime_skip=Game_cdrom.c,Game_file.c   # runtime .c files the game does not build
guest_data_in_memory=no # win32 only: the guest's data at the addresses of the original (llasm -guest-data-in-memory, runtime/win32/winapi/Game_guestdata.c)
llasm_flags=            # more flags of llasm
glibc_max=2.38          # Linux: the newest glibc the binary may ask for (also `scons glibc_max=`)
program=game                       # the port's name: build/bin/<device>/<program>-<arch>, and
                                   # the namespace of its files in the game's directory
dist_name=game                     # the name of the release archives (default program)
targets=                           # game/ only: the other programs, game-<name>/ (see "targets" below)
android_id=io.github.user.game   # for Android: the application id
android_name=Game                 # and the launcher's label
icon=disc:GAME.EXE                # the icon of the window, the Windows exe and the APK: the game's own
                                  # (an exe's first group icon, EXE#NAME for another, or an
                                  # .ico/.bmp), with "SR" drawn in its bottom right corner
                                  # (tools/game_icon.py); disc: and paths as input_exe
```

The port puts its files into the game's directory, all named after `program`:
`<program>-x64`, `<program>-x64.exe` and `<program>-arm64` (the programs: the name and the
architecture, the first word of the device, so the builds of several sit side by side;
`binary_name` of `scons/game.py`), `<program>.cfg` (the port's settings, one for all of them,
`Game_config.c`; `GAME_CONFIG_FILENAME` names another), `<program>.log` (the log of a
Windows program without a console, `Game_log.c`; `GAME_LOG_FILE` names another) and, in the
write root, `<program>.reg` (what a Win32 game wrote into its registry, `WinApi-registry.c`).  Windows
has one file for names that differ only in case, and the game's own files are there too, so
none of these names may be one of the game's: a distrib release compares them
(`stage_directory` of `scons/release.SConscript`) and fails with the pair of names.  The
architecture keeps the programs off the original's `<game>.exe`; a game with its own
`<program>.cfg` or `.log` renames the port.

`game/recomp/<module>/<variant>/` holds SR's inputs for the original: its
`sha256`, `SR.cfg` and the `*.sci` files. `sha256` may list several originals, one per
line (the hash first, then a note): releases of the game whose exe differs only where
SR's inputs do not care (two languages of one build) share them. Then:

```sh
scons                            # build/bin/<device>/<program>-<arch>
scons device=x64-windows         # Windows, cross-built with MinGW-w64
scons stage=gen                  # stop after SR's output
scons input_exe=/path/to/GAME.EXE
scons image=../iso/OTHER.bin     # another release's disc image than image= of game/distrib.cfg
```

`image=` (also `make <target> image=...`) stays the choice, in `build/disc-image`, for
the next builds, `build/disc` and `build/distrib/data`, until `image=default`; the exe
of `disc:` is taken from the disc again when it changed.

The game's `Makefile` is two lines, `include SR-SDK/scons/game.mk`: the targets are scons's
(`scons/game.py` and `scons/release.SConscript`), the `.mk` only chains them.
`make linux` (the binary and the `libSDL2_mixer-2.0.so.0` the linker found, beside it: the
binary's RPATH is `$ORIGIN`, so a release runs where the system has SDL2 and no SDL2_mixer,
as SteamOS), `make windows` (the MinGW build with the SDL DLLs beside the exe),
`make distrib-data` (`build/distrib/data`: the game directory of the fullest install with the music, from `game/distrib.cfg`),
`make distrib` (both builds, the data, and `build/distrib/<name>-x64.zip`:
both programs with the Windows DLLs and the Linux `.so`, `game/release/{linux,windows}/` and their `x64/`,
where `@GLIBC_MAX@` of a `.txt` is the build's `glibc_max`, `<program>-NOTICE.txt` (`LICENSE` and
`NOTICE.md`; the APK has it as `assets/NOTICE.txt`) and the data), `make release` (`build/distrib/<name>-x64-port.zip`:
the same without the game's data, the programs for a game already installed; the distrib archive and the APK hold the game's files), `make apk`, `make disc`
(`build/disc`), `make host|input|recomp|gen|obj` (one stage), `make clean` (everything built; the disc's unpacked files, `build/disc`, `build/cd` and
`build/distrib/data`, stay, so the tree builds on a machine without the disc image), `make distclean` (those too), `make test`.
The same names work as `scons <name>`; `MINGW_BIN=` points at the DLLs.

A Linux binary asks for no glibc newer than `glibc_max` (default 2.38), so it runs on systems
older than the one it is built on.  glibc keeps the old versions of a function it changes
(`sqrtf` and `log10f` got `GLIBC_2.43`, and still have `GLIBC_2.2.5`), and the linker takes the
newest: before the link, `scons/glibc_max.py` sends the calls of every object to the newest
version not above `glibc_max` (a trampoline with `.symver`, and `-Wl,--wrap=`); after it,
the build fails, naming them, when a function has no such version (the `__isoc23_*` of
glibc 2.38).

For Android, `tools/android_apk.py` makes the whole APK, from the game
repository's root:

```sh
ANDROID_HOME=/path/to/android-sdk \
    SR-SDK/tools/android_apk.py --data /path/to/game-data --out game.apk
```

It builds SDL2 and SDL2_mixer for arm64 once (`tools/android_sdl.sh`, into
`build/sysroot-android-arm64/`), then `libmain.so` with
`scons device=arm64-android`. The gradle project is put together in
`build/android/project/` from SDL's own `android-project` of the same release,
with `android/app.gradle` and `android/*.java` over it, and the
game's `game/android/res/` (its icons) over that. On the first launch, and
after each update of the APK, `UnpackActivity` (the launcher's) unpacks the data
from the APK's assets on a thread of its own, with its progress on the screen
(`assets/game.files` lists the tree), then starts `GameActivity` (SDL's): the
port reads its files with `fopen`, and a copy of hundreds of MB on the
activity's thread makes Android close the program as one that does not answer.

`tools/distrib_cd.py OUT_DIR`, from the game repository's root, makes the
game's data directory the port runs from, the directory the game's fullest
install makes: the files of the disc's data track that installer copies, where
it puts them, with what that install leaves on the disc and the game still reads
there at their disc paths (the game directory is the disc as well), the files
the installer would have written (with `unpack=`, a setup program of the disc
that keeps its files inside itself), and the audio tracks as flac, wav or ogg
files with a cue sheet. What to skip, move and add is in `game/distrib.cfg`; the
script's header lists its keys. Its `image=`, the disc image, is the one path outside the repository; its
cue sheet is made in `build/cd/`. A distrib copy has no launcher script: the
program finds the game in its own directory. The General MIDI bank of the port's synthesizer is
not kept in a repository: `soundfont=` lists the presets the game's music uses, and
`tools/sf2_subset.py` cuts them from the system's FluidR3_GM into `soundfont.sf2` (and its license) of
the game directory and of `make release`.

`tools/image/image.py` turns a disc image into what the rest of the SDK takes as
the disc, a cue sheet: `image.py disc IMAGE [-o DIR] [--iso] [--extract DIR]`
takes a Nero `.nrg` (a cue + bin pair is written, with the audio tracks), an
Alcohol 120% `.mds`/`.mdf` (a cue sheet that points at the `.mdf`), a CloneCD
`.ccd` (a cue sheet that points at the `.img` beside it, with all its tracks),
a raw `.bin` or an `.iso`. `--iso` also writes the data track as an ISO, `--extract` copies
its files. It writes only the files it names, and deletes nothing.
`image.py installer FILE OUT_DIR` unpacks an InstallShield 3 `.z` installer or
a Wise setup program (the game directory, `%MAINDIR%`). The format modules
(`nrg.py`, `mds.py`, `ccd.py`, `rawbin.py`, `installshield.py`, `wise.py`) share
`cdimage.py` with `distrib_cd.py`.

### Several programs of one game: targets

A repository may build several programs of one game, each from another exe of
the same disc (say, a 3dfx, a DirectDraw and a DOS exe).
`game/` is the main target; each other one is a directory `game-<name>/` with
a `game.cfg`, recomp inputs and code of its own (another `platform` too), and
`game/game.cfg` lists them: `targets=win,dos`. `scons target=<name>` builds one
(`target=default`, or none, the main one); its stages up to obj go to
`build/target-<name>/`. One game directory serves them all: `game/distrib.cfg`,
`game/release/`, `build/disc`, `build/distrib`, `build/bin` (each target has its own
`program`). `make linux`, `make windows` and the stages build every target
(`TARGETS=<name ...>` some of them), `make distrib` puts all programs into the
archive of the main one, `make apk TARGET=<name>` builds the APK of one (its own
`android_id`; `android_apk.py --target`). The launchers take `--target=<name>`
(`tools/run_common.sh`), the regression column 10 of `games.tsv`.

The stages are `host` (SR, built from a copy in `build/`), `input` (the
original, checked against its sha256), `recomp` (SR), `gen` (SR's output,
compacted), `obj` and `bin`. All output goes to `build/` of the game
repository, so the checkouts of SR-SDK and SR stay clean.
`scons/game.py` never names a game.

## Checking a change

`tools/regress/run_all.sh` answers "did I break anything?" after a change to SR,
to `llasm.py` or to the SDK: SR's output for SR's reference games, `make linux`
and `make windows` of every game, a start-and-close smoke test of every port under
Linux and Wine, runs at different load addresses (ASLR), the hashes of the llasm output
and the conversion of the disc images, all compared with a saved reference run. The games
it checks and that reference are the workspace's regression data (`$REGRESS_CONF`, by
default `regress/` beside `SR-SDK/`), not part of the SDK. See [`tools/regress/README.md`](tools/regress/README.md).

## What the SDK builds

Every build is position-independent (Linux and Android: PIE / `-fPIC`, llasm
`-pic -ptrofs`; Windows: dynamicbase, high-entropy VA, a `.reloc` section). Guest
pointers are 32-bit offsets from a load-time base, never absolute host addresses,
and there is no fixed-address mode.

The base, `pointer_offset`, is the bottom of the guest window: the 2 GB the guest
sees as its addresses 0 .. 2 GB, the same on Linux, Windows and Android
(`runtime/core/Game_memory.c`). The window is reserved whole at the start (address
space only, no memory), so nothing of the host's lands in it later. From the
bottom: the guest's low memory (`GAME_GUEST_LOW_MEMORY()`), the guest's heap from
the bottom up, the host's image at 64 MB (`android/gameloader.c` loads it there on
Android), and a DOS program's own memory from the top down. What the host already
had in the window (the image, the heap of `brk`) is left out. `SR_SDK_MEMORY_MAP=1`
prints the map of the window at the start; a request that finds no room prints it
as well. The window is the only place of the guest's memory: there is no other way to
find it, and a program that cannot set the window up stops with its map. The runtime
is built for 64-bit hosts only (`PTROFS_64BIT`).

The low memory is the same for every game, DOS or Windows: reserved with no access,
so a NULL pointer faults, and the part a game needs becomes memory (zeros) with
`commit_guest_low(size)`: the DOS memory of a DOS game (`runtime/dos/Game_dosmem.c`,
the PSP at 10000h and the VGA window at A0000h at their own addresses), the data of
a `guest_data_in_memory` game, the page 0 of Windows 9x (`GAME_GUEST_PAGE0`). C code
turns a guest address into a pointer and back with `GAME_GUEST32_TO_PTR` /
`GAME_PTR_TO_GUEST32`, or `GUEST_MEM` / `GUEST_ADDR` when 0 has to stay NULL
(`runtime/core/Game_memory.h`).

- every `.c` of `runtime/core/` and `runtime/<platform>/`, unless `game/` has
  a file with the same relative path: the game's file replaces it;
- for the llasm backend, the `.c` of each `llasm_X.llinc` that the game's
  `llasm.llinc` includes, straight from `SR/SR/llasm-support/`;
- `game/`, `game/display/`, `game/audio/`, `game/input/`: every `.c`, or the
  directory's own `SConscript` when it has one;
- `game/llasm/`: its own `SConscript` when it has one. Otherwise `scons/llasm.SConscript`
  builds the game's `.c` and `.llasm`, and those of the runtime's `llasm/` directories
  (`runtime/core/llasm/`, `runtime/<platform>/llasm/`, `runtime/win32/winapi/llasm/`), except the files the game has
  under the same name.

Headers are looked up in `game/` first, then in the runtime, then in SR's
`llasm-support/`. A runtime `.c` finds the runtime's own
headers first, so a header the game overrides affects only the game's code.

## What a game provides

`game/Game_hooks.h`:

| Macro | Meaning |
|---|---|
| `GAME_MAIN_FILENAME` | the original's file name, as `argv[0]` of its `main` |
| `GAME_NAME` | the game's name in the port's messages |
| `GAME_ENV(name)` | the port's environment variables: `"XX_" name` |
| `GAME_CD_MARKER` | a file or directory the game reads from the CD: a game directory holding it is also the disc (a distrib copy); without it, only a disc image is found |
| `GAME_CD_OPTIONAL` | the game works without its disc: "no CD" is printed only when the CD trace is on |
| `GAME_DATA_MARKERS` | file names, one of which the game directory holds (`"A.DAT", "B.BAT"`); default: `GAME_MAIN_FILENAME` |
| `GAME_FLIP_HOOK()` | optional: run before each frame is shown |
| `GAME_OSD_ITEMS()` | optional: after the settings, registers the game's own items of the port's panel (a switch of a mod say) with `Game_OsdAddItem` (`runtime/core/Game_osd.h`). The panel's key (`Display_OSD_Key`, F12) is the port's only key: no item has a key of its own; an item with a key of the settings file is saved there when it is switched (`Game_ConfigSave`). Enter, a click and a touch switch an item to its next value, round; an item of values in an order (sizes) has `step`, and Left/Right go to the smaller/larger value; an item of many values has `count`, `value` and `choose`: Enter, a click, a touch or the pad's A open the list of its values under it. The rows scroll when they do not fit; on a touch screen (Android, a touch device, `SteamDeck` in the environment) a row is about 7 mm high and a tap chooses it when the finger is lifted, a finger drawn up or down scrolls |
| `GAME_KEYBOARD_BLOCKED()`, `GAME_MOUSE_BLOCKED()` | optional: non-zero while the game reads keys or the mouse by itself |

A DOS game (`runtime/dos`) also takes, all optional:

| Macro | Meaning |
|---|---|
| `GAME_ENTRY` | the llasm symbol the game starts at (`Game-asm.c`): default `c_start_`, the program's entry point, which SR exports by itself; `c_main_` is the game's C `main` named by `main_` in `global_aliases.sci`, for a port that skips the startup code |
| `GAME_PROGRAM_MEMORY`, `GAME_PROGRAM_STACK` | `Game-asm.c`, a game started at its entry point: the size of the program's memory block (default 64 MiB, less if the guest window has no room) and of the stack in it (1 MiB); the startup code grows its heap in the block (INT 21h EAX=80004800h of the flat model it is told it runs under: `Game_dosint.c`, AH=30h) |
| `GAME_ARGS(argc, argv)` | `Game_dosmain.c`: takes the port's own options out of `argv`, returns the new `argc` (default `Game_ReadArgs`: `--config-file=`) |
| `GAME_CONFIG()` | the port's configuration, read after the options and the defaults of `Init_Display`/`Init_Audio`/`Init_Input` (default `Game_ReadConfig`, `runtime/core/Game_config.c`: `--config-file=`, else `<program>.cfg` in the data directory or the write root; `Sound`, `Music` and the keys of the display, audio and input) |
| `GAME_CONFIG_KEY(key, value)` | a key of the settings file that is the game's (`Game_config.c`, DOS and Win32); nonzero: taken |
| `GAME_DISPLAY_INIT()`, `GAME_DISPLAY_INIT2()`, `GAME_DISPLAY_CLEANUP()`, `GAME_DISPLAY_DESTROY()` | `core/display/pc.c`, the default display: after the defaults of `Init_Display`; at the end of `Init_Display2` (may change `Render_Width`, `Render_Height`, `Display_Flip_Procedure`); in `Cleanup_Display`; in `Game_Display_Destroy` |
| `GAME_DISPLAY_WINDOW_FLAGS`, `GAME_DISPLAY_FLIP()` | the same: more `SDL_WINDOW_*` flags; the start of `Game_Display_Flip`, on the flip thread (nonzero: the game showed the frame itself) |
| `GAME_DISPLAY_MAX_PIXELS` | the same: the most pixels of the frame, for a `Display_Flip_Procedure` that changes `Render_Width` and `Render_Height` as the game runs (default 1024×768) |
| `GAME_RUNTIME_INIT()` / `GAME_RUNTIME_DEINIT()` | the game's own part of the runtime (nonzero: failed). `Game_dosmain.c` makes the DOS world of every game first: conventional memory at its own addresses (`Game_dosmem.h`: the PSP with the command line and its environment at 10000h, the VGA window at A0000h as `Game_FrameBuffer`), the disc and the music; the game's end comes before the world's |
| `GAME_DOS_ENV` | the variables of the DOS environment, `"NAME=VALUE"` strings separated by commas; WATCOM's startup builds the array of their addresses itself; for a game that starts at its main (`GAME_ENTRY`), which skips that, it is `Game_DosEnviron`, and `instruction_replacements.sci` points the C library's `getenv` at it |
| `GAME_DOS_SOUNDBLASTER` | the Sound Blaster 2.0 of `Game_dossb.c` (DSP, 8-bit DMA, the IRQ from `SR_CheckTimer`), with `BLASTER=A220 I5 D1 T1` in the environment; `GAME_SB_BASE`, `GAME_SB_IRQ`, `GAME_SB_DMA` change the card. The IRQ handler the game installs has to be translated code: SR finds a handler given to INT 21h AH=25h by itself (see below) |
| `GAME_DOS_SB_OPL` | with `GAME_DOS_SOUNDBLASTER`: the card's FM chip, at base+8/9 and at the Ad Lib's 388h/389h (the OPL2 of `core/Game_opl.c`, opened at the first write; the status with the chip's two timers), for a driver that plays its music on the card's FM |
| `GAME_DOS_REALMODE` | `Game_dosreal.c`: an interpreter of the 8086 (with the 80186's additions) for the 16-bit code a game loads into its DOS memory at run time and calls through a real-mode vector (a sound driver read from a file), which SR cannot translate: DPMI 0201h, an INT that protected mode does not handle, DPMI 0300h, and the card's IRQ the game's handler passes on. Without it those find no real-mode handler, as before |
| `GAME_DOS_KBD_MIN_HOLD_MS` | `Game_doskbd.c`: how long (ms) a key's press reaches the game's INT 9 handler before its release, and its release before its next press (default 60: a tap the event loop hands over in one batch is seen by a game that reads the keys once a frame); counted for each key on its own, so the keys of two fingers keep their times. 0 hands the bytes over as they come, as DOSBox does |
| `GAME_DOS_VESA` | the VESA BIOS of `Game_dosvga.c` (INT 10h AH=4Fh, VBE 1.2): the 256-colour modes 100h, 101h, 103h and 105h, banked through the window at A0000h (4F05h, no window function), with 4F06h and 4F07h; the blocks of 4F00h and 4F01h at ES:DI of a real-mode call (DPMI 0300h). Without it AH=4Fh fails as before: a game that only probes VESA keeps its VGA modes |
| `GAME_DOS_DSMI` | `Game_dosdsmi.c`: DSMI's MCP (Otto Chrons' Multi Channel Player 2.6) mixed on the host: the game's `instruction_replacements.sci` puts `call Game_DsmiMcpGetDelta` at MCP's getdelta and `call Game_DsmiMcpPoll microseconds, vars, order` at its poll (CDI entries 38h and 34h; the head of the file says how to find them and MCP's variables); the rest of DSMI (AMP, the CDI, the game's effects) stays the game's |
| `GAME_LOG_FILE` | Windows: the name of the log file of a program without a console (default `<program>.log`) |
| `GAME_MIXER_CHANNELS` | `core/Game_audio.c`, the SDL_mixer device of every port (DOS and Win32): its channels (default SDL_mixer's 8) |
| `GAME_AUDIO_ON_MIXER` | `core/Game_audiostream.c`: 1 plays the PCM streams (the Sound Blaster, DirectSound, a game's own driver) in SDL_mixer's output after its mix, 0 on devices of their own; the default is 1 on Android, where SDL has one playback device, and 0 elsewhere |
| `GAME_DOS_INTERRUPT(number, regs)` | the game's own INT services, tried first (nonzero: served) |
| `GAME_DOS_REALMODE_INTERRUPT(vector, regs, es)` | DPMI 0300h (a real-mode interrupt) for a vector the shared services do not know, on the copy of the call structure; `es` is the structure's ES (nonzero: served; otherwise INT 10h, 1Ah, 21h and 2Fh go to the shared services and every other vector fails with CF set) |
| `GAME_DUMP_GUEST_MEMORY()` | called when the game opens its error log: a debug dump |
| `GAME_DOS_AIL_PERIOD_POINTER()` / `GAME_DOS_AIL_SAMPLE_NAME(address)` / `GAME_DOS_AIL_SERVICE_US` | `Game_dosail.c`: where the game reads AIL's current PIT period (a dword of microseconds, kept current); the name of the sound at a guest address, for `$<GAME>_TRACE_SOUND`; the period of the DIG driver's own service timer (default 5000 us, SB16.DIG's) |
| `GAME_DOS_TIMER_TICK()` | what the PIT's channel 0 interrupt runs in `Game_dostimer.c`'s `SR_CheckTimer`, in place of `Game_DosTimerTick` (the guest's INT 8 and INT 1Ch handlers): a sound driver's timer service and then the game's handler |
| `GAME_DOS_MOUSE_MARGIN_Y(width, height)` | `Game_dosmouse.c`: the rows of the picture (the mode's `width` × `height`) above and below the part the mouse's range covers, for a game that draws a smaller screen in the middle and sets the range to it; a touch and the gyroscope then put the pointer under the finger (default 0: the range covers the picture) |

`Game_dosmain.c` is the `main` of a DOS game's port, `Game_dostimer.c` its
`SR_CheckTimer`; a game with its own version of one lists it in `runtime_skip` and brings the
functions the file's head names.

`runtime/core/display/pc.c` is the window of every port (mode-13h palette, 4:3, shown through
OpenGL by the nearest pixel, the original's look, or with the 2D filter of
`Display_Shader=none|smooth|pixelart|scanlines|crt|xbr|super-xbr` (a mod, also an item of the port's panel); OpenGL 2.0 (GLES 3.0 on Android) is needed, without it
`Game_GlUnavailable` says so in a message box and the program ends; on Win32 the window
stays hidden, the game draws in its own, and DirectDraw shows its picture through OpenGL too,
with the same filter). Every window of a picture comes from `Game_WindowCreate` (`Game_eventloop.c`,
`Game_thread.h`): it opens at the picture's own size at its aspect (320x240 for mode 13h, a Win32
game's display mode), the player makes it bigger and the picture stays at its aspect inside, with
black bars; a new mode resizes a window the player did not; `Display_Fullscreen=off|on` (the item
Fullscreen of the panel) puts it on the whole screen; `Display_FPS=off|on` (the item FPS) shows the frames per second in the upper right corner. `Game_state.c` the variables
of `Game_vars.h` and `Game_CleanState`, `Game_log.c` the log file of a Windows program without
a console and the report of a crash. A game with its own window has its own `game/display/pc.c`.

A Win32 game (`runtime/win32/winapi/`) has its `main` in `Game_winmain.c` and its settings file in
`runtime/core/Game_config.c` (`--config-file=`, else `<program>.cfg` in the game directory; `Sound`,
`Music` and the keys of the display, audio and input; `Display_Additional_Resolutions=off|on`, a mod, DirectDraw's list of
display modes with 1024x768, 1152x864, 1280x960, 1280x1024 and 1600x1200 after the original's 640x480 and 800x600; with Direct3D also `3D=direct3d|software|off` and
`Display_Antialiasing=off|on|2|4|8`, the multisampling of the scene on OpenGL, `on` 4 samples, the
game's own `D3DRENDERSTATE_ANTIALIAS` 4 when the key is off; with Glide `Display_3D_Resolution=WxH|off`,
the 3D at that resolution (the picture is 4:3 either way), `<ENVP>_3D_RESOLUTION` over it, and
`Display_Antialiasing` too, the multisampling of the 3D, off unless set; an item of the port's panel
in both, off/2/4/8, seen at the next frame). In `Game_hooks.h`, all optional:

| Macro | Meaning |
|---|---|
| `GAME_CONFIG_FILENAME`, `GAME_LOG_FILE` | another name for the settings file or the log (default `<program>.cfg`, `<program>.log`) |
| `GAME_WIN_START()` | called once the winapi layer is up (a registry the game reads) |
| `GAME_WINAPI_CONFIG_FILE` | the file `ReadConfiguration` reads (default `<program>.cfg`) |
| `GAME_WINAPI_KEY_MESSAGES` | 0: the message pump does not deliver key events (default 1) |
| `GAME_WINAPI_START_DIRECTORY` | the directory the game starts in, `"D:\\"` for the disc (default the process's own) |
| `GAME_CD_VOLUME_LABEL` | the volume label of a copy of the disc in a directory (an image gives its own; default empty) |
| `GAME_CD_VOLUME_SERIAL` | the serial number of the disc's volume (default `0x0000CD01`) |
| `GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption)` | a button to answer a message box with without showing it (default 0) |
| `GAME_WINAPI_PAINT_WHEN_IDLE` | 1: the game draws in its window procedure and waits for the repaint request; the port makes the request when the program has no other message |
| `GAME_WINAPI_ACTIVATE_APP_ON_IDLE` | 1: the port sends `WM_ACTIVATEAPP` with 0 and then with 1 when the program has no other message (the game makes its picture buffer on the first and draws on the second) |
| `GAME_WINAPI_WINDOW_MESSAGES` | 1: the game's message loop looks at the window of a message: input messages carry the window's handle, `PostMessageA` delivers the `WM_USER` range |
| `GAME_WINDOW_WIDTH`, `GAME_WINDOW_HEIGHT` | both defined: `GetClientRect` and `GetWindowRect` give the game's window that size at the origin (the display mode it asks for; undefined: the stub, or the game's own function) |
| `GAME_WINAPI_GDI` | 1: the game draws into DIB sections and copies them to its window with `BitBlt`/`StretchBlt` (`WinApi-gdi.c`: device contexts, bitmaps with their pixels in the guest's memory, palettes; the window's picture, its client size, shown through OpenGL) |
| `GAME_WINAPI_WAIL32` | 1: the Miles Sound System for Win32 (`WinApi-wail32.c`, AIL 3.03, over `core/Game_ail.c`): samples from WAV images or raw PCM, played on the port's mixer, and XMIDI sequences (`soundfont.sf2` of the game directory); a game that loads `WAIL32.DLL` with `LoadLibrary` lists the functions in `GAME_EXTRA_PROCS` and its import shim |
| `GAME_DSOUND_CURSOR_BEHIND_MS` | optional: milliseconds; the play cursor of a DirectSound buffer is that far behind the mixer's, for a game that keeps its mix a fixed, long way ahead of it |
| `GAME_DSOUND_HARDWARE` | defined: DirectSound answers as a sound card's driver, not the emulation: `GetCaps` has no `DSCAPS_EMULDRIVER`, the write cursor of a buffer is the hardware's (the play cursor and what the next mix takes, not the end of what the game wrote) and `DSSCL_WRITEPRIMARY` lets the game write the primary buffer, played as it is (a driver that keeps its mix a little way past the write cursor) |
| `GAME_WINAPI_MIDI_TECHNOLOGY` | the `wTechnology` of the one MIDI output device (`midiOutGetDevCapsA`, `WinApi-midi.c`), the synthesizer of `Game_midi.c` with `soundfont.sf2`: by it a game picks its arrangements; default `MOD_SWSYNTH` (7), as Windows' software synthesizer; 2 `MOD_SYNTH`, 4 `MOD_FMSYNTH`, 6 `MOD_WAVETABLE` |
| `GAME_WINAPI_TIMER_EVENTS` | 1: the callback of `timeSetEvent` runs, from the game's message pump (default 0: it never runs) |
| `GAME_GUEST_PUMP` | 1: the thread that draws pumps SDL's events (a Win32 game's own thread, a DOS game's flip thread), as on Android (the default there; elsewhere 0: the host's main thread pumps); the host's main thread always reads them |
| `GAME_EARLY_GL_WINDOW` | defined: the window of Direct3D on OpenGL comes up at the start of the program, not after the game's loading |
| `GAME_DINPUT_EXCLUSIVE_MOUSE` | defined: the game draws its cursor from DirectInput's deltas: the host pointer is hidden and held in the window from the first click in it until the focus is lost |
| `GAME_WINAPI_ADDITIONAL_RESOLUTIONS` | 1: the game picks its mode from DirectDraw's list in its menu: the port's panel has the item of `Display_Additional_Resolutions` (seen at the next start) |
| `GAME_DIRECT3D` | 1: the game uses Direct3D, so `IDirectDraw::QueryInterface` gives `IDirect3D`; the log says which renderer runs |
| `GAME_D3D_FOG_HIDDEN` | optional: an expression, non-zero while a toggle of the port hides the fog the program turns on (read at every draw; Direct3D on OpenGL) |
| `GAME_D3D_FOG_TABLE_W` | optional: an expression, the 64 bytes of a fog table as Glide's `grFogTable` (by W, 0 no fog, 255 all) or `NULL`: with a table the fog of a vertex comes from its W, not from its specular alpha, and is drawn whatever `RS_FOGENABLE` says (a driver that has such a table but looks it up by z) |
| `GAME_GLIDE_FOG_HIDDEN` | optional: an expression, non-zero while a toggle of the port hides the fog of a Glide program |
| `GAME_GLIDE_SLI` | optional: 1, `grSstQueryHardware` reports a Voodoo2 SLI pair (`sliDetect`): a program that sizes its modes by the frame buffer memory gets twice as much (a depth buffer at 1024x768) |
| `GAME_GLIDE_3D_RESOLUTION_ITEM` | optional: 1, the port's panel has the item "3D resolution" (`Display_3D_Resolution`: off, 640x480 … 1920x1440), the colour buffers made again at that size at the next `grBufferSwap` |
| `GAME_COMMAND_LINE_ARGS` | the game's own switches, always first on its command line, before the player's arguments |
| `GAME_DIRECT3D_ARG` | a switch added to the game's command line when Direct3D is on |
| `GAME_DIRECT3D_ANTIALIAS_ARG` | the game's own switch of the antialiasing (it sets `D3DRENDERSTATE_ANTIALIAS`), added to its command line when Direct3D is on and `Display_Antialiasing` of the settings file (or `<ENVP>_ANTIALIASING`) asks for it |
| `GAME_GUEST_LOW_MEMORY()` | the low memory of the guest window: how much of the guest's address space from `pointer_offset` up has to stay free of the host and of the guest's heap (default 1 MiB + 64 KiB, at most 64 MiB) |
| `GAME_GUEST_PAGE0` | defined: the guest's page at address 0 reads as zeros and takes writes, as the DOS memory Windows 9x maps there |
| `GAME_PROC_LOOKUP(name)` | `GetProcAddress` asks the game first: the guest address of the procedure, or 0 for the layer's table |
| `GAME_EXTRA_PROCS(X)` | more exports for the `GetProcAddress` table of the layer: `X(DirectInputCreateA) X(...)` |
| `GAME_MODULE_PATH_SEP` | the path separator of `GetModuleFileNameA`, a string (default `"/"`) |

The game brings `WinApi-game.h`, the prototypes of the imports of its executable, and
`WinApi-game*.c`, their implementation, which `WinApi-guest.h` (calling back into the
guest) completes. Only the event loop's thread pumps SDL's events, so the windows are made
and changed on it: `Game_RunOnMain` (`Game_thread.h`) runs a call there and waits for it,
and `Game_WindowCreate`, `Game_ShowWindow`, `Game_HideWindow`, `Game_ShowCursor`,
`Game_SetCursor`, `Game_SetRelativeMouseMode` and `Game_WarpMouse` go through it (on
Windows a window's messages reach only the thread that made it; SDL's X11 and Wayland
drivers wait for the server's answer on the connection the event loop pumps). The event
loop also reads all of SDL's queue: the port's panel takes its own, and what is the
game's goes as it came to the input queue of the window's thread (`Winapi_RouteEvent`,
`WinApi-user32.c`), where the ticks of `timeSetEvent` go too; the message pump of that
thread makes window messages of them and waits on the queue's condition variable.

The remaining `GAME_*` names of the runtime are not hooks: `GAME_D3D_GL`, `GAME_WINAPI` and
`GAME_PROGRAM_NAME` come from `scons/game.py`, `GAME_CORE_CONSUMES_INPUT`, `GAME_DOS_ENTRY` and
`GAME_ENTRY_IS_START` are derived from the platform, the others are include guards.

On Android the game's `main` calls `Game_AndroidSetup()` (`Game_android.h`)
first: it sends stdout and stderr to logcat and sets `GAME_ENV("DATA_DIR")`
and `GAME_ENV("WRITE_ROOT")` to the directories `GameActivity` made.

The game directory, the write root, the disc and the DOS handle table are
`runtime/core/Game_gamedir.c` (`Game_FilesInit`, declared in `Game_vfs.h`); the game
names its data in `Game_hooks.h` (`GAME_DATA_MARKERS`, `GAME_CD_MARKER`,
`GAME_CD_CURRENT_DRIVE` for a game that runs from the disc). The runtime of a DOS game is
`runtime/dos`: `Game_DosRuntimeInit`/`Deinit` (`Game_dosrt.c`) start and end it, and it owns the
procedures the translated code calls -- `X86_InterruptProcedure` (`Game_dosint.c`: the vectors,
the game's handlers run as hardware interrupts, INT 08h, 16h, 1Ah, 21h, 2Fh and 31h),
`X86_InPortProcedure`/`X86_OutPortProcedure` and `X86_ReadMemProcedure`/`X86_WriteMemProcedure`
(`Game_dosrt.c`: the PIT, the keyboard, the VGA DAC and retrace, the segment-override accesses).
Under it are the file functions of INT 21h (`Game_dosfile.c`), the keyboard (`Game_doskbd.c`),
MSCDEX (`Game_doscd.c`), PIT channel 0 (`Game_dospit.c`), the VGA ports (`Game_dosvga.c`) and the
Microsoft mouse driver of INT 33h with AH = 0 (`Game_dosmouse.c`: the host mouse in the 4:3 picture,
polled, or the button events and the motion handed to the game's event handler of function 0Ch
as an interrupt of the mouse; SR finds no such handler, `fixup_interpret_as_code.sci` lists it;
the game draws its pointer, so the host's is hidden over the window once it uses the driver).
The game's own services (INT, a memory dump) and ports come through hooks
(`GAME_DOS_INTERRUPT`, `GAME_DUMP_GUEST_MEMORY`, `GAME_DOS_PORT_IN`/`GAME_DOS_PORT_OUT`
in `Game_hooks.h`). It brings its sound, its llasm glue (with the handler every vector starts
with) and whatever else of the files above it replaces.

`<GAME>_AUTO_KEYS="seconds:Key[~hold_ms],..."` (or `@fifo`) presses keys for unattended runs
(`runtime/core/Game_keys.c`, DOS and DirectInput alike; `run-sdl.sh --keys=`); a game may set
the default hold with `GAME_AUTO_KEY_HOLD_MS`.

## Layout

```
SR/                     submodule: the recompiler, llasm, llasm-support
llasm/                  llasm.py, the Python port of SR's llasm, and its tests
scons/game.py           the whole build of one game, as one SCons graph
scons/glibc_max.py      the glibc versions of a Linux binary (glibc_max)
tools/compact_source.py compacts the data segments of SR's output
tools/android_sdl.sh    SDL2 and SDL2_mixer for Android
tools/android_apk.py    the Android APK of a game
tools/image/            disc images and installers: image.py and its format modules
tools/distrib_cd.py    a distrib copy of a game's disc (game/distrib.cfg)
tools/floppy.py         a game's floppy images and its installer's archives on them (floppy=)
tools/sf2_subset.py     a SoundFont cut to some presets: the MIDI bank of a game (soundfont=)
tools/font_cyrillic.py  the Cyrillic letters of the OSD font (runtime/core/Game_font_cyr.h)
tools/gen_import_shim.py the llasm bridges of a Win32 port's imports (game/import_args.tsv)
tools/run_common.sh     the common part of a game's run-sdl.sh / run-sdl-wine.sh
tools/analysis/         tools for the work on a game's SR input (LE/PE, coverage)
tools/regress/          regression checks: SR's output, builds, smoke tests, llasm hashes
android/                what the APK adds to SDL's android-project
runtime/core/           threads, memory, frontends, the OSD and its font, printf,
                        Android start-up, and what the DOS and the Win32 ports
                        share: WATCOM C library and file I/O (Game_clib, Game_vfs,
                        Game_file), the game directory (Game_gamedir), CD image
                        (Game_cdrom), CD audio (Game_music), the Miles Sound
                        System (Game_ail), PC scancodes and AUTO_KEYS (Game_keys),
                        the gamepads (Game_pad)
runtime/dos/            the runtime of a DOS4GW game: start and end, ports and segment
                        accesses (Game_dosrt), interrupts (Game_dosint), registers
                        (Game_dosregs.h), DOS memory (Game_dosmem), INT 21h file functions
                        (Game_dosfile), the keyboard (Game_doskbd), MSCDEX (Game_doscd),
                        the PIT (Game_dospit), the VGA ports (Game_dosvga), the
                        Miles AIL a game was linked with (Game_dosail)
runtime/dos/llasm/      shared llasm includes of the DOS ports (ail.llinc: AIL's entry points)
runtime/win32/          what every port of a Windows original needs: platform.h, X86_FS_mem.c,
                        llasm/ (asm-cpu.c, Game-asm.c); glide/ (Glide 2 on OpenGL), gl/ (Direct3D on OpenGL)
runtime/win32/winapi/   the Win32 API layer: kernel32, user32, gdi32, DirectDraw, Direct3D,
                        DirectInput, DirectSound, winmm, MCI, avifile, msvfw, the registry and the
                        game's config file; WinApi-runtime (start-up, CD audio),
                        Game_RuntimeGlue; winapi/llasm/ is its llasm glue
skeleton/               a starting point for a new game repository -- to come
```

`runtime/core/` and `runtime/dos/`: the layers of a DOS game (also used by Win32 ports):

- `Game_clib.c`: the WATCOM C library functions a game's
  `external_procedures.sci` routes to the host (kbhit and getch: `Game_doskbd.c`);
- `Game_vfs.c`, `.h`: WATCOM stdio over DOS handles, with a write root that
  can overlay the game directory;
- `Game_file.c`, `.h`: DOS files as host files or as files of the CD image;
- `Game_gamedir.c`: the game directory (C:), the write root, the disc (D:), the DOS
  handle table;
- `Game_cdrom.c`, `.h`: drive D: from a cue sheet (ISO9660 data track,
  CDDA or ogg/flac/wav audio tracks) or from a copy of the disc;
- `Game_music.c`, `.h`: MSCDEX audio playback through SDL_mixer;
- `Game_ail.c`, `.h`: the Miles Sound System (AIL 3.x) without its entry points: digital
  samples on reserved channels of the SDL_mixer device, XMIDI sequences through
  `Game_midi`; `$<GAME>_TRACE_SOUND` logs what it plays;
- `Game_dosrt.c`, `.h`: start and end of a DOS game's runtime, the I/O ports, the
  segment-override accesses (PSP, the last DOS block, a DOS address);
- `Game_dosint.c`, `.h`: the interrupt vectors, the game's handlers run as hardware
  interrupts, AIL timer callbacks and a driver's far callbacks with their registers (the
  mouse's event handler: `RunProcStack` of `llasm/Game-llasm.llasm`, which takes the target from
  the stack, so ECX is an argument), the software interrupts every DOS game here shares
  (INT 21h and DPMI as far as a flat game asks them), after the game's `Game_DosInterrupt`;
- `Game_dosregs.h`: the registers of an interrupt and their 8- and 16-bit parts;
- `Game_dosvga.c`, `.h`: the VGA DAC (3C7h-3C9h), Input Status #1 (3DAh), the Map Mask and the
  graphics controller, and mode 12h (640x480, 16 colours, four planes: the VGA window holds the
  plane the Map Mask addresses, the picture is made of the planes through the attribute palette),
  and the VESA BIOS of `GAME_DOS_VESA` (VBE 1.2: modes 100h, 101h, 103h, 105h in 256 colours,
  a frame of 2 MB that the window at A0000h shows 64 KB of, the bank of 4F05h, the line of 4F06h
  and the display start of 4F07h);
- `Game_dosmem.c`, `.h`: DOS conventional memory of a DOS4GW game (DPMI blocks, real-mode
  buffers, the PSP) at its own addresses in the low memory of the guest window (zeros below
  the PSP at 10000h, the VGA window at A0000h), and the memory block of a
  program started at its entry point (`Game_DosProgramInit`: stack and heap above the image,
  grown by INT 21h AH=4Ah or EAX=80004800h); the blocks of DPMI 0100h are taken first fit
  and given back (0101h), resized in place (0102h);
- `Game_dosexec.c`, `.h`: INT 21h AH=4Bh, the other programs of the game (`exe_modules=` of
  `game.cfg`, modules of the same binary): run on the game's CPU with a PSP and a program block
  of their own, also behind `COMMAND.COM /C`; their AH=4Ch comes back, AH=4Dh gives the code;
- `Game_dosfile.c`, `.h`: the file functions of INT 21h (open, create, read, write, seek,
  close, FindFirst/FindNext, the DTA, the devices CON and NUL);
- `Game_doskbd.c`, `.h`: the keyboard: the controller's bytes (port 60h, E0 before a grey
  key) delivered to the game's INT 9 handler, each key held at least `GAME_DOS_KBD_MIN_HOLD_MS`
  (a tap again while its press still waits is left out: quick taps do not pile up), the BIOS
  buffer behind INT 16h, INT 21h 08h/0Bh and WATCOM's kbhit/getch;
- `Game_doscd.c`, `.h`: MSCDEX on drive D:: the install and drive checks, the table of
  contents and the audio status (IOCTL input), PLAY/STOP/RESUME AUDIO through `Game_music.c`;
- `Game_dospit.c`, `.h`: PIT channel 0 on the host clock: the reload value and the counter
  of ports 40h/43h, when IRQ 0 is due (`Game_DosPitRun`), the BIOS ticks;
- `Game_dosail.c`, `.h`: the Miles AIL 3.x a game was linked with, over `Game_ail.c`: its
  timer service on the PIT (`Game_AIL_TimerInterrupt` from the game's `GAME_DOS_TIMER_TICK()`),
  the digital driver (`$<GAME>_NOSOUND`) and its samples; the game's `external_procedures.sci`
  routes AIL's entry points to the `SR_AIL_*` of `llasm/ail.llinc`, which its own llasm includes;
- `Game_dosail2.c`, `.h`: the drivers of the Miles AIL/32 2.x a game was linked with, by the
  numbers of the driver functions: a digital one (one .VOC at a time, over `Game_ail.c`) and
  an XMIDI one: the Ad Lib driver of AIL 2.14 (`Game_dosail2fm.c`, `.h`: its interpreter
  and its synthesizer ported to C, on the OPL2 of `core/Game_opl.c`, with the timbres the
  game loads from its `.AD` file), one at a time; the game's `external_procedures.sci`
  routes its `AIL_register_driver` and the dispatcher of the driver functions to
  `SR_AIL2_register_driver` and `SR_AIL2_call_driver` (`llasm/ail2.llinc`, in every DOS game's llasm);
- `Game_dosdsmi.c`: DSMI's MCP on the host (`GAME_DOS_DSMI`): its voices mixed into a stream of
  the port, its time the stream's, for a game whose MCP mixes through a based ES selector;
- `Game_dossb.c`, `.h`: the Sound Blaster 2.0 of `GAME_DOS_SOUNDBLASTER` (the DSP, the 8-bit
  DMA, the IRQ at the end of a block, the mixer's volume), with the FM chip of `GAME_DOS_SB_OPL`;
- `Game_dosjoy.c`, `.h`: the game port (201h) and the BIOS joystick (INT 15h AH=84h) on the host's
  first gamepad (`Game_pad.c`): the left stick and the D-pad are joystick A, the right stick B, the
  buttons A, B, X, Y the four buttons; without a pad, a port with nothing plugged in;
- `Game_dosreal.c`, `.h`: real-mode code in the DOS memory (`GAME_DOS_REALMODE`), on an
  interpreter of the 8086 and the 80186 of its own: a driver the game loads into a DOS block
  and calls through a real-mode vector (DPMI 0201h, an INT the protected-mode code does not
  handle, DPMI 0300h), its handler of the card's IRQ
  (INT 21h AH=25h of the driver) reached when the game's protected-mode handler passes the IRQ
  on (`Game_DosHardwareInterrupt` of `Game_dosint.c`), and the ports of the translated code;
- `Game_keys.c`, `.h`: the PC scancode of an SDL key, `<GAME>_AUTO_KEYS`;
- `Game_pad.c`, `.h`: the host's gamepads (`SDL_GameController`, up to four, plugged in and out
  while the game runs) with the state of XInput: its buttons, two sticks, two triggers, with or
  without XInput's dead zones, and the motors; for the joystick APIs of the games. No pad, or
  `Gamepad=off` in the settings file: nothing is opened. `$<GAME>_DEBUG_PAD` logs the pads and
  every change of their state;
- `touchgamepad.c`, `.h`: the on-screen buttons of the Android build, drawn over the game and
  pressing keys; a game's `game/Game_touchlayout.h` places them (`touchlayout_default.h` otherwise),
  and may define `GP_SECOND_KEY(index)`, a second key a button holds with its own (a chord: Ctrl+Alt of one thumb).
  A button with the key `TOUCHGAMEPAD_KEYBOARD` opens a translucent PC keyboard (F1-F12, Esc,
  Shift, Ctrl, Alt and the arrows; EN/RU changes only its labels, the game makes the keys
  letters) in place of the buttons: its HIDE key, or a touch above it, closes it, and while it is
  open no touch is the game's mouse. A game without such a button has no keyboard. While a
  button with the key `TOUCHGAMEPAD_MOUSE_RIGHT` is held, a touch on the picture is the right
  button of the DOS mouse where the finger is, until it is lifted;
- `Game_gyro.c`, `.h`: the host's gyroscope (a phone, a tablet) as the pointer, the mod
  `Gyro_Mouse=on` with `Gyro_Mouse_Speed=1..9` (5: the picture's width in a turn of 38 degrees);
  opened on the thread that pumps the events, only while the mod is on, and closed there before
  that thread ends (`Game_GyroQuit`: on Android its events come through that thread's looper).
  The INT 33h driver of `runtime/dos` then keeps its own position, which turning the device moves; a touch away from the
  on-screen buttons (the pointer of `touchgamepad.c`, not SDL's mouse of the first finger) puts
  it under the finger and presses the button, and the turns go on from there. An on-screen button
  with the key `TOUCHGAMEPAD_MOUSE_LEFT` is the mouse's left button; a layout may give it to a
  button while the mod is on (`Game_GyroMod()`; a switch of the mod places the buttons again). A game shows the two items in its panel with `Game_GyroAddItems()` from
  `GAME_OSD_ITEMS()`;
- `Game_audio.c`, `Game_sound.h`: the SDL_mixer device (`Game_AudioInit`) shared by CD audio, MIDI and a game's digital sound, DOS and Win32.
- `Game_opl.c/.h`: an OPL2 (emu8950, `extern/emu8950`, built as C by `Game_opl_impl.c` and `Game_opl_adpcm_impl.c`) on SDL_mixer's music, with the service routine of the FM driver that writes it called on the audio thread at the driver's rate (AIL's 120 Hz), so the music keeps its time whatever the game's frame rate is;
- `Game_audiostream.c/.h`: a stream of PCM (`Game_AudioSourceOpen`, SDL_OpenAudioDevice's calls: a fill callback or a queue) on a device of its own or in SDL_mixer's output, whose one post-mix hook it owns; the Sound Blaster, DirectSound and a game's own driver play through it.

The `Game_dos*` files are in `runtime/dos/`; the others are in `runtime/core/`, and a game that
has no use for them leaves them out with `runtime_skip`.

## License

MIT, see [`LICENSE`](LICENSE). SR-SDK contains files taken from SR and
derived from it; they keep Roman Pauer's copyright, and files changed here add
their own line. [`NOTICE.md`](NOTICE.md) names the third-party code (SR,
TinySoundFont, printf, blast, SDL's Android project) with its license texts.

Thanks to Roman Pauer for SR and its ports, on which all of this stands.
