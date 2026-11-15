#
#  Copyright (C) 2026 mxvisor
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
#  of the Software, and to permit persons to whom the Software is furnished to do
#  so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.
#

# The targets of a game, as `make <name>`: a thin layer over scons.  The game's
# Makefile is one line, `include SR-SDK/scons/game.mk` (tools/new_game.sh writes
# it); everything the targets do is in scons/game.py and scons/release.SConscript.
#
#   make [linux windows]   the native binary / the MinGW cross build with the DLLs
#   make distrib           release archives in build/distrib/ (builds both, and the data)
#   make release           build/distrib/<name>-x64-port.zip: the archive of distrib
#                          without the game's data, to publish
#   make distrib-data      build/distrib/data: the disc's files and music
#   make apk               the Android APK in build/distrib/ (tools/android_apk.py)
#   make disc | host | input | recomp | gen | obj    one stage (scons stage=...)
#   make clean             everything built, but not the disc's unpacked files (build/disc,
#                          build/cd, build/distrib/data): the tree builds on another machine
#                          without the disc image
#   make distclean         build/ and run/ whole, the unpacked disc too
#   make test | help
#
# A repository of several targets (targets= of game/game.cfg, game-<name>/; see
# scons/game.py): `linux`, `windows` and the stages build every target in turn,
# or those of TARGETS=<name ...> (default: the main one); `apk` builds the one
# of TARGET=<name> (default the main one).

SCONS ?= scons
JOBS ?= $(shell nproc)
# the Android SDK of the workspace (<workspace>/ANDROID_SDK) unless ANDROID_HOME says otherwise
export ANDROID_HOME ?= $(abspath $(CURDIR)/../../ANDROID_SDK)
SDK := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
TARGETS ?= default $(subst $(comma), ,$(shell sed -n 's/^targets[ \t]*=//p' game/game.cfg))
TARGET ?= default
comma := ,

# The targets share build/ (the host tree, build/disc): never two at once, even
# under `make -j`.  The parallelism is scons's own (JOBS).
.NOTPARALLEL:

.PHONY: all windows-skipped host input recomp gen obj linux windows disc distrib-data distrib release apk \
        clean distclean test help

# the Windows build needs the MinGW-w64 cross compiler: a host without it (a board) builds
# the native binary and says so; `make windows` and `make distrib` insist
MINGW_GCC ?= x86_64-w64-mingw32-gcc
HAVE_MINGW := $(shell command -v $(MINGW_GCC) >/dev/null 2>&1 && echo yes)

all: $(if $(HAVE_MINGW),,windows-skipped) linux $(if $(HAVE_MINGW),windows)

windows-skipped:
	@echo "make: the Windows build is skipped, no $(MINGW_GCC) (MinGW-w64); \`make windows\` says what is missing"

host input recomp gen obj:
	set -e; for t in $(TARGETS); do $(SCONS) target=$$t stage=$@; done

linux windows:
	set -e; for t in $(TARGETS); do $(SCONS) -j$(JOBS) target=$$t $@; done

disc distrib-data:
	$(SCONS) -j$(JOBS) $@

distrib: linux windows distrib-data
	$(SCONS) distrib

release: linux windows
	$(SCONS) release

apk: distrib-data
	python3 $(SDK)/tools/android_apk.py -j $(JOBS) --target $(TARGET)

clean distclean:
	$(SCONS) -Q $@

test: clean linux
	$(SCONS) -Q check

help:
	$(SCONS) -h
