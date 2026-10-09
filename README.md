# SR-SDK

A build kit for native ports of DOS games made with
[SR](https://github.com/M-HT/SR), the static recompiler by Roman Pauer.
SR itself is the submodule `SR/` (the fork
[mxvisor/SR](https://github.com/mxvisor/SR), branch `master`: upstream plus
fixes offered back to it). Nothing here changes SR; this repository only adds
what a game repository needs around it.

```
SR/                     submodule: the recompiler, llasm, llasm-support
scons/game.py           the whole build of one game, as one SCons graph
tools/compact_source.py compresses the data segments of SR's output
runtime/                shared runtime code (core, dos, win32) -- to come
skeleton/               a starting point for a new game repository -- to come
```

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
a game. All build output goes to `build/` of the game repository: SR and llasm
are built from copies there, so the checkouts of SR-SDK and SR stay clean.

## License

MIT, see `LICENSE`. Files taken from SR keep Roman Pauer's copyright; files
changed or written here add their own line. `tools/compact_source.py` is based
on the per-game `compact_source*.py` scripts in SR, which carry no header of
their own and fall under SR's MIT license.
