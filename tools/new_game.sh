#!/usr/bin/env bash
# Start the repository of a new port: the skeleton a game needs before SR can run.
#
#   new_game.sh DIR NAME dos|win32 [--exe PATH] [--sdk URL_OR_PATH] [--env PREFIX]
#
#   DIR     the new repository (created; must be empty or missing)
#   NAME    the program name, lowercase with dashes: build/bin/<device>/NAME (game.cfg program=)
#   --exe   the original executable: its sha256 goes to game/recomp/, input_exe= to game.cfg
#           (path relative to DIR, or absolute); without it the sha256 is a placeholder
#   --sdk   where SR-SDK comes from (default: the repository this script is in)
#   --env   the prefix of the port's environment variables (default: NAME in capitals, no dashes)
#
# It writes SConstruct, Makefile, .gitignore, game/{game.cfg,Game_hooks.h,recomp/},
# the launchers run-sdl.sh, run_sdl_wine.sh and run-orig-*.sh, makes a git repository and
# adds SR-SDK (with SR) as a submodule at the commit this script is in.  Nothing is
# committed.  Then `scons stage=input` checks the original; main.c, the SR inputs
# (SR.cfg, *.sci) and the game's hooks are the work of the port.
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

[ $# -ge 3 ] || { usage; exit 1; }
DIR="$1"; NAME="$2"; PLATFORM="$3"; shift 3
EXE=""; SDK_URL="$SDK_DIR"
ENVP="$(printf '%s' "$NAME" | tr -d '-' | tr '[:lower:]' '[:upper:]')"
while [ $# -gt 0 ]; do
  case "$1" in
    --exe) EXE="$2"; shift ;;
    --sdk) SDK_URL="$2"; shift ;;
    --env) ENVP="$2"; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done
case "$PLATFORM" in dos|win32) ;; *) echo "platform must be dos or win32" >&2; exit 1 ;; esac
case "$NAME" in *[!a-z0-9-]*|'') echo "NAME: lowercase letters, digits and dashes only" >&2; exit 1 ;; esac

mkdir -p "$DIR"
[ -z "$(ls -A "$DIR")" ] || { echo "$DIR is not empty" >&2; exit 1; }
DIR="$(cd "$DIR" && pwd)"
MODULE="Game-main"
SHA="0000000000000000000000000000000000000000000000000000000000000000"
INPUT_EXE="../GAME.EXE"
if [ -n "$EXE" ]; then
  SRC="$EXE"; case "$EXE" in /*) ;; *) SRC="$DIR/$EXE" ;; esac
  [ -f "$SRC" ] || { echo "No such file: $SRC" >&2; exit 1; }
  SHA="$(sha256sum "$SRC" | cut -d' ' -f1)"; INPUT_EXE="$EXE"
fi
TITLE="$(printf '%s' "$NAME" | tr '-' ' ')"
COPYRIGHT="Copyright (C) $(date +%Y) $(git config user.name 2>/dev/null || echo 'the author')"

mkdir -p "$DIR/game/recomp/$MODULE/default"
cd "$DIR"

cat > SConstruct <<'EOF'
# The thin root: the build is SR-SDK/scons/game.py, this game is game/game.cfg.

SConscript('SR-SDK/scons/game.py')
EOF

cat > .gitignore <<'EOF'
/build/
/run/
.sconsign.dblite
/*.EXE
/*.exe
EOF

{
  cat <<EOF
# The description of the game for SR-SDK/scons/game.py (keys: SR-SDK/README.md).

platform=$PLATFORM
sdk_version=0.1
archs=llasm
modules=$MODULE
variants=default
input_exe=$INPUT_EXE
link=separate
devices=pc64-linux,pc64-windows
program=$NAME
EOF
  [ "$PLATFORM" = win32 ] && printf '# winapi=no leaves out the Win32 API layer; runtime_skip=File.c,... single runtime files\n'
  true
} > game/game.cfg

printf '%s\n' "$SHA" > "game/recomp/$MODULE/default/sha256"
cat > "game/recomp/$MODULE/default/SR.cfg" <<EOF
# SR's configuration for the original. Empty = the built-in defaults;
# keys are added after the first run of SR, when something needs them.
EOF

ORIG_NAME="GAME.EXE"
cat > game/Game_hooks.h <<EOF
/* What SR-SDK's runtime asks of this game.  Every hook is optional: see the
 * #ifndef defaults in SR-SDK/runtime/ for the full list. */

#ifndef _GAME_HOOKS_H_INCLUDED_
#define _GAME_HOOKS_H_INCLUDED_

