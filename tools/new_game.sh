#!/usr/bin/env bash
# Start the repository of a new port: the skeleton a game needs before SR can run.
#
#   new_game.sh DIR NAME dos|win32 [--exe PATH|disc:NAME] [--image IMAGE] [--sdk URL_OR_PATH] [--env PREFIX]
#
#   DIR     the new repository (created; must be empty or missing)
#   NAME    the program name, lowercase with dashes: build/bin/<device>/NAME (game.cfg program=)
#   --exe   the original executable: its sha256 goes to game/recomp/, input_exe= to game.cfg
#           (path relative to DIR, or absolute); without it the sha256 is a placeholder.
#           disc:NAME is a file of the game's disc (needs --image): it is taken from
#           build/disc/, made by tools/disc_tree.py, so the exe need not be unpacked by hand
#   --image the disc image (.iso/.nrg/.mds/.bin/.cue; path relative to DIR, or absolute):
#           game/portable.cfg gets image= (its cue sheet is made in build/cd/); the
#           launchers take the game directory from build/dist/portable (make portable-data)
#   --sdk   where SR-SDK comes from (default: the repository this script is in)
#   --sr    where SR comes from (default: SR of the --sdk repository when that is a directory,
#           else the URL in SR-SDK/.gitmodules)
#   --env   the prefix of the port's environment variables (default: NAME in capitals, no dashes)
#
# It writes SConstruct, Makefile, .gitignore, game/{game.cfg,Game_hooks.h,recomp/},
# the launchers run-sdl.sh, run-sdl-wine.sh and run-orig-*.sh, makes a git repository and
# adds SR-SDK (with SR) as a submodule at the commit this script is in.  Nothing is
# committed.  Then `scons stage=input` checks the original; main.c, the SR inputs
# (SR.cfg, *.sci) and the game's hooks are the work of the port.
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

