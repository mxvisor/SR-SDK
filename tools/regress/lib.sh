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

# Shared by the tools of tools/regress. Source it: . "$(dirname "$0")/lib.sh"
#
# Sets: REGRESS_DIR (this directory), SDK (the SR-SDK it is in), ROOT (the workspace
# root: $DOS_RECOMP_ROOT, or the parent of SDK), CONF (the workspace's regression data:
# $REGRESS_CONF, or $ROOT/regress; games.tsv, images.tsv, markers.tsv, sr-inputs.sh, ref/;
# exported as REGRESS_CONF for the tools), OUT (scratch and reports:
# $REGRESS_OUT, default $ROOT/cache/sdk-regress; TMPDIR is
# set to $ROOT/cache/tmp, so nothing of a run goes to /tmp).
# Functions: game_ids, game_field ID N, game_repo ID, game_target ID, game_stage_build ID,
# game_glob PATTERN.

REGRESS_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="$(cd "$REGRESS_DIR/../.." && pwd)"
ROOT="${DOS_RECOMP_ROOT:-$(dirname "$SDK")}"
OUT="${REGRESS_OUT:-$ROOT/cache/sdk-regress}"
export TMPDIR="$ROOT/cache/tmp"; mkdir -p "$TMPDIR"
CONF="${REGRESS_CONF:-$ROOT/regress}"
[ -f "$CONF/games.tsv" ] || { echo "no regression data in $CONF (games.tsv): set REGRESS_CONF" >&2; exit 2; }
export REGRESS_CONF="$CONF"
GAMES_TSV="$CONF/games.tsv"
mkdir -p "$OUT"

game_ids() { awk -F'\t' '!/^#/ && NF >= 2 { print $1 }' "$GAMES_TSV"; }
game_field() { awk -F'\t' -v id="$1" -v n="$2" '!/^#/ && $1 == id { print $n }' "$GAMES_TSV"; }
game_repo() { echo "$ROOT/$(game_field "$1" 2)"; }
# the target of a repository of several (column 10, scons/game.py), default the main one
game_target() { local t; t=$(game_field "$1" 10); echo "${t:-default}"; }
# where the stages up to obj of the game's target are
game_stage_build() {
    local t; t=$(game_target "$1")
    if [ "$t" = default ]; then echo "$(game_repo "$1")/build"; else echo "$(game_repo "$1")/build/target-$t"; fi
}
# the first file a glob (relative to ROOT) matches, or nothing
game_glob() { [ "$1" = - ] && return 0; for f in $ROOT/$1; do [ -e "$f" ] && { echo "$f"; return 0; }; done; return 0; }
# the ids the arguments name, or all of them
select_games() {
    if [ $# -eq 0 ]; then game_ids; return; fi
    for id in "$@"; do
        [ -n "$(game_field "$id" 2)" ] || { echo "unknown game: $id" >&2; return 1; }
        echo "$id"
    done
}
