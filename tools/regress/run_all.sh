#!/bin/bash
# One command that answers "did I break anything?": SR's output for the reference
# games, a build of every game, a smoke test of every port under Linux and Wine,
# several runs of each port at different load addresses (ASLR), and the hashes of
# the llasm output. The result is compared with ref/results.tsv,
# the reference run, and the differences are listed.
#
#   run_all.sh [options] [GAME...]          (GAME: ids of games.tsv; default all)
#
#   --only LIST      run only these checks, a comma list of: sr build smoke aslr llasm
#   --sr COMMIT      the SR commit to check (default HEAD of SR-SDK/SR; @worktree:
#                    the checkout with its uncommitted changes)
#   --run-secs N     how long a smoke test lets the game run (default 15)
#   --save-ref       write this run as the new reference (ref/results.tsv)
#   --no-wine        smoke tests under Linux only
#
# Needs: scons, make, MinGW-w64 and Wine (for the windows builds and tests), Xvfb,
# ImageMagick (import, convert), 7z, unrar, dosbox, nasm (see sr-regression.sh).
# Environment: REGRESS_OUT (reports and logs, default /tmp/sdk-regress), SR_IMAGES,
# DOS_RECOMP_ROOT (see lib.sh and sr-regression.sh).
#
# Results go to $REGRESS_OUT/results.tsv, TAB-separated:
#   CHECK  SUBJECT  STATUS  DETAIL
# CHECK is sr, build, smoke, aslr, llasm-gen or llasm-llvm; STATUS is ok, fail or
# changed. A row of the comparison is: same | REGRESSION | improved | CHANGED | new.
# Exit status: 1 if there is a REGRESSION or a CHANGED, else 0.
. "$(dirname "$0")/lib.sh"
ONLY="sr build smoke aslr llasm"; SR_COMMIT=HEAD; RUN_SECS=15; SAVE=0; WINE=1; GAMES=()
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY=$(echo "$2" | tr ',' ' '); shift ;;
        --sr) SR_COMMIT=$2; shift ;;
        --run-secs) RUN_SECS=$2; shift ;;
        --save-ref) SAVE=1 ;;
        --no-wine) WINE=0 ;;
        -h|--help) sed -n '2,/^[^#]/p' "$0" | grep '^#' | sed 's/^# \?//'; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) GAMES+=("$1") ;;
    esac
    shift
done
GAMES=($(select_games "${GAMES[@]}")) || exit 2
want() { case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
RESULTS="$OUT/results.tsv"; : > "$RESULTS"
add() { printf '%s\n' "$1" | tee -a "$RESULTS"; }

if want sr; then
    echo "## sr: $SR_COMMIT" >&2
    "$REGRESS_DIR/sr-regression.sh" "$OUT/sr" "$SR_COMMIT" | tee "$OUT/sr-hashes.txt" | while IFS=$'\t' read -r _ kind dir backend hash; do
        add "$(printf 'sr\t%s:%s:%s\tok\t%s' "$kind" "$dir" "$backend" "$hash")"
    done
fi

declare -A BUILT
if want build; then
    echo "## build: ${GAMES[*]}" >&2
    while IFS=$'\t' read -r kind subject status detail; do
        BUILT[$subject]=$status
        add "$(printf '%s\t%s\t%s\t%s' "$kind" "$subject" "$status" "$detail")"
    done < <("$REGRESS_DIR/build_all.sh" "${GAMES[@]}")
fi

if want smoke; then
    echo "## smoke: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        for mode in linux wine; do
            [ "$mode" = wine ] && [ "$WINE" = 0 ] && continue
            target=$([ $mode = wine ] && echo windows || echo linux)
            if [ "${BUILT[$id:$target]}" = fail ]; then
                add "$(printf 'smoke\t%s:%s\tfail\tbuild failed' "$id" "$mode")"
                continue
            fi
            "$REGRESS_DIR/smoke.sh" "$id" "$mode" "$RUN_SECS" | while IFS=$'\t' read -r kind subject status detail; do
                add "$(printf '%s\t%s\t%s\t%s' "$kind" "$subject" "$status" "$detail")"
            done
        done
    done
fi

if want aslr; then
    echo "## aslr: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        for mode in linux wine; do
            [ "$mode" = wine ] && [ "$WINE" = 0 ] && continue
            target=$([ $mode = wine ] && echo windows || echo linux)
            if [ "${BUILT[$id:$target]}" = fail ]; then
                add "$(printf 'aslr\t%s:%s\tfail\tbuild failed' "$id" "$mode")"
                continue
            fi
            "$REGRESS_DIR/aslr.sh" "$id" "$mode" 3 "$((RUN_SECS / 2 + 1))" | while IFS=$'\t' read -r kind subject status detail; do
                add "$(printf '%s\t%s\t%s\t%s' "$kind" "$subject" "$status" "$detail")"
            done
        done
    done
fi

if want llasm; then
    echo "## llasm: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        repo=$(game_repo "$id")
        if [ ! -d "$repo/build/gen" ]; then
            add "$(printf 'llasm-gen\t%s\tfail\tno build/gen' "$id")"
            continue
        fi
        "$REGRESS_DIR/llasm_hashes.py" "$repo" --ref "$REGRESS_DIR/ref/llasm-$id.txt" --id "$id" | while IFS= read -r line; do
            add "$line"
        done
    done
fi

# ---- comparison with the reference run
REF="$REGRESS_DIR/ref/results.tsv"
if [ "$SAVE" = 1 ]; then
    # a partial run (--only, GAME...) replaces only its own rows of the reference
    python3 - "$REF" "$RESULTS" <<'PY2'
import sys, os
ref, now = sys.argv[1:3]
def rows(path):
    d = {}
    if os.path.exists(path):
        for line in open(path):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 2:
                d[(f[0], f[1])] = line if line.endswith('\n') else line + '\n'
    return d
merged = rows(ref)
merged.update(rows(now))
with open(ref, 'w') as out:
    out.writelines(merged[k] for k in sorted(merged))
PY2
    want sr && cp "$OUT/sr-hashes.txt" "$REGRESS_DIR/ref/sr-hashes.txt"
    echo "reference saved: $REF" >&2
    exit 0
fi
[ -f "$REF" ] || { echo "no reference run ($REF); use --save-ref" >&2; exit 0; }
echo "## comparison with ref/results.tsv" >&2
python3 - "$REF" "$RESULTS" <<'PY'
import sys

def load(path):
    rows = {}
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 3:
            rows[(f[0], f[1])] = (f[2], f[3] if len(f) > 3 else '')
    return rows

ref, now = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for key in sorted(now):
    status, detail = now[key]
    if key not in ref:
        verdict = 'new'
    else:
        rstatus, rdetail = ref[key]
        if status == rstatus:
            # sr rows carry the output hash: a different hash is a change
            verdict = 'CHANGED' if key[0] == 'sr' and detail != rdetail else 'same'
        elif rstatus == 'ok':
            verdict = 'REGRESSION'
        elif status == 'ok':
            verdict = 'improved'
        else:
            verdict = 'CHANGED'
    if verdict in ('REGRESSION', 'CHANGED'):
        bad += 1
    if verdict != 'same':
        print('%-10s %-10s %-45s %-8s %s' % (verdict, key[0], key[1], status, detail))
total = len(now)
same = sum(1 for k in now if k in ref and now[k][0] == ref[k][0] and (k[0] != 'sr' or now[k][1] == ref[k][1]))
print('%d checks, %d same as the reference, %d need attention' % (total, same, bad))
sys.exit(1 if bad else 0)
PY