/* the original's file name (argv[0] of the guest) */
#define GAME_MAIN_FILENAME "$ORIG_NAME"

/* the game's name in the port's messages */
#define GAME_NAME "$TITLE"
/* the port's environment variables: GAME_ENV("CD_IMAGE") is \$${ENVP}_CD_IMAGE */
#define GAME_ENV(name) "${ENVP}_" name

#endif /* _GAME_HOOKS_H_INCLUDED_ */
EOF

cat > Makefile <<'EOF'
SHELL := /bin/sh
TOP := $(abspath .)
MINGW_BIN ?= /usr/x86_64-w64-mingw32/sys-root/mingw/bin
PROGRAM := $(shell sed -n 's/^program=//p' game/game.cfg)

.PHONY: all host input recomp gen obj linux windows clean help

all: linux windows

host input recomp gen obj:
	scons stage=$@

HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
LINUX_DEVICE := arm64-linux
else
LINUX_DEVICE := pc64-linux
endif
linux:
	scons stage=bin device=$(LINUX_DEVICE) -j$$(nproc)

# Never run it at the same time as `linux`: both write to build/.
windows:
	scons stage=bin device=pc64-windows -j$$(nproc)
	@for dll in SDL2.dll SDL3.dll SDL2_mixer.dll; do \
		[ -f "$(MINGW_BIN)/$$dll" ] || { echo "Missing $$dll in $(MINGW_BIN)" >&2; exit 1; }; \
		cp -p "$(MINGW_BIN)/$$dll" "$(TOP)/build/bin/pc64-windows/$$dll"; \
	done

clean:
	rm -rf $(TOP)/build $(TOP)/run $(TOP)/.sconsign.dblite

help:
	@echo "targets: host input recomp gen obj linux windows clean"
EOF

launcher() { # file title kind-line
  cat > "$1" <<EOF
#!/usr/bin/env bash
# $2
# Options, environment and the layout of the data: SR-SDK/tools/$3 (--help).
set -euo pipefail
GAME_REPO="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
source "\$GAME_REPO/SR-SDK/tools/$3"

$4
EOF
  chmod +x "$1"
}
COMMON="PROG=$NAME
ENVP=$ENVP
DATA_DEFAULT=\"\$GAME_REPO/../iso\"        # TODO: the game's files
DATA_MARKER=GAME.EXE                      # TODO: a file the data directory must hold"
launcher run-sdl.sh "Run the native port ($TITLE)." run_common.sh "$COMMON

run_native \"\$@\""
launcher run_sdl_wine.sh "Run the Windows build of the port ($TITLE) under Wine." run_common.sh "$COMMON
WINE_NAME=$NAME-wine

run_wine \"\$@\""
if [ "$PLATFORM" = dos ]; then
  launcher run-orig-dosbox.sh "Run the ORIGINAL $TITLE under DOSBox (the reference for the port)." run_orig_common.sh "ENVP=$ENVP
DATA_DEFAULT=\"\$GAME_REPO/../iso\"            # TODO: C:
CD_DEFAULT=\"\$GAME_REPO/../iso/CD/*.cue\"     # TODO: the disc, if any
DOS_CMD=GAME.EXE                              # TODO

run_orig_dosbox \"\$@\""
else
  launcher run-orig-wine.sh "Run the ORIGINAL $TITLE under Wine (the reference for the port)." run_orig_common.sh "ENVP=$ENVP
DATA_DEFAULT=\"\$GAME_REPO/../iso/iso_unpacked\"   # TODO: the directory with the exe
CD_DEFAULT=\"\$GAME_REPO/../iso/iso_unpacked\"     # TODO: D:, if any
WIN_EXE=GAME.EXE                                  # TODO
WINE_NAME=$NAME-wine

run_orig_wine \"\$@\""
fi

git init -q
git -c protocol.file.allow=always submodule add -q "$SDK_URL" SR-SDK
SDK_HEAD="$(git -C "$SDK_DIR" rev-parse HEAD)"
git -C SR-SDK fetch -q --no-recurse-submodules origin
git -C SR-SDK checkout -q --no-recurse-submodules "$SDK_HEAD"
git -C SR-SDK -c protocol.file.allow=always submodule update -q --init --recursive
git -C SR-SDK/SR checkout -q master 2>/dev/null || true

echo "A new $PLATFORM port is in $DIR (SR-SDK at ${SDK_HEAD:0:7})."
echo "Next: put the original at $INPUT_EXE$([ -n "$EXE" ] || echo ' and its sha256 into game/recomp/'"$MODULE"'/default/sha256'), then: scons stage=input"
