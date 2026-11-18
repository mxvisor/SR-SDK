# tools

| Tool | What |
|---|---|
| `new_game.sh` | the skeleton of a new game repository ([below](#starting-a-new-port)) |
| `sr_run.py` | SR's run of a DOS program, twice when it must (the handlers of INT 21h AH=25h: [`scons/README.md`](../scons/README.md#srs-inputs)) |
| `compact_source.py` | compacts the data segments of SR's output (stage `gen`) |
| `gen_import_shim.py` | the llasm bridges of a Win32 port's imports (`game/import_args.tsv`) |
| `dll_exports.py` | a DLL of the original recompiled as a module (`dll_modules=`): what the runtime needs to load it |
| `gen_llasm_mmx.py` | writes `SR/SR/llasm-support/llasm_mmx.llinc` from the list of `llasm_mmx.c` |
| `image/` | disc images and installers: `image.py` and its format modules ([below](#disc-images)) |
| `disc_tree.py` | the game's disc, unpacked and patched (`build/disc`) |
| `distrib_cd.py` | a distrib copy of a game's disc, the game directory (`game/distrib.cfg`; [below](#the-game-directory)) |
| `floppy.py` | a game's floppy images and its installer's archives on them (`floppy=`) |
| `sf2_subset.py` | a SoundFont cut to some presets: the MIDI bank of a game (`soundfont=`) |
| `game_icon.py` | the port's icon from the game's own (`icon=`) |
| `font_cyrillic.py` | the Cyrillic letters of the OSD font (`runtime/core/Game_font_cyr.h`) |
| `android_sdl.sh`, `android_apk.py` | SDL2 and SDL2_mixer for Android, the APK of a game ([below](#android)) |
| `run_common.sh` | the common part of a game's `run-sdl.sh` / `run-sdl-wine.sh` |
| `run_orig_common.sh`, `orig_mcicda/` | the common part of `run-orig-dosbox.sh` / `run-orig-wine.sh`, and an MCI CD audio driver for Wine ([below](#running-the-original)) |
| `wine_dummy_audio.sh` | the "dummy" audio driver of SDL under Wine, for headless runs |
| `analysis/` | the work on a game's SR input: LE/PE, coverage ([`analysis/README.md`](analysis/README.md)) |
| `regress/` | the regression checks ([`regress/README.md`](regress/README.md)) |

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

Next come `main.c`, SR's inputs (`SR.cfg`, `*.sci`: [`scons/README.md`](../scons/README.md#srs-inputs))
and the game's hooks ([`runtime/README.md`](../runtime/README.md#what-a-game-provides)). The
`TODO` lines of the launchers name what to fill in. [`PORTING_DOS.md`](../PORTING_DOS.md) walks
through a DOS port: what SR stops at, the crashes of a first run, the flags, the sound, the traps.

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

## The game directory

`tools/distrib_cd.py OUT_DIR` (`make distrib-data`), from the game repository's root, makes the
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

## Disc images

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

## Android

`make apk` (`tools/android_apk.py`) makes the whole APK, from the game
repository's root:

```sh
ANDROID_HOME=/path/to/android-sdk \
    SR-SDK/tools/android_apk.py --data /path/to/game-data --out game.apk
```

It builds SDL2 and SDL2_mixer for arm64 once (`tools/android_sdl.sh`, into
`build/sysroot-android-arm64/`), then `libmain.so` with
`scons device=arm64-android`. The gradle project is put together in
`build/android/project/` from SDL's own `android-project` of the same release,
with `android/app.gradle` and `android/*.java` of SR-SDK over it, and the
game's `game/android/res/` (its icons) over that. On the first launch, and
after each update of the APK, `UnpackActivity` (the launcher's) unpacks the data
from the APK's assets on a thread of its own, with its progress on the screen
(`assets/game.files` lists the tree), then starts `GameActivity` (SDL's): the
port reads its files with `fopen`, and a copy of hundreds of MB on the
activity's thread makes Android close the program as one that does not answer.
`android/gameloader.c` loads the program's image at 64 MB of the guest window
([`runtime/README.md`](../runtime/README.md#the-guests-memory)).

## Checking a change

`tools/regress/run_all.sh` answers "did I break anything?" after a change to SR,
to `llasm.py` or to the SDK: SR's output for SR's reference games, `make linux`
and `make windows` of every game, a start-and-close smoke test of every port under
Linux and Wine, runs at different load addresses (ASLR), the hashes of the llasm output
and the conversion of the disc images, all compared with a saved reference run. The games
it checks and that reference are the workspace's regression data (`$REGRESS_CONF`, by
default `regress/` beside `SR-SDK/`), not part of the SDK. See [`regress/README.md`](regress/README.md).
