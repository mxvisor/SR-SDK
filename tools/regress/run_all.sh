#!/bin/bash
# One command that answers "did I break anything?": SR's output for the reference
# games, a build of every game, a smoke test of every port under Linux and Wine,
# several runs of each port at different load addresses (ASLR), the hashes of
# the llasm output, the conversion of the disc images (tools/image), and the names and the layout of the
# game repositories. The result is compared with ref/results.tsv,
# the reference run, and the differences are listed.
#
#   run_all.sh [options] [GAME...]          (GAME: ids of games.tsv; default all)
#
#   --only LIST      run only these checks, a comma list of: sr build smoke d3d aslr llasm image prefixes layout
#   --sr COMMIT      the SR commit to check (default HEAD of SR-SDK/SR; @worktree:
#                    the checkout with its uncommitted changes)
#   --run-secs N     how long a smoke test lets the game run (default 15)
#   --save-ref       write this run as the new reference (ref/results.tsv)
#   --jobs N         smoke and aslr tests run N at a time (default 4)
#   --no-wine        smoke tests under Linux only
#
# Needs: scons, make, MinGW-w64 and Wine (for the windows builds and tests), Xvfb,
# ImageMagick (import, convert), 7z, unrar, dosbox, nasm (see sr-regression.sh).
# Environment: REGRESS_OUT (reports and logs, default <workspace>/cache/sdk-regress), SR_IMAGES,
# DOS_RECOMP_ROOT (see lib.sh and sr-regression.sh).
#
# Results go to $REGRESS_OUT/results.tsv, TAB-separated:
#   CHECK  SUBJECT  STATUS  DETAIL
# CHECK is sr, build, smoke, d3d, aslr, llasm-gen, llasm-llvm, image, prefixes or layout; STATUS is ok, fail or
# changed. A row of the comparison is: same | REGRESSION | improved | CHANGED | new.
# Exit status: 1 if there is a REGRESSION or a CHANGED, else 0.
. "$(dirname "$0")/lib.sh"
ONLY="sr build smoke aslr llasm image prefixes layout d3d"; SR_COMMIT=HEAD; RUN_SECS=15; SAVE=0; WINE=1; JOBS=4; GAMES=()
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY=$(echo "$2" | tr ',' ' '); shift ;;
        --sr) SR_COMMIT=$2; shift ;;
        --run-secs) RUN_SECS=$2; shift ;;
        --save-ref) SAVE=1 ;;
        --no-wine) WINE=0 ;;
        --jobs) JOBS=$2; shift ;;
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

# the seconds a game is left running: RUN_SECS, or the game's own (column 8 of
# games.tsv) when that is longer
game_secs() {
    local own; own=$(game_field "$1" 8)
    if [ -n "$own" ] && [ "$own" -gt "$RUN_SECS" ]; then echo "$own"; else echo "$RUN_SECS"; fi
}

# one check of every game and mode, JOBS at a time (each has its own Xvfb and Wine prefix);
# the rows are added in the order of games and modes
par_checks() {   # KIND SCRIPT: its arguments after GAME MODE come from the function KIND_args
    local kind=$1 script=$2 id mode target lock i
    local -a tags=()
    local dir; dir=$(mktemp -d "$OUT/par-$kind-XXXXXX")
    for id in "${GAMES[@]}"; do
        for mode in linux wine; do
            [ "$mode" = wine ] && [ "$WINE" = 0 ] && continue
            target=$([ $mode = wine ] && echo windows || echo linux)
            tags+=("$id:$mode")
            if [ "${BUILT[$id:$target]}" = fail ]; then
                printf '%s\t%s:%s\tfail\tbuild failed\n' "$kind" "$id" "$mode" > "$dir/$id:$mode"
                continue
            fi
            while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.5; done
            # games that share a Wine prefix (and its wineserver) run one after another
            lock=/dev/null; [ $mode = wine ] && lock="$dir/lock-$(game_field "$id" 7)"
            if [ $mode = wine ]; then
                flock "$lock" "$REGRESS_DIR/$script" "$id" "$mode" $(${kind}_args "$id" "$mode") > "$dir/$id:$mode" &
            else
                "$REGRESS_DIR/$script" "$id" "$mode" $(${kind}_args "$id" "$mode") > "$dir/$id:$mode" &
            fi
        done
    done
    wait
    for i in "${tags[@]}"; do
        while IFS=$'\t' read -r k subject status detail; do
            add "$(printf '%s\t%s\t%s\t%s' "$k" "$subject" "$status" "$detail")"
        done < "$dir/$i"
    done
    rm -rf "$dir"
}
smoke_args() { game_secs "$1"; }
# Wine maps the image at one address, a second run adds nothing there
aslr_args() { if [ "$2" = wine ]; then echo "1 $(( $(game_secs "$1") / 2 + 1))"; else echo "3 $(( $(game_secs "$1") / 2 + 1))"; fi; }

if want smoke; then
    echo "## smoke: ${GAMES[*]}" >&2
    par_checks smoke smoke.sh
