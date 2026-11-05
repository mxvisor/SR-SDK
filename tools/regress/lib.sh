# Shared by the tools of tools/regress. Source it: . "$(dirname "$0")/lib.sh"
#
# Sets: REGRESS_DIR (this directory), SDK (the SR-SDK it is in), ROOT (the workspace
# root: $DOS_RECOMP_ROOT, or the parent of SDK), OUT (scratch and reports:
# $REGRESS_OUT, default $ROOT/cache/sdk-regress; TMPDIR is
# set to $ROOT/cache/tmp, so nothing of a run goes to /tmp).
# Functions: game_ids, game_field ID N, game_repo ID, game_target ID, game_stage_build ID,
# game_glob PATTERN.

REGRESS_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="$(cd "$REGRESS_DIR/../.." && pwd)"
ROOT="${DOS_RECOMP_ROOT:-$(dirname "$SDK")}"
OUT="${REGRESS_OUT:-$ROOT/cache/sdk-regress}"
export TMPDIR="$ROOT/cache/tmp"; mkdir -p "$TMPDIR"
GAMES_TSV="${GAMES_TSV:-$REGRESS_DIR/games.tsv}"
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
