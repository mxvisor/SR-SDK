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
port of SR's `llasm`, which gives byte-identical output (see
[`llasm/README.md`](llasm/README.md)).

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
program=game
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

The stages are `host` (SR, built from a copy in `build/`), `input` (the
original, checked against its sha256), `recomp` (SR), `gen` (SR's output,
compacted), `obj` and `bin`. All output goes to `build/` of the game
repository, so the checkouts of SR-SDK and SR stay clean.
`scons/game.py` never names a game.

## Checking a change

`tools/regress/run_all.sh` answers "did I break anything?" after a change to SR,
to `llasm.py` or to the SDK: SR's output for SR's reference games, `make linux`
and `make windows` of every game, a start-and-close smoke test of every port under
Linux and Wine, runs at different load addresses (ASLR) and the hashes of the llasm output, all compared with a saved
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
  directory's own `SConscript` when it has one. `game/llasm/SConscript` is
  required.

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
| `GAME_CD_MARKER` | a directory on the CD that tells a copy of the disc from an installed game |
| `GAME_FLIP_HOOK()` | optional: run before each frame is shown |

On Android the game's `main` calls `Game_AndroidSetup()` (`Game_android.h`)
first: it sends stdout and stderr to logcat and sets `GAME_ENV("DATA_DIR")`
and `GAME_ENV("WRITE_ROOT")` to the directories `GameActivity` made.

The DOS runtime (`runtime/dos/`) asks the game's own runtime for its DOS
handle table and paths, declared in `Game_vfs.h`, and for `key_available` and
`dos_blocking_getch`. The game also brings `main.c`, its `display` frontend,
its sound and its llasm glue.

## Layout

```
SR/                     submodule: the recompiler, llasm, llasm-support
llasm/                  llasm.py, the Python port of SR's llasm, and its tests
scons/game.py           the whole build of one game, as one SCons graph
tools/compact_source.py compacts the data segments of SR's output
tools/android_sdl.sh    SDL2 and SDL2_mixer for Android
tools/android_apk.py    the Android APK of a game
tools/portable_cd.py    a portable copy of a game's disc (game/portable.cfg)
tools/regress/          regression checks: SR's output, builds, smoke tests, llasm hashes
android/                what the APK adds to SDL's android-project
runtime/core/           threads, memory, frontends, virtual keyboard, printf,
                        Android start-up
runtime/dos/            WATCOM C library and file I/O, CD image, CD audio
runtime/dos/llasm/      shared llasm includes of the DOS ports
runtime/win32/          the Windows-original counterpart -- to come
skeleton/               a starting point for a new game repository -- to come
```

`runtime/dos/` in more detail:

- `Game_clib.c`: the WATCOM C library functions a game's
  `external_procedures.sci` routes to the host;
- `Game_vfs.c`, `.h`: WATCOM stdio over DOS handles, with a write root that
  can overlay the game directory;
- `Game_file.c`, `.h`: DOS files as host files or as files of the CD image;
- `Game_cdrom.c`, `.h`: drive D: from a cue sheet (ISO9660 data track,
  CDDA or ogg/flac/wav audio tracks) or from a copy of the disc;
- `Game_music.c`, `.h`: MSCDEX audio playback through SDL_mixer;
- `Game_dosmem.c`, `.h`: DOS conventional memory of a DOS4GW game (DPMI blocks, real-mode
  buffers, the PSP) in one pool;
- `Game_sound.h`: the interface of a game's Miles AIL replacement.

## License

MIT, see [`LICENSE`](LICENSE). SR-SDK contains files taken from SR and
derived from it; they keep Roman Pauer's copyright, and files changed here add
their own line. [`NOTICE.md`](NOTICE.md) lists where each file comes from and
gives SR's license text, and that of `printf` by Marco Paland, which SR
includes.

Thanks to Roman Pauer for SR and its ports, on which all of this stands.