fi

# the renderer variants of a game (column 9 of games.tsv: label:VAR=value,VAR=value ...),
# a smoke test of each, one after another
if want d3d; then
    echo "## d3d: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        for v in $(game_field "$id" 9); do
            for mode in linux wine; do
                [ "$mode" = wine ] && [ "$WINE" = 0 ] && continue
                target=$([ $mode = wine ] && echo windows || echo linux)
                [ "${BUILT[$id:$target]}" = fail ] && continue
                add "$(SMOKE_KIND=d3d SMOKE_LABEL=${v%%:*} SMOKE_EXTRA="$(echo "${v#*:}" | tr ',' ' ')" \
                    "$REGRESS_DIR/smoke.sh" "$id" $mode $(smoke_args "$id"))"
            done
        done
    done
fi

if want aslr; then
    echo "## aslr: ${GAMES[*]}" >&2
    par_checks aslr aslr.sh
fi

if want llasm; then
    echo "## llasm: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        repo=$(game_repo "$id")
        if [ ! -d "$(game_stage_build "$id")/gen" ]; then
            add "$(printf 'llasm-gen\t%s\tfail\tno build/gen' "$id")"
            continue
        fi
        # --save-ref renews the listing too: its old verdict is only in the log
        renew=(); [ "$SAVE" = 1 ] && renew=(--renew)
        "$REGRESS_DIR/llasm_hashes.py" "$repo" --target "$(game_target "$id")" --ref "$REGRESS_DIR/ref/llasm-$id.txt" --id "$id" "${renew[@]}" | while IFS= read -r line; do
            add "$line"
        done
    done
fi

if want image; then
    echo "## image: ${GAMES[*]}" >&2
    "$REGRESS_DIR/image_check.py" "${GAMES[@]}" | while IFS=$'\t' read -r kind subject status detail; do
        add "$(printf '%s\t%s\t%s\t%s' "$kind" "$subject" "$status" "$detail")"
    done
fi

if want prefixes; then
    echo "## prefixes: ${GAMES[*]} sdk" >&2
    for id in "${GAMES[@]}" sdk; do
        out=$("$REGRESS_DIR/check_prefixes.py" "$id") && st=ok || st=fail
        [ "$st" = fail ] && printf '%s\n' "$out" | sed 's/^/    /' >&2
        add "$(printf 'prefixes\t%s\t%s\t%s' "$id" "$st" "$(printf '%s\n' "$out" | tail -1)")"
    done
fi

if want layout; then
    echo "## layout: ${GAMES[*]}" >&2
    for id in "${GAMES[@]}"; do
        out=$("$REGRESS_DIR/check_layout.py" "$id") && st=ok || st=fail
        [ "$st" = fail ] && printf '%s\n' "$out" | sed 's/^/    /' >&2
        add "$(printf 'layout\t%s\t%s\t%s' "$id" "$st" "$(printf '%s\n' "$out" | tail -1)")"
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
python3 - "$REF" "$RESULTS" "$ONLY" "${GAMES[*]}" "$WINE" <<'PY'
import sys

def load(path):
    rows = {}
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 3:
            rows[(f[0], f[1])] = (f[2], f[3] if len(f) > 3 else '')
    return rows

ref, now = load(sys.argv[1]), load(sys.argv[2])
only, games, wine = set(sys.argv[3].split()), set(sys.argv[4].split()), sys.argv[5] == '1'

def expected(key):
    # a row of the reference this run should have made too: of a check it ran, for a
    # game it ran (sr: not per game; prefixes: also the SDK), not of Wine with --no-wine
    kind, subject = key
    if kind.split('-')[0] not in only or (not wine and subject.endswith(':wine')):
        return False
    return kind == 'sr' or subject.split(':')[0] in games or (kind == 'prefixes' and subject == 'sdk')

def unchanged(key):
    # sr and image rows carry the output hash: a different hash is a change; a llasm row
    # says changed against the listing of ref/, which --save-ref renews: changed is a change
    if key[0] in ('sr', 'image'):
        return now[key][1] == ref[key][1]
    return not (key[0].startswith('llasm-') and now[key][0] == 'changed')

bad = 0
for key in sorted(now):
    status, detail = now[key]
    if key not in ref:
        verdict = 'new'
    else:
        rstatus, rdetail = ref[key]
        if status == rstatus:
            verdict = 'same' if unchanged(key) else 'CHANGED'
        elif key[0].startswith('llasm-') and status == 'changed':
            verdict = 'CHANGED'
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
for key in sorted(ref):
    if key not in now and expected(key):
        bad += 1
        print('%-10s %-10s %-45s %-8s %s' % ('MISSING', key[0], key[1], '-', 'no result in this run'))
total = len(now)
same = sum(1 for k in now if k in ref and now[k][0] == ref[k][0] and unchanged(k))
print('%d checks, %d same as the reference, %d need attention' % (total, same, bad))
sys.exit(1 if bad else 0)
PY
