#!/bin/sh
# SR's and SRW's output for every reference game of SR, from given SR commits,
# as one hash per commit, game input and backend. Same hash = same output.
# SR is the DOS recompiler (SR-games/), SRW the Windows one (SRW-games/).
#
#   sr-regression.sh WORKDIR [COMMIT...]     (default: HEAD of the SR checkout)
#
# COMMIT is anything `git rev-parse` takes in the SR checkout, or `@worktree`:
# the checkout as it is on disk, with uncommitted changes (it is copied, the
# checkout is not touched).
#
# Environment:
#   SR_REPO    the SR checkout (default: SR/ of the SR-SDK this script is in)
#   REGRESS_CONF the workspace's regression data (default: regress/ beside SR-SDK, or
#              under $DOS_RECOMP_ROOT). Its sr-inputs.sh, sourced, gets the executables
#              of the reference games from the user's images into WORKDIR/exe and lists
#              them in INPUTS ("kind:SR-games or SRW-games directory|executable under
#              exe/|backends"); where the images are is its own business
#   SR_JOBS    jobs of the SR builds (default 8)
#   SR_ONLY    only the inputs whose "kind:dir" matches this grep pattern
#
# WORKDIR is scratch: the executables, the SR builds and the outputs go there,
# never into git. The executables come from the user's images (see sr-inputs.sh), which
# are only read.
#
# Each output is what the input's own build-<backend>.sh leaves, run as the
# README says (SR.exe or SRW.exe and the executable beside it), with the
# recompiler's messages kept in sr.out/sr.err instead of being deleted. The
# hash is over the generated .asm/.s/.llasm and .inc/.llinc files.
#
# Output: one line per commit, input and backend:
#   COMMIT <TAB> KIND <TAB> DIR <TAB> BACKEND <TAB> HASH
# (HASH is "(no build-BACKEND.sh)" for a backend the input has no script for)
# and, in WORKDIR/out-COMMIT/, the outputs themselves.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="$(cd "$HERE/../.." && pwd)"
SR_REPO="${SR_REPO:-$SDK/SR}"
WORK="$1"; shift || true
[ -n "$WORK" ] || { echo "usage: $0 WORKDIR [COMMIT...]" >&2; exit 2; }
[ $# -gt 0 ] || set -- HEAD

CONF="${REGRESS_CONF:-${DOS_RECOMP_ROOT:-$(dirname "$SDK")}/regress}"
[ -f "$CONF/sr-inputs.sh" ] || { echo "$0: no $CONF/sr-inputs.sh, set REGRESS_CONF" >&2; exit 2; }
SR_JOBS="${SR_JOBS:-8}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
EXE="$WORK/exe"
mkdir -p "$EXE"
. "$CONF/sr-inputs.sh"
[ -z "$SR_ONLY" ] || INPUTS=$(echo "$INPUTS" | grep -E -- "$SR_ONLY")

# the checkout's own tree of a commit: $WORK/sr-<rev>/{SR,SRW,SR-games,SRW-games}
checkout_tree() {   # COMMIT -> prints the tree path
    case "$1" in
        @worktree)
            tree="$WORK/sr-worktree"
            rm -rf "$tree"; mkdir -p "$tree"
            for p in SR SRW SR-games SRW-games; do
                [ -d "$SR_REPO/$p" ] && cp -a "$SR_REPO/$p" "$tree/"
            done ;;
        *)
            rev=$(git -C "$SR_REPO" rev-parse --short "$1")
            tree="$WORK/sr-$rev"
            if [ ! -d "$tree" ]; then
                mkdir -p "$tree"
                git -C "$SR_REPO" archive "$rev" SR SRW SR-games SRW-games | tar -x -C "$tree"
            fi ;;
    esac
    echo "$tree"
}

build_sr() {    # TREE KIND BACKEND -> $TREE/<SR|SRW>/<SR|SRW>.exe.<backend>
    [ -x "$1/$2/$2.exe.$3" ] && return 0
    case "$3" in
        x86) type=OUT_X86 ;; llasm) type=OUT_LLASM ;; arm) type=OUT_ARM_LINUX ;; x64) type=OUT_X64 ;;
    esac
    ( cd "$1/$2"
      sed -i "s/^#define OUTPUT_TYPE.*/#define OUTPUT_TYPE  $type/" SR_defs.h
      scons -c >/dev/null 2>&1 || true
      scons -Q -j"$SR_JOBS" > "../build-$2-$3.log" 2>&1
      mv "$2.exe" "$2.exe.$3" )
}

regen() {       # LABEL TREE KIND GAMEDIR EXEFILE BACKEND
    label=$1; tree=$2; kind=$3; gamedir=$4; exefile=$5; backend=$6
    case "$kind" in SR) games=SR-games ;; SRW) games=SRW-games ;; esac
    out="$WORK/out-$label/$kind-$(echo "$gamedir" | tr '/ ' '__')-$backend"
    rm -rf "$out"; mkdir -p "$out"
    ( cd "$tree/$games/$gamedir"
      if [ ! -f "build-$backend.sh" ]; then
          printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$kind" "$gamedir" "$backend" "(no build-$backend.sh)"; exit 0
      fi ) | grep . && return 0
    cp -a "$tree/$games/$gamedir/." "$out/"
    ( cd "$out"
      cp "$EXE/$exefile" .
      cp "$tree/$kind/$kind.exe.$backend" "$kind.exe"
      sed -i -e 's/>a\.a 2>b\.a/>sr.out 2>sr.err/' -e 's/^rm \*\.a$/rm -f *.a/' "build-$backend.sh"
      sh "./build-$backend.sh" || true
      rm -f "$kind.exe" "$(basename "$exefile")" ./*.o
      hash=$(find . -maxdepth 1 -type f \( -name '*.asm' -o -name '*.s' -o -name '*.llasm' -o -name '*.inc' -o -name '*.llinc' \) \
             -exec sha256sum {} + | sort -k2 | sha256sum | cut -c1-16)
      printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$kind" "$gamedir" "$backend" "$hash" )
}

for commit in "$@"; do
    tree=$(checkout_tree "$commit")
    label=$(basename "$tree" | sed 's/^sr-//')
    for kind in SR SRW; do
        for backend in llasm x86 arm x64; do
            echo "$INPUTS" | grep "^$kind:" | while IFS='|' read -r dir exefile backends; do
                case " $backends " in *" $backend "*) ;; *) continue ;; esac
                build_sr "$tree" "$kind" "$backend" || { echo "$label $kind $backend: SR build failed, see $tree/build-$kind-$backend.log" >&2; exit 1; }
                regen "$label" "$tree" "$kind" "${dir#*:}" "$exefile" "$backend"
            done
        done
    done
done
