# SR-SDK

A build kit for native ports of DOS games, made with
[SR](https://github.com/M-HT/SR), the static recompiler by Roman Pauer.

SR turns a game's original executable into source code: x86, ARM or x64
assembler, or *llasm*, an IR that LLVM compiles for any target. A playable port
also needs a runtime around that code: the DOS, DPMI, video, sound, input and
C library services the game used to call. Each of SR's own ports carries a full
copy of that runtime. SR-SDK keeps the shared part in one place, together with
the build, so a game repository holds only what is specific to its game.

```
game repository
├── game/            the game's runtime, hooks and SR inputs (game.cfg, recomp/)
├── SConstruct       one line: SConscript('SR-SDK/scons/game.py')
└── SR-SDK/          this repository, a submodule
    └── SR/          SR itself, a submodule
```

SR is used as it is: the submodule `SR/` is the fork
[mxvisor/SR](https://github.com/mxvisor/SR), branch `master`, which is upstream
plus fixes that can be offered back to it. SR-SDK does not change SR.

## Requirements

- Python 3 and [SCons](https://scons.org/)
- a C/C++ compiler: GCC or Clang for Linux, MinGW-w64 for Windows (it also
  cross-builds from Linux)
- LLVM's `opt` and `llc`, for the llasm backend
- SDL2 and SDL2_mixer
- for Android: the Android SDK with an NDK and a JDK, CMake, Ninja and curl
  (SDL itself is downloaded and built by `tools/android_sdl.sh`)

No D compiler is needed: llasm IR is translated by `llasm/llasm.py`, a Python
version of SR's `llasm` that is developed here; on SR's reference games its output is byte-identical to the D original (see
[`llasm/README.md`](llasm/README.md)). To find a procedure of the original in a debugger, delete
`build/obj/<device>/gen/*.o` and build once with `LLASM_SYMBOLS=1`: every procedure keeps its name
(`loc_XXXX`, the address in the original) in the object, for `nm` and `gdb`.

## Starting a new port

`tools/new_game.sh DIR NAME dos|win32 [--exe PATH|disc:NAME] [--image IMAGE]` makes the skeleton of a game
repository: `SConstruct`, `Makefile`, `.gitignore`, `game/game.cfg`,
`game/Game_hooks.h`, `game/recomp/Game-main/default/` (`sha256`, `SR.cfg`), the
launchers (`run-sdl.sh`, `run_sdl_wine.sh`, `run-orig-dosbox.sh` or
`run-orig-wine.sh`), a git repository and SR-SDK (with SR) as a submodule. With
`--exe` the original's sha256 is written, and `scons stage=input` then passes. With
`--image` the disc is described in `game/portable.cfg`, and `--exe disc:NAME` takes the
executable from the disc (`build/disc/`, unpacked by `tools/disc_tree.py`) instead of a
file unpacked by hand; the launchers read the data from `build/disc`.
For a DOS program `tools/sr_run.py` runs SR twice when it must: SR's `--list_handlers=FILE`
lists the routines the program gives to INT 21h AH=25h (EDX loaded with an address, directly or
through a wrapper routine that takes it from its stack argument), and the second run takes them as
code, so the game's `fixup_interpret_as_code.sci` is for what this pattern misses. A DOS4GW program
also gets `esp_dword_aligned`, `ebp_dword_aligned` and `code_writable` in its `SR.cfg` from
`scons/game.py`.
Next come `main.c`, SR's inputs (`SR.cfg`, `*.sci`) and the game's hooks. The
`TODO` lines of the launchers name what to fill in.

## Running the original

`run-orig-dosbox.sh` (DOS games) and `run-orig-wine.sh` (Windows games) of a game
repository run the original as the reference for the port, on the layout of the
disc: DOSBox mounts the game as `C:` with an overlay and the cue sheet made by
`tools/image/image.py` as `D:`; Wine runs on a `fuse-overlayfs` mount of the game
(a copy when it is missing), and `D:` is the unpacked disc. What the original
writes lands in `<outer dir>/run/orig-*`. `--headless[=N]` takes screenshots under
Xvfb (`build/shots/orig-*`), `--keys=` sends keys. The common part is
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
variants=default        # recognized versions of the original, by sha256
input_exe=../path/to/GAME.EXE
link=separate
devices=pc64-linux,pc64-windows
winapi=yes              # win32 only: the Win32 API layer, runtime/win32/winapi (default yes)
opengl=no               # the shared OpenGL module, runtime/core/gl (glad, context, shaders, FBOs)
glide=no                # win32 only: Glide 2.x on OpenGL, runtime/win32/glide (implies opengl)
d3d_gl=no               # win32 only: Direct3D on OpenGL, runtime/win32/gl (implies opengl)
runtime_skip=Game_cdrom.c,Game_file.c   # runtime .c files the game does not build
program=game
dist_name=game                     # the name of the release archives (default program)
android_id=io.github.user.game   # for Android: the application id
android_name=Game                 # and the launcher's label
```

`game/recomp/<module>/<variant>/` holds SR's inputs for the original: its
`sha256`, `SR.cfg` and the `*.sci` files. Then:

```sh
scons                            # build/bin/<device>/<program>
scons device=pc64-windows        # Windows, cross-built with MinGW-w64
scons stage=gen                  # stop after SR's output
scons input_exe=/path/to/GAME.EXE
```

The game's `Makefile` is two lines, `include SR-SDK/scons/game.mk`: the targets are scons's
(`scons/game.py` and `scons/release.SConscript`), the `.mk` only chains them.
`make linux`, `make windows` (the MinGW build with the SDL DLLs beside the exe),
`make portable-data` (`build/dist/portable`: the disc's files and music, from `game/portable.cfg`),
`make portable` (both builds, the data, and `build/dist/<name>-{linux,windows}-x64.{tar.gz,zip}`:
the program, `game/release/<os>/` and its `x64/`, and the data), `make apk`, `make disc`
(`build/disc`), `make host|input|recomp|gen|obj` (one stage), `make clean`, `make test`.
The same names work as `scons <name>`; `MINGW_BIN=` points at the DLLs.

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
with `android/app.gradle` and `android/GameActivity.java` over it, and the
game's `game/android/res/` (its icons) over that. On the first launch, and
after each update of the APK, `GameActivity` unpacks the data from the APK's
assets: the port reads its files with `fopen`.

`tools/portable_cd.py OUT_DIR`, from the game repository's root, makes the
game's data directory the port runs from: the files of the disc's data track
without its installer, the files the installer would have written, and the
audio tracks as flac, wav or ogg files with a cue sheet. What to skip and what
to add is in `game/portable.cfg`; the script's header lists its keys.

`tools/image/image.py` turns a disc image into what the rest of the SDK takes as
the disc, a cue sheet: `image.py disc IMAGE [-o DIR] [--iso] [--extract DIR]`
takes a Nero `.nrg` (a cue + bin pair is written, with the audio tracks), an
Alcohol 120% `.mds`/`.mdf` (a cue sheet that points at the `.mdf`), a raw `.bin`
or an `.iso`. `--iso` also writes the data track as an ISO, `--extract` copies
its files. It writes only the files it names, and deletes nothing.
`image.py installer FILE.z OUT_DIR` unpacks an InstallShield 3 installer. The
format modules (`nrg.py`, `mds.py`, `rawbin.py`, `installshield.py`) share
`cdimage.py` with `portable_cd.py`.

The stages are `host` (SR, built from a copy in `build/`), `input` (the
original, checked against its sha256), `recomp` (SR), `gen` (SR's output,
compacted), `obj` and `bin`. All output goes to `build/` of the game
repository, so the checkouts of SR-SDK and SR stay clean.
`scons/game.py` never names a game.

## Checking a change

`tools/regress/run_all.sh` answers "did I break anything?" after a change to SR,
to `llasm.py` or to the SDK: SR's output for SR's reference games, `make linux`
and `make windows` of every game, a start-and-close smoke test of every port under
Linux and Wine, runs at different load addresses (ASLR) the hashes of the llasm output and the conversion of the disc images, all compared with a saved
reference run. See [`tools/regress/README.md`](tools/regress/README.md).

## What the SDK builds

Every build is position-independent (Linux and Android: PIE / `-fPIC`, llasm
`-pic -ptrofs`; Windows: dynamicbase, high-entropy VA, a `.reloc` section). Guest
pointers are 32-bit offsets from a load-time base, never absolute host addresses,
and there is no fixed-address mode.

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
`scaler-plugins/` and `llasm-support/`. A runtime `.c` finds the runtime's own
headers first, so a header the game overrides affects only the game's code.

## What a game provides

`game/Game_hooks.h`:

| Macro | Meaning |
|---|---|
| `GAME_MAIN_FILENAME` | the original's file name, as `argv[0]` of its `main` |
| `GAME_NAME` | the game's name in the port's messages |
| `GAME_ENV(name)` | the port's environment variables: `"XX_" name` |
| `GAME_CD_MARKER` | a directory on the CD that tells a copy of the disc from an installed game (without it, only a disc image is found) |
| `GAME_DATA_MARKERS` | file names, one of which the game directory holds (`"A.DAT", "B.BAT"`); default: `GAME_MAIN_FILENAME` |
| `GAME_DATA_SUBDIR` | Win32 ports, optional: the directory of a copy of the disc that is the game directory (`"DATA"`), so `<PREFIX>_DATA_DIR` may name the disc |
| `GAME_FLIP_HOOK()` | optional: run before each frame is shown |

A DOS game (`runtime/dos`) also takes, all optional:

| Macro | Meaning |
|---|---|
| `GAME_ENTRY` | the llasm symbol the game starts at (`Game-asm.c`): default `c_start_`, the program's entry point, which SR exports by itself; `c_main_` is the game's C `main` named by `main_` in `global_aliases.sci`, for a port that skips the startup code |
| `GAME_PROGRAM_MEMORY`, `GAME_PROGRAM_STACK` | `Game-asm.c`, a game started at its entry point: the size of the program's memory block (default 64 MiB, less if the guest window has no room) and of the stack in it (1 MiB); the startup code grows its heap in the block with INT 21h AH=4Ah |
| `GAME_ARGS(argc, argv)` | `Game_dosmain.c`: takes the port's own options out of `argv`, returns the new `argc` |
| `GAME_CONFIG()` | the port's configuration, read after the options |
| `GAME_RUNTIME_INIT()` / `GAME_RUNTIME_DEINIT()` | the game's runtime, started after the guest allocator (nonzero: failed). Without them `Game_dosmain.c` makes the DOS world itself: conventional memory at its own addresses (PSP and command line at 0, the VGA window at A0000h), the disc and the music |
| `GAME_DOS_ENV` | without `GAME_RUNTIME_INIT`: the variables of the DOS environment, `"NAME=VALUE"` strings separated by commas; the guest address of the array of their addresses is `Game_DosEnviron` (a DOS4GW game's `getenv` reads its own copy, which the port has not got: `instruction_replacements.sci` loads `Game_DosEnviron` instead) |
| `GAME_DOS_SOUNDBLASTER` | the Sound Blaster 2.0 of `Game_dossb.c` (DSP, 8-bit DMA, the IRQ from `SR_CheckTimer`), with `BLASTER=A220 I5 D1 T1` in the environment; `GAME_SB_BASE`, `GAME_SB_IRQ`, `GAME_SB_DMA` change the card. The IRQ handler the game installs has to be translated code: SR finds a handler given to INT 21h AH=25h by itself (see below) |
| `GAME_LOG_FILE` | Windows: the name of the log file of a program without a console (default `"game.log"`) |
| `GAME_MIXER_CHANNELS` | `Game_dosaudio.c`: SDL_mixer channels (default 16) |
| `GAME_DOS_INTERRUPT(number, regs)` | the game's own INT services, tried first (nonzero: served) |
| `GAME_DOS_REALMODE_INTERRUPT(vector, regs, es)` | DPMI 0300h (a real-mode interrupt) for a vector the shared services do not know, on the copy of the call structure; `es` is the structure's ES (nonzero: served; otherwise INT 10h, 1Ah, 21h and 2Fh go to the shared services and every other vector fails with CF set) |
| `GAME_DOS_VIDEO_WINDOW(address, size)` | the host copy of a guest access to the VGA window at A0000h, NULL if it is not in it |
| `GAME_DUMP_GUEST_MEMORY()` | called when the game opens its error log: a debug dump |

`Game_dosmain.c` is the `main` of a DOS game's port, `Game_dosaudio.c` the SDL_mixer device, `Game_dostimer.c`
`SR_CheckTimer`; a game with its own version of one lists it in `runtime_skip` and brings the
functions the file's head names.

`runtime/core/display/pc.c` is the window of every port (mode-13h palette, whole-pixel scaling,
4:3; on Win32 the window stays hidden, the game draws in its own), `Game_state.c` the variables
of `Game_vars.h` and `Game_CleanState`, `Game_log.c` the log file of a Windows program without
a console and the report of a crash. A game with its own window has its own `game/display/pc.c`.

A Win32 game (`runtime/win32/winapi/`) has its `main` in `Game_winmain.c` and its settings file in
`Game_config.c` (`--config-file=` or `GAME_CONFIG_FILENAME` in the game directory; `Sound`,
`Music` and the keys of the display, audio and input). It says in `Game_hooks.h`:
`GAME_CONFIG_FILENAME` (required), `GAME_LOG_FILE` and the optional `GAME_WIN_START()`, called
once the winapi layer is up (a registry the game reads). The layer also takes, all optional:
`GAME_WINAPI_KEY_MESSAGES` (0: the message pump does not deliver key events; default 1),
`GAME_WINAPI_START_DIRECTORY` (the directory the game starts in, `"D:\\"` for the disc;
default the process's own), `GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption)` (a button to
answer a message box with without showing it; default 0). The game brings
`WinApi-game.h`, the prototypes of the imports of its executable, and
`WinApi-game*.c`, their implementation, which `WinApi-guest.h` (calling back into the
guest) completes.

On Android the game's `main` calls `Game_AndroidSetup()` (`Game_android.h`)
first: it sends stdout and stderr to logcat and sets `GAME_ENV("DATA_DIR")`
and `GAME_ENV("WRITE_ROOT")` to the directories `GameActivity` made.

The game directory, the write root, the disc and the DOS handle table are
`runtime/core/Game_gamedir.c` (`Game_FilesInit`, declared in `Game_vfs.h`); the game
names its data in `Game_hooks.h` (`GAME_DATA_MARKERS`, `GAME_DATA_SUBDIR`,
`GAME_CD_CURRENT_DRIVE` for a game that runs from the disc). The runtime of a DOS game is
`runtime/dos`: `Game_DosRuntimeInit`/`Deinit` (`Game_dosrt.c`) start and end it, and it owns the
procedures the translated code calls -- `X86_InterruptProcedure` (`Game_dosint.c`: the vectors,
the game's handlers run as hardware interrupts, INT 08h, 16h, 1Ah, 21h, 2Fh and 31h),
`X86_InPortProcedure`/`X86_OutPortProcedure` and `X86_ReadMemProcedure`/`X86_WriteMemProcedure`
(`Game_dosrt.c`: the PIT, the keyboard, the VGA DAC and retrace, the segment-override accesses).
Under it are the file functions of INT 21h (`Game_dosfile.c`), the keyboard (`Game_doskbd.c`),
MSCDEX (`Game_doscd.c`), PIT channel 0 (`Game_dospit.c`), the VGA ports (`Game_dosvga.c`) and the
Microsoft mouse driver of INT 33h with AH = 0 (`Game_dosmouse.c`: the host mouse in the 4:3 picture).
The game's own services (INT, the A0000h window, a memory dump) and ports come through hooks
(`GAME_DOS_INTERRUPT`, `GAME_DOS_VIDEO_WINDOW`, `GAME_DUMP_GUEST_MEMORY`, `GAME_DOS_PORT_IN`/`GAME_DOS_PORT_OUT`
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
tools/compact_source.py compacts the data segments of SR's output
tools/android_sdl.sh    SDL2 and SDL2_mixer for Android
tools/android_apk.py    the Android APK of a game
tools/image/            disc images and installers: image.py and its format modules
tools/portable_cd.py    a portable copy of a game's disc (game/portable.cfg)
tools/gen_import_shim.py the llasm bridges of a Win32 port's imports (tools/import_args.tsv)
tools/run_common.sh     the common part of a game's run-sdl.sh / run_sdl_wine.sh
tools/analysis/         tools for the work on a game's SR input (LE/PE, coverage)
tools/regress/          regression checks: SR's output, builds, smoke tests, llasm hashes
android/                what the APK adds to SDL's android-project
runtime/core/           threads, memory, frontends, virtual keyboard, printf,
                        Android start-up, and what the DOS and the Win32 ports
                        share: WATCOM C library and file I/O (Game_clib, Game_vfs,
                        Game_file), the game directory (Game_gamedir), CD image
                        (Game_cdrom), CD audio (Game_music), PC scancodes and
                        AUTO_KEYS (Game_keys)
runtime/dos/            the runtime of a DOS4GW game: start and end, ports and segment
                        accesses (Game_dosrt), interrupts (Game_dosint), registers
                        (Game_dosregs.h), DOS memory (Game_dosmem), INT 21h file functions
                        (Game_dosfile), the keyboard (Game_doskbd), MSCDEX (Game_doscd),
                        the PIT (Game_dospit), the VGA ports (Game_dosvga)
runtime/dos/llasm/      shared llasm includes of the DOS ports
runtime/win32/          what every port of a Windows original needs: platform.h, X86_FS_mem.c,
                        llasm/ (asm-cpu.c, Game-asm.c)
runtime/win32/winapi/   the Win32 API layer: kernel32, user32, gdi32, DirectDraw, DirectInput,
                        DirectSound, winmm, MCI, avifile, msvfw, the registry and the
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
- `Game_dosrt.c`, `.h`: start and end of a DOS game's runtime, the I/O ports, the
  segment-override accesses (PSP, the last DOS block, the game's A0000h window);
- `Game_dosint.c`, `.h`: the interrupt vectors, the game's handlers run as hardware
  interrupts and AIL timer callbacks, the software interrupts every DOS game here shares
  (INT 21h and DPMI as far as a flat game asks them), after the game's `Game_DosInterrupt`;
- `Game_dosregs.h`: the registers of an interrupt and their 8- and 16-bit parts;
- `Game_dosvga.c`, `.h`: the VGA DAC (3C7h-3C9h) and Input Status #1 (3DAh);
- `Game_dosmem.c`, `.h`: DOS conventional memory of a DOS4GW game (DPMI blocks, real-mode
  buffers, the PSP) in one pool, at its own addresses when the guest's low megabyte is free
  (`Game_DosMapLow`: the PSP at 0, the VGA window at A0000h), and the memory block of a
  program started at its entry point (`Game_DosProgramInit`: stack and heap above the image,
  grown by INT 21h AH=4Ah);
- `Game_dosfile.c`, `.h`: the file functions of INT 21h (open, create, read, write, seek,
  close, FindFirst/FindNext, the DTA, the devices CON and NUL);
- `Game_doskbd.c`, `.h`: the keyboard: the controller's bytes (port 60h, E0 before a grey
  key) delivered to the game's INT 9 handler, the BIOS buffer behind INT 16h, INT 21h
  08h/0Bh and WATCOM's kbhit/getch;
- `Game_doscd.c`, `.h`: MSCDEX on drive D:: the install and drive checks, the table of
  contents and the audio status (IOCTL input), PLAY/STOP/RESUME AUDIO through `Game_music.c`;
- `Game_dospit.c`, `.h`: PIT channel 0 on the host clock: the reload value and the counter
  of ports 40h/43h, when IRQ 0 is due (`Game_DosPitRun`), the BIOS ticks;
- `Game_keys.c`, `.h`: the PC scancode of an SDL key, `<GAME>_AUTO_KEYS`;
- `Game_sound.h`: the interface of a game's Miles AIL replacement.

The `Game_dos*` files are in `runtime/dos/`; the others are in `runtime/core/`, and a game that
has no use for them leaves them out with `runtime_skip`.

## License

MIT, see [`LICENSE`](LICENSE). SR-SDK contains files taken from SR and
derived from it; they keep Roman Pauer's copyright, and files changed here add
their own line. [`NOTICE.md`](NOTICE.md) lists where each file comes from and
gives SR's license text, and that of `printf` by Marco Paland, which SR
includes.

Thanks to Roman Pauer for SR and its ports, on which all of this stands.
