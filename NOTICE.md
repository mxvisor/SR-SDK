# Notices

SR-SDK is built on [SR](https://github.com/M-HT/SR) by Roman Pauer. SR is
released under the MIT license (and, for use in GPL projects, also under
GPLv2 or later and LGPLv2.1 or later); SR-SDK uses it under the MIT license.
The MIT license asks that its copyright notice and permission notice go with
every copy or substantial portion of the software. SR-SDK meets this as
follows:

- every file taken from SR keeps its header with Roman Pauer's copyright and
  the permission notice;
- a file changed here keeps that header and adds a `Copyright (C) 2026
  mxvisor` line;
- `LICENSE` names both copyright holders and carries the MIT text;
- this file lists where each file comes from and gives SR's license text
  below.

## SR itself

`SR/` is a git submodule: the fork
[mxvisor/SR](https://github.com/mxvisor/SR) of
[M-HT/SR](https://github.com/M-HT/SR), branch `master`, which is upstream plus
fixes that can be offered back to it. SR-SDK does not change it. The build uses
`SR/SR/llasm-support/` (the `llasm_*.c` support code) and
`SR/scaler-plugins/scaler-plugins.h` from there directly, without copies.

## Files copied from SR unchanged

Each of these is identical to a file of one of SR's game ports in
`SR/games/`:

| File | Copyright |
|---|---|
| `runtime/core/Game_defs.h` | Roman Pauer |
| `runtime/core/Game_scalerplugin.h` | Roman Pauer |
| `runtime/core/Game_virtualkeyboard.c`, `.h` | Roman Pauer |
| `runtime/core/VKfont2.h` | font bitmap data, from SR |
| `runtime/core/audio.h`, `display.h`, `input.h`, `main.h` | Roman Pauer |
| `runtime/core/display/palette32bgra.h`, `palettecommon.h` | Roman Pauer |
| `runtime/core/ptr32.h` | Roman Pauer |
| `runtime/core/virtualfs.c`, `.h` | Roman Pauer |
| `runtime/core/printf_x86.c`, `.h` | Marco Paland, MIT (see below); SR's modified version |
| `runtime/dos/llasm/SR-asm-calls.llinc` | Roman Pauer |
| `runtime/dos/llasm/macros.llinc` | Roman Pauer |

## Files derived from SR and changed here

| File | Derived from | Copyright |
|---|---|---|
| `runtime/core/Game_thread.c`, `.h` | the ports' `Game_thread.c`, `.h` | Roman Pauer, mxvisor |
| `runtime/core/Game_vars.h` | the ports' `Game_vars.h` | Roman Pauer, mxvisor |
| `runtime/core/Game_memory.c`, `.h` | the ports' `Game_memory.c`, `.h` | Roman Pauer, mxvisor |
| `runtime/core/Game_scalerplugin.c` | the ports' `Game_scalerplugin.c` | Roman Pauer, mxvisor |
| `runtime/core/audio/pc.c`, `input/pc.c` | the ports' `audio/pc.c`, `input/pc.c` | Roman Pauer, mxvisor |
| `runtime/core/touchgamepad.h` | the header of the ports' runtime headers | Roman Pauer, mxvisor |
| `scons/game.py` | the ports' `SConstruct` files | Roman Pauer, mxvisor |
| `tools/compact_source.py` | SR's per-game `compact_source*.py` (no header of their own; SR's MIT license) | Roman Pauer, mxvisor |
| `llasm/llasm.py` | a port to Python of SR's `llasm/llasm.d` | Roman Pauer, mxvisor |

## Files written for SR-SDK

`runtime/dos/Game_clib.c`, `runtime/dos/Game_cdrom.*`, `Game_file.*`,
`Game_music.*`, `Game_sound.h`, `Game_vfs.*` and `llasm/llasm_compare.py`,
`llasm_edge.py`, `llasm_fuzz.py`: Copyright (C) 2026 mxvisor, MIT.

## SR's license

As in the headers of SR's source files (the years differ from file to file):

```
Copyright (C) 2016-2026 Roman Pauer

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## printf

`runtime/core/printf_x86.c` and `.h` are SR's modified version of
[printf 4.0.0](https://github.com/mpaland/printf):

```
(c) Marco Paland (info@paland.com)
    2014-2019, PALANDesign Hannover, Germany

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
