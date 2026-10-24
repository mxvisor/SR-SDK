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

SCONS ?= scons
JOBS ?= $(shell nproc)
SDK := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

# The targets share build/ (the host tree, build/disc): never two at once, even
# under `make -j`.  The parallelism is scons's own (JOBS).
.NOTPARALLEL:

.PHONY: all host input recomp gen obj linux windows disc portable-data portable apk \
        clean distclean test help

all: linux windows

host input recomp gen obj:
	$(SCONS) stage=$@

linux windows disc portable-data:
	$(SCONS) -j$(JOBS) $@

portable: linux windows portable-data
	$(SCONS) portable

apk: portable-data
	python3 $(SDK)/tools/android_apk.py -j $(JOBS)

clean distclean:
	$(SCONS) -Q $@

test: clean linux
	$(SCONS) -Q check

help:
	$(SCONS) -h