[ $# -ge 3 ] || { usage; exit 1; }
DIR="$1"; NAME="$2"; PLATFORM="$3"; shift 3
EXE=""; IMAGE=""; SDK_URL="$SDK_DIR"; SR_URL=""
ENVP="$(printf '%s' "$NAME" | tr -d '-' | tr '[:lower:]' '[:upper:]')"
while [ $# -gt 0 ]; do
  case "$1" in
    --exe) EXE="$2"; shift ;;
    --image) IMAGE="$2"; shift ;;
    --sdk) SDK_URL="$2"; shift ;;
    --sr) SR_URL="$2"; shift ;;
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
INPUT_EXE="disc:GAME.EXE"
ORIG_NAME="GAME.EXE"
REL_IMAGE="../iso/GAME.iso                  # TODO: the disc image"
CUE_BASE=GAME
if [ -n "$IMAGE" ]; then
  SRC="$IMAGE"; case "$IMAGE" in /*) ;; *) SRC="$DIR/$IMAGE" ;; esac
  [ -f "$SRC" ] || { echo "No such file: $SRC" >&2; exit 1; }
  SRC="$(realpath "$SRC")"
  REL_IMAGE="$(realpath -m --relative-to="$DIR" "$SRC")"
  CUE_BASE="$(basename "${SRC%.*}")"
fi
mkdir -p "$DIR/game"
cat > "$DIR/game/portable.cfg" <<EOF
# The game directory of the portable copy (SR-SDK/tools/portable_cd.py) and the disc
# (SR-SDK/tools/disc_tree.py): what the game's fullest install makes of the disc, with
# what that install leaves on the disc. Paths are relative to the repository's root.

# the disc image, the one path outside the repository (its cue sheet goes to build/cd/)
image=$REL_IMAGE

# the disc's files the game's installer does not copy (the installer itself; what
# it copies elsewhere is moved with place=, see portable_cd.py): add them here
skip_files=AUTORUN.* SETUP.* INSTALL.*

# music: the audio tracks of the disc
audio_dir=AUDIO
audio_cue=$CUE_BASE.cue
audio=ogg
EOF
case "$EXE" in
  disc:*)
    [ -n "$IMAGE" ] || { echo "--exe $EXE needs --image" >&2; exit 1; }
    DISC_NAME="${EXE#disc:}"
    (cd "$DIR" && python3 -I "$SDK_DIR/tools/disc_tree.py" build/disc) >&2
    FOUND="$DIR/build/disc"
    IFS=/ read -ra PARTS <<< "$DISC_NAME"
    for part in "${PARTS[@]}"; do
      next="$(ls -1 "$FOUND" | awk -v p="$part" 'tolower($0) == tolower(p) { print; exit }')"
      [ -n "$next" ] || { echo "No $DISC_NAME on the disc (build/disc)" >&2; exit 1; }
      FOUND="$FOUND/$next"
    done
    SHA="$(sha256sum "$FOUND" | cut -d' ' -f1)"; INPUT_EXE="$EXE"
    ORIG_NAME="$(basename "$FOUND")"
    ;;
esac
if [ -n "$EXE" ] && [ -z "${DISC_NAME:-}" ]; then
  SRC="$EXE"; case "$EXE" in /*) ;; *) SRC="$DIR/$EXE" ;; esac
  [ -f "$SRC" ] || { echo "No such file: $SRC" >&2; exit 1; }
  SHA="$(sha256sum "$SRC" | cut -d' ' -f1)"; INPUT_EXE="$EXE"
  ORIG_NAME="$(basename "$SRC" | tr '[:lower:]' '[:upper:]')"
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
  [ "$PLATFORM" = win32 ] && printf '# key winapi (no = leave out the Win32 API layer) and runtime_skip (File.c,... = single runtime files): see README\n'
  true
} > game/game.cfg

printf '%s\n' "$SHA" > "game/recomp/$MODULE/default/sha256"
cat > "game/recomp/$MODULE/default/SR.cfg" <<EOF
# SR's configuration for the original. Empty = the built-in defaults;
# keys are added after the first run of SR, when something needs them.
EOF

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
# The targets (linux, windows, portable, apk, clean, ...) are SR-SDK/scons/game.mk's.
include SR-SDK/scons/game.mk
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
DATA_MARKER=$ORIG_NAME                    # a file the game directory (build/dist/portable) must hold"
launcher run-sdl.sh "Run the native port ($TITLE)." run_common.sh "$COMMON

run_native \"\$@\""
launcher run-sdl-wine.sh "Run the Windows build of the port ($TITLE) under Wine." run_common.sh "$COMMON

run_wine \"\$@\""
if [ "$PLATFORM" = dos ]; then
  launcher run-orig-dosbox.sh "Run the ORIGINAL $TITLE under DOSBox (the reference for the port)." run_orig_common.sh "ENVP=$ENVP
DOS_CMD=$ORIG_NAME$([ -n "$IMAGE" ] || echo '                              # TODO')

run_orig_dosbox \"\$@\""
else
  launcher run-orig-wine.sh "Run the ORIGINAL $TITLE under Wine (the reference for the port)." run_orig_common.sh "ENVP=$ENVP
WIN_EXE=$ORIG_NAME$([ -n "$IMAGE" ] || echo '                                  # TODO')

run_orig_wine \"\$@\""
fi

git init -q
git -c protocol.file.allow=always submodule add -q "$SDK_URL" SR-SDK
SDK_HEAD="$(git -C "$SDK_DIR" rev-parse HEAD)"
git -C SR-SDK fetch -q --no-recurse-submodules origin
git -C SR-SDK checkout -q --no-recurse-submodules "$SDK_HEAD"
[ -n "$SR_URL" ] || { [ -d "$SDK_URL/SR/.git" ] || [ -f "$SDK_URL/SR/.git" ]; } && SR_URL="${SR_URL:-$SDK_URL/SR}" || true
[ -z "$SR_URL" ] || git -C SR-SDK config submodule.SR.url "$SR_URL"
git -C SR-SDK -c protocol.file.allow=always submodule update -q --init --recursive
git -C SR-SDK/SR checkout -q master 2>/dev/null || true

echo "A new $PLATFORM port is in $DIR (SR-SDK at ${SDK_HEAD:0:7})."
if [ -n "${DISC_NAME:-}" ]; then
  echo "Next: scons stage=input (takes $DISC_NAME from the disc, build/disc)"
else
  echo "Next: $([ -n "$IMAGE" ] || echo 'image= of game/portable.cfg, ')input_exe= of game/game.cfg ($INPUT_EXE)$([ -n "$EXE" ] || echo ' and its sha256 in game/recomp/'"$MODULE"'/default/sha256'), then: scons stage=input"
fi
