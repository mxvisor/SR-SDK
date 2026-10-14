# Shared by the tools of tools/regress. Source it: . "$(dirname "$0")/lib.sh"
#
# Sets: REGRESS_DIR (this directory), SDK (the SR-SDK it is in), ROOT (the workspace
# root: $DOS_RECOMP_ROOT, or the parent of SDK), OUT (scratch and reports:
# $REGRESS_OUT, default ${TMPDIR:-/tmp}/sdk-regress).
# Functions: game_ids, game_field ID N, game_repo ID, game_glob PATTERN.

REGRESS_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="$(cd "$REGRESS_DIR/../.." && pwd)"
ROOT="${DOS_RECOMP_ROOT:-$(dirname "$SDK")}"
OUT="${REGRESS_OUT:-${TMPDIR:-/tmp}/sdk-regress}"
GAMES_TSV="${GAMES_TSV:-$REGRESS_DIR/games.tsv}"
mkdir -p "$OUT"

game_ids() { awk -F'\t' '!/^#/ && NF >= 2 { print $1 }' "$GAMES_TSV"; }
game_field() { awk -F'\t' -v id="$1" -v n="$2" '!/^#/ && $1 == id { print $n }' "$GAMES_TSV"; }
game_repo() { echo "$ROOT/$(game_field "$1" 2)"; }
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
