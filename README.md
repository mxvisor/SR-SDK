# SR-SDK

A build kit for native ports of DOS games made with
[SR](https://github.com/M-HT/SR), the static recompiler by Roman Pauer.
SR itself is the submodule `SR/` (the fork
[mxvisor/SR](https://github.com/mxvisor/SR), branch `master`: upstream plus
fixes offered back to it). Nothing here changes SR; this repository only adds
what a game repository needs around it.

```
SR/                     submodule: the recompiler, llasm, llasm-support
llasm/                  llasm.py, the Python port of SR's llasm, and its tests
scons/game.py           the whole build of one game, as one SCons graph
tools/compact_source.py compresses the data segments of SR's output
runtime/core/           shared runtime code of the ports (C and headers)
runtime/dos/llasm/      shared llasm includes of the DOS ports
runtime/win32/          the Windows-original counterpart -- to come
skeleton/               a starting point for a new game repository -- to come
```

What a game builds besides its own `game/`:

- every `.c` of `runtime/core/` and `runtime/<platform>/` (`platform` from
  `game.cfg`), unless `game/` has a file of the same name: that one replaces it;
- for the llasm backend, the `.c` of each `llasm_X.llinc` its `llasm.llinc`
  includes, straight from `SR/SR/llasm-support/` (the game's own `llasm.llinc`
  and `llasm_X.c` in `game/llasm/` take precedence);
- `game/`, `game/display`, `game/audio`, `game/input`: all `.c`, or the
  directory's own `SConscript` if it has one; `game/llasm/SConscript` is
  required.

The game provides what the runtime asks of it:

- `game/Game_hooks.h`: `GAME_MAIN_FILENAME`, the original's file name as
  `argv[0]`, and optionally `GAME_FLIP_HOOK()`, run before each frame is shown;
- for `runtime/dos/Game_clib.c`, the WATCOM C library replacements its
  `external_procedures.sci` routes there: `key_available` and
  `dos_blocking_getch` from the game's runtime;
- `Game_memory`, `main.c` and the `display` frontend, which still differ
  between the games.

Headers are found in `game/` first, then in the runtime and in SR's
`scaler-plugins/` and `llasm-support/`. A runtime `.c` sees the runtime's
headers beside it first, so a header override applies to the game's code only.

## Using it in a game repository

```
git submodule add https://github.com/mxvisor/SR-SDK.git SR-SDK
git submodule update --init --recursive
```

The game's root `SConstruct` is one line:

```python
SConscript('SR-SDK/scons/game.py')
```

Everything about the game is in `game/game.cfg` (the original executable and
its sha256, modules, archs, devices, program name); `scons/game.py` never names
a game. All build output goes to `build/` of the game repository: SR is built
from a copy there, so the checkouts of SR-SDK and SR stay clean. The llasm IR is
translated by `llasm/llasm.py` (see `llasm/README.md`), so the build needs
Python, SCons, a C compiler and LLVM's `opt` and `llc`, but no D compiler.

## License

MIT, see `LICENSE`. Files taken from SR keep Roman Pauer's copyright; files
changed or written here add their own line. `llasm/llasm.py` is a port of
SR's `llasm/llasm.d` and keeps its copyright. `tools/compact_source.py` is
based on the per-game `compact_source*.py` scripts in SR, which carry no header
of their own and fall under SR's MIT license.
