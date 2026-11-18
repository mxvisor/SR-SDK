# scons: the build of one game

`scons/game.py` is the whole build of one game, as one SCons graph; a game repository's
`SConstruct` is one line, `SConscript('SR-SDK/scons/game.py')`, and its `Makefile` is
`include SR-SDK/scons/game.mk`. `scons/game.py` never names a game.

| File | What |
|---|---|
| `game.py` | the stages, from SR's build to the binary of each device |
| `release.SConscript` | `make distrib`, `release`, `apk`, `disc`, `clean`: the targets of the release |
| `game.mk` | the `make` targets: it only chains scons's |
| `llasm.SConscript` | the llasm glue of a game without `game/llasm/SConscript` |
| `glibc_max.py` | the glibc versions of a Linux binary (`glibc_max`) |

## Using it in a game repository

```sh
git submodule add https://github.com/mxvisor/SR-SDK.git SR-SDK
git submodule update --init --recursive
echo "SConscript('SR-SDK/scons/game.py')" > SConstruct
```

`tools/new_game.sh` makes all of this, and the rest of the skeleton
([`tools/README.md`](../tools/README.md#starting-a-new-port)).

## game.cfg

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
targets=                           # game/ only: the other programs, game-<name>/ (see "Several programs of one game" below)
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

## SR's inputs

`game/recomp/<module>/<variant>/` holds SR's inputs for the original: its
`sha256`, `SR.cfg` and the `*.sci` files. `sha256` may list several originals, one per
line (the hash first, then a note): releases of the game whose exe differs only where
SR's inputs do not care (two languages of one build) share them.

For a DOS program `tools/sr_run.py` runs SR twice when it must: SR's `--list_handlers=FILE`
lists the routines the program gives to INT 21h AH=25h (EDX loaded with an address, directly or
through a wrapper routine that takes it from its stack argument), and the second run takes them as
code, so the game's `fixup_interpret_as_code.sci` is for what this pattern misses. A DOS4GW program
also gets `esp_dword_aligned`, `ebp_dword_aligned` and `code_writable` in its `SR.cfg` from
`scons/game.py`. Keys of `SR.cfg` a game may need (off by default; `SR/SR/SR_main.c`):

- `int_stub_table=yes`: a WATCOM library whose int386x() returns into a table of 256 stubs
  `int N / ret`: SR finds the table and gives the instruction that takes its address a stub of
  its own;
- `exact_push16=yes`: a 16-bit push or pop moves esp by 2, as the CPU does: for code
  that measures the distance of a pointer from esp after such pushes;
- `self_modifying_code=yes`: the program writes immediates and displacements of its own
  instructions: each such instruction gets a copy in the data segment `smc`, the writes go to the
  copy and the instruction reads its operand from it; SR and SRW
  ([PORTING_DOS.md](../PORTING_DOS.md#4-srs-first-run-scons-stagerecomp));
- `global_prefix=<name>`: before every global name of the output; set by `scons/game.py` for
  `exe_modules=`.

In SRW's `instruction_replacements.sci`, a replacement whose llasm ends with `endp` (after a `tcall`
of its own) ends its procedure: the bytes after it are code only when something else reaches them,
and a `loc_X` of data that it names gets a label. So a `call` whose callee pops the return address
as the address of a table after the call (WATCOM's library: `call X` / `X: pop edi / ret`) becomes
`mov edi, loc_<table>|POP tmp1|tcall tmp1|endp`.

SR's warnings about the game's `fixup_interpret_as_code.sci` (a label of it that the code reads as
memory: a table, not code) are shown on the build's output as `SR: ...`.

## Building

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

The stages are `host` (SR, built from a copy in `build/`), `input` (the
original, checked against its sha256), `recomp` (SR), `gen` (SR's output,
compacted by `tools/compact_source.py`), `obj` and `bin`. All output goes to `build/` of the game
repository, so the checkouts of SR-SDK and SR stay clean.

`llasm/llasm.py` and `opt` make one bitcode per `.llasm` for all devices, `build/obj/ir/*.bc`;
each device runs only `llc` on it, into `build/obj/<device>/gen/*.o`. On SR's reference games the
output of `llasm.py` is byte-identical to SR's D original ([`llasm/README.md`](../llasm/README.md)).
To find a procedure of the original in a debugger, delete `build/obj/ir/*.bc` and build once with
`LLASM_SYMBOLS=1`: every procedure and every label of the data keeps its name (`loc_XXXX`, the
address in the original) in the object, for `nm` and `gdb`, as a local symbol (the modules of
`dll_modules=` have labels of the same names); the variable is not part of the command, so SCons
does not see it change.

## The make targets

The game's `Makefile` is `include SR-SDK/scons/game.mk`: the targets are scons's
(`scons/game.py` and `scons/release.SConscript`), the `.mk` only chains them.

- `make linux`: the binary and the `libSDL2_mixer-2.0.so.0` the linker found, beside it (the
  binary's RPATH is `$ORIGIN`, so a release runs where the system has SDL2 and no SDL2_mixer,
  as SteamOS);
- `make windows`: the MinGW build with the SDL DLLs beside the exe;
- `make distrib-data`: `build/distrib/data`, the game directory of the fullest install with the
  music, from `game/distrib.cfg` (`tools/distrib_cd.py`);
- `make distrib`: both builds, the data, and `build/distrib/<name>-x64.zip`: both programs with
  the Windows DLLs and the Linux `.so`, `game/release/{linux,windows}/` and their `x64/`, where
  `@GLIBC_MAX@` of a `.txt` is the build's `glibc_max`, `<program>-NOTICE.txt` (`LICENSE` and
  `NOTICE.md`; the APK has it as `assets/NOTICE.txt`) and the data;
- `make release`: `build/distrib/<name>-x64-port.zip`, the same without the game's data, the
  programs for a game already installed (the distrib archive and the APK hold the game's files);
- `make apk`: the Android package ([`tools/README.md`](../tools/README.md#android));
- `make disc`: `build/disc`;
- `make host|input|recomp|gen|obj`: one stage;
- `make clean`: everything built; the disc's unpacked files, `build/disc`, `build/cd` and
  `build/distrib/data`, stay, so the tree builds on a machine without the disc image;
- `make distclean`: those too;
- `make test`.

The same names work as `scons <name>`; `MINGW_BIN=` points at the DLLs.

## glibc_max

A Linux binary asks for no glibc newer than `glibc_max` (default 2.38), so it runs on systems
older than the one it is built on.  glibc keeps the old versions of a function it changes
(`sqrtf` and `log10f` got `GLIBC_2.43`, and still have `GLIBC_2.2.5`), and the linker takes the
newest: before the link, `scons/glibc_max.py` sends the calls of every object to the newest
version not above `glibc_max` (a trampoline with `.symver`, and `-Wl,--wrap=`); after it,
the build fails, naming them, when a function has no such version (the `__isoc23_*` of
glibc 2.38).

## Several programs of one game: targets

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

## What is built

Every build is position-independent (Linux and Android: PIE / `-fPIC`, llasm
`-pic -ptrofs`; Windows: dynamicbase, high-entropy VA, a `.reloc` section). Guest
pointers are 32-bit offsets from a load-time base, never absolute host addresses,
and there is no fixed-address mode (the guest window: [`runtime/README.md`](../runtime/README.md#the-guests-memory)).

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
