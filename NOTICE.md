# Notices

SR-SDK is built on [SR](https://github.com/M-HT/SR) by Roman Pauer. SR is
released under the MIT license (and, for use in GPL projects, also under
GPLv2 or later and LGPLv2.1 or later); SR-SDK uses it under the MIT license,
whose text is below under "SR's license".

Where a file comes from, its header says:

- a file taken from SR, changed or not, keeps SR's header with Roman Pauer's
  copyright and the permission notice; a changed one adds a
  `Copyright (C) 2026 mxvisor` line. A file that has another name here than in
  SR, or holds a part of a file of SR, gives the name of SR's file in
  parentheses: `Copyright (C) 2016-2026 Roman Pauer (main.c)`;
- a file written for SR-SDK carries `Copyright (C) 2026 mxvisor` and the MIT
  notice;
- third-party code (below) keeps its own header and license.

`runtime/core/VKfont2.h`, a font bitmap without a header, is SR's file, unchanged
(Roman Pauer, MIT).

`LICENSE` names both copyright holders and carries the MIT text.

## SR itself

`SR/` is a git submodule: the fork
[mxvisor/SR](https://github.com/mxvisor/SR) of
[M-HT/SR](https://github.com/M-HT/SR), branch `master`, which is upstream plus
fixes that can be offered back to it. SR-SDK does not change it. The build uses
`SR/SR/llasm-support/` (the `llasm_*.c` support code) from there directly,
without copies.

## TinySoundFont and TinyMidiLoader

`extern/tsf/` is a git submodule of
[TinySoundFont](https://github.com/schellingb/TinySoundFont), pinned to a
commit. `tsf.h` is TinySoundFont by Bernhard Schelling, based on SFZero
(Copyright (C) 2012 Steve Folta), under the MIT license; the full MIT text is
below under "SR's license", with this copyright line:

```
Copyright (C) 2017-2023 Bernhard Schelling (Based on SFZero, Copyright (C) 2012 Steve Folta)
```

`tml.h` is TinyMidiLoader by Bernhard Schelling, under the Zlib license:

```
Copyright (C) 2017, 2018, 2020 Bernhard Schelling

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
