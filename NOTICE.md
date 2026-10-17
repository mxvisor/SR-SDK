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

## SDL

SR-SDK contains no SDL sources. `tools/android_sdl.sh` downloads the release
archives of [SDL](https://github.com/libsdl-org/SDL) and
[SDL_mixer](https://github.com/libsdl-org/SDL_mixer) (zlib license, Sam
Lantinga and contributors) and checks their sha256; `tools/android_apk.py`
builds the APK from SDL's `android-project` in that archive, which keeps its
license files. `android/app.gradle` replaces that project's
`app/build.gradle`, of which it keeps the `android { }` settings, and is
under SDL's zlib license:

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Files written for SR-SDK

`runtime/core/Game_clib.c`, `Game_cdrom.*`, `Game_file.*`, `Game_gamedir.c`,
`Game_keys.*`, `Game_music.*`, `Game_sound.h`, `Game_vfs.*`, `Game_android.*`,
`runtime/dos/Game_dosfile.*`, `Game_doskbd.*`, `Game_doscd.*`, `Game_dospit.*`,
`android/GameActivity.java`, `tools/android_apk.py`, `tools/android_sdl.sh`
and `llasm/llasm_compare.py`, `llasm_edge.py`, `llasm_fuzz.py`: Copyright (C)
2026 mxvisor, MIT; so are `tools/image/*.py` and `tools/regress/*`.

## blast

`tools/image/blast.c` and `blast.h`: blast 1.3 by Mark Adler (zlib's
`contrib/blast`), unchanged, under the zlib-style license in `blast.h`. It
unpacks the PKWARE DCL data of InstallShield 3 archives. `blast_shim.c` is
SR-SDK's, MIT.

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
