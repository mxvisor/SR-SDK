#!/bin/bash
# Smoke test of a built port: starts it on an Xvfb of its own, lets it run, closes
# its window the way a window manager does (WM_DELETE_WINDOW) and checks that it
# exits by itself and leaves no process behind.
#
#   smoke.sh GAME linux|wine [RUN_SECS=15] [CLOSE_WAIT=10]
#
# linux runs build/bin/pc64-linux/<program>, wine runs build/bin/pc64-windows/<program>.exe
# in a Wine prefix of its own (~/.cache/<wine-prefix of games.tsv>, never ~/.wine).
# The game's data, write root and disc image are given through its env prefix
# ($<PREFIX>_DATA_DIR, _WRITE_ROOT, _CD_IMAGE); the write root is a scratch directory.
#
# Prints one line, TAB-separated:
#   smoke  GAME:MODE  ok|fail  alive=yes|no closed=yes|no rc=N killed=yes|no nonblank=1|0 leftovers=N
# ok means: still running after RUN_SECS, gone within CLOSE_WAIT of the close
# request without being killed, with exit status 0, no process left, and the screen was not blank (at the end of RUN_SECS, or within 12 s after it).
# rc is the game's exit status; a close by the user is a normal exit, so rc has to be 0.
#
# Environment: SMOKE_OUT (default $REGRESS_OUT/smoke) takes the log and the
# screenshot of the run: <game>-<mode>.log and .png.
. "$(dirname "$0")/lib.sh"
GAME=$1; MODE=$2; RUN_SECS=${3:-15}; CLOSE_WAIT=${4:-10}
[ -n "$GAME" ] && [ -n "$MODE" ] || { sed -n '2,/^$/p' "$0" | sed 's/^# \?//' >&2; exit 2; }
[ -n "$(game_field "$GAME" 2)" ] || { echo "unknown game: $GAME" >&2; exit 2; }
REPO=$(game_repo "$GAME"); PROG=$(game_field "$GAME" 3); P=$(game_field "$GAME" 4)
DATA="$ROOT/$(game_field "$GAME" 5)"; CUE=$(game_glob "$(game_field "$GAME" 6)"); PFX=$(game_field "$GAME" 7)
SMOKE_OUT=${SMOKE_OUT:-$OUT/smoke}; mkdir -p "$SMOKE_OUT"
LOG=$SMOKE_OUT/$GAME-$MODE.log; SHOT=$SMOKE_OUT/$GAME-$MODE.png
report() {   # STATUS DETAIL
    printf 'smoke\t%s:%s\t%s\t%s\n' "$GAME" "$MODE" "$1" "$2"
}
case $MODE in
    linux) BIN=$REPO/build/bin/pc64-linux/$PROG ;;
    wine)  BIN=$REPO/build/bin/pc64-windows/$PROG.exe ;;
    *) echo "mode is linux or wine" >&2; exit 2 ;;
esac
[ -f "$BIN" ] || { report fail "no binary: $BIN"; exit 0; }
[ $MODE = linux ] || command -v wine >/dev/null 2>&1 || { report fail "wine is not installed"; exit 0; }

WR=$(mktemp -d "$SMOKE_OUT/wr-$GAME-$MODE-XXXXXX")
TAG="smoke-$$-$GAME-$MODE"
DF=$(mktemp); Xvfb -displayfd 3 -screen 0 1024x768x24 3>"$DF" >/dev/null 2>&1 &
XPID=$!
for _ in $(seq 1 50); do [ -s "$DF" ] && break; sleep 0.1; done
[ -s "$DF" ] || { report fail "Xvfb did not start"; kill $XPID 2>/dev/null; exit 0; }
export DISPLAY=:$(head -n1 "$DF"); rm -f "$DF"
# without a WM; and never the real desktop: SDL would take Wayland first
unset WAYLAND_DISPLAY
export SDL_AUDIODRIVER=dummy

# SMOKE_TAG is only in the game's environment, to find what it leaves behind
ENVV=("SMOKE_TAG=$TAG" "${P}_DATA_DIR=$DATA" "${P}_WRITE_ROOT=$WR")
[ -n "$CUE" ] && ENVV+=("${P}_CD_IMAGE=$CUE")
cd "$WR"
if [ $MODE = linux ]; then
    env SDL_VIDEODRIVER=x11 "${ENVV[@]}" "$BIN" > "$LOG" 2>&1 &
else
    export WINEPREFIX=$HOME/.cache/$PFX WINEDEBUG=${WINEDEBUG:--all}
    . "$REGRESS_DIR/../wine_dummy_audio.sh"; wine_dummy_audio
    env WINEDLLOVERRIDES="mscoree=d;mshtml=d;winepulse.drv=d;winealsa.drv=d;wineoss.drv=d" "${ENVV[@]}" \
        wine "$BIN" > "$LOG" 2>&1 &
fi
GPID=$!

for _ in $(seq 1 $((RUN_SECS * 5))); do kill -0 $GPID 2>/dev/null || break; sleep 0.2; done
alive=no; kill -0 $GPID 2>/dev/null && alive=yes
# a screen that is black at that moment (a fade, a movie) is looked at again a few times
nonblank=0
for try in 1 2 3 4 5; do
    import -window root "$SHOT" 2>/dev/null
    nonblank=$(convert "$SHOT" -format '%[fx:maxima-minima>0.01?1:0]' info: 2>/dev/null || echo 0)
    [ "$nonblank" = 1 ] || [ $alive = no ] && break
    sleep 3
done
closed=no; killed=no
if [ $alive = yes ]; then
    python3 "$REGRESS_DIR/xclose.py" >/dev/null 2>&1
    for _ in $(seq 1 $((CLOSE_WAIT * 5))); do
        kill -0 $GPID 2>/dev/null || { closed=yes; break; }
        sleep 0.2
    done
    if [ $closed = no ]; then
        killed=yes; kill -TERM $GPID 2>/dev/null; sleep 2; kill -KILL $GPID 2>/dev/null
    fi
fi
wait $GPID 2>/dev/null; rc=$?

# what is left of this run: processes that have our tag in their environment.
# Wine's wineserver goes by itself a few seconds after the last client.
left() {
    n=0
    for p in /proc/[0-9]*; do
        [ -r "$p/environ" ] || continue
        if { tr '\0' '\n' < "$p/environ"; } 2>/dev/null | grep -qx "SMOKE_TAG=$TAG"; then
            case "$(cat "$p/comm" 2>/dev/null)" in wineserver) ;; *) n=$((n + 1)) ;; esac
        fi
    done
    echo $n
}
leftovers=$(left)
for _ in 1 2 3 4 5; do [ "$leftovers" = 0 ] && break; sleep 1; leftovers=$(left); done
[ $MODE = wine ] && wineserver -k 2>/dev/null
kill $XPID 2>/dev/null; wait $XPID 2>/dev/null
rm -rf "$WR"

status=fail
[ $alive = yes ] && [ $closed = yes ] && [ $killed = no ] && [ "$rc" = 0 ] && [ "$leftovers" = 0 ] && [ "$nonblank" = 1 ] && status=ok
report $status "alive=$alive closed=$closed rc=$rc killed=$killed nonblank=$nonblank leftovers=$leftovers"
