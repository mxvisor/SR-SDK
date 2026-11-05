# The targets of a game, as `make <name>`: a thin layer over scons.  The game's
# Makefile is one line, `include SR-SDK/scons/game.mk` (tools/new_game.sh writes
# it); everything the targets do is in scons/game.py and scons/release.SConscript.
#
#   make [linux windows]   the native binary / the MinGW cross build with the DLLs
#   make portable          release archives in build/dist/ (builds both, and the data)
#   make portable-data     build/dist/portable: the disc's files and music
#   make apk               the Android APK in build/dist/ (tools/android_apk.py)
#   make disc | host | input | recomp | gen | obj    one stage (scons stage=...)
#   make clean | test | help
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

.PHONY: all host input recomp gen obj linux windows disc portable-data portable apk \
        clean distclean test help

all: linux windows

host input recomp gen obj:
	set -e; for t in $(TARGETS); do $(SCONS) target=$$t stage=$@; done

linux windows:
	set -e; for t in $(TARGETS); do $(SCONS) -j$(JOBS) target=$$t $@; done

disc portable-data:
	$(SCONS) -j$(JOBS) $@

portable: linux windows portable-data
	$(SCONS) portable

apk: portable-data
	python3 $(SDK)/tools/android_apk.py -j $(JOBS) --target $(TARGET)

clean distclean:
	$(SCONS) -Q $@

test: clean linux
	$(SCONS) -Q check

help:
	$(SCONS) -h
