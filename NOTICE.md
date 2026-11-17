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

## xBR-lv2 and Super-xBR

`runtime/core/gl/Game_gl_xbr.h` holds Hyllian's xBR-lv2 and Super-xBR shaders
from libretro's [glsl-shaders](https://github.com/libretro/glsl-shaders)
(`xbr/shaders/xbr-lv2.glsl`, `xbr/shaders/super-xbr/super-xbr-pass0..2.glsl`),
rewritten as fragment shaders of `Game_gl.c`, under the MIT license; the file
keeps their header: Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com.

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

## emu8950

`extern/emu8950/` is a git submodule of
[emu8950](https://github.com/digital-sound-antiques/emu8950), pinned to a
commit: Mitsutaka Okazaki's Y8950/YM3526/YM3812 emulator, the OPL2 of
`runtime/core/Game_opl.c`, under the MIT license (the full text is below under
"SR's license"), with this copyright line in its `LICENSE`:

```
Copyright (c) 2014 Mitsutaka Okazaki
```

and this one in the header of its source, `emu8950.c`:

```
Copyright (C) 2001-2026 Mitsutaka Okazaki
```

## The Audio Interface Library 2

`runtime/dos/Game_dosail2fm.c` is a port to C of `XMIDI.ASM` and `YAMAHA.INC`,
the Ad Lib XMIDI driver of the Audio Interface Library 2.14 by John Miles
(Copyright (C) 1991, 1992 Miles Design, Inc.), whose package says:

```
As of May 26, 2000, all source code and compiled object code in this package
may be considered "open-source freeware," usable by anyone for any purpose,
commercial or otherwise, without restriction or limitation.
```

The port is under SR-SDK's MIT license.

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

## Source Code Pro (the Cyrillic letters of the OSD font)

`runtime/core/Game_font_cyr.h` holds the Cyrillic letters of the port's text
font, the ones that do not look like Latin letters: bitmaps made by
`tools/font_cyrillic.py` from Source Code Pro Regular 2.042, fitted to the cell
of `VKfont2.h`. They are a work derived from the font and stay under its
license, the SIL Open Font License 1.1 (not MIT):

Copyright 2023 Adobe (http://www.adobe.com/), with Reserved Font Name
'Source'. All Rights Reserved. Source is a trademark of Adobe in the United
States and/or other countries.

```
-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

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
