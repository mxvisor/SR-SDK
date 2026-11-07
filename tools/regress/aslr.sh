#!/bin/bash
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

# Runs a built port several times in a row and records where its image was loaded.
# A position-independent port must keep working wherever the loader puts it
# (ASLR): every run has to be alive after RUN_SECS, and under Linux (PIE, ASLR of
# the kernel on) the base address must change between the runs.
#
#   aslr.sh GAME linux|wine [RUNS=4] [RUN_SECS=6]
#
# Prints one line, TAB-separated:
#   aslr  GAME:MODE  ok|fail  runs=N alive=N bases=N [first=0x... ]
# bases is the number of distinct load addresses of the program's image. Under
# Wine a fixed base is not an error (Wine loads a relocatable image at its
# preferred base when it is free), so only alive decides there.
# Same environment as smoke.sh (data, write root, Xvfb of its own, Wine prefix).
. "$(dirname "$0")/lib.sh"
GAME=$1; MODE=$2; RUNS=${3:-4}; RUN_SECS=${4:-6}
[ -n "$GAME" ] && [ -n "$MODE" ] || { sed -n '2,/^$/p' "$0" | sed 's/^# \?//' >&2; exit 2; }
[ -n "$(game_field "$GAME" 2)" ] || { echo "unknown game: $GAME" >&2; exit 2; }
REPO=$(game_repo "$GAME"); PROG=$(game_field "$GAME" 3); P=$(game_field "$GAME" 4)
DATA="$ROOT/$(game_field "$GAME" 5)"; CUE=$(game_glob "$(game_field "$GAME" 6)"); PFX=$(game_field "$GAME" 7)
report() { printf 'aslr\t%s:%s\t%s\t%s\n' "$GAME" "$MODE" "$1" "$2"; }
case $MODE in
    linux) BIN=$REPO/build/bin/pc64-linux/$PROG; NAME=$PROG ;;
    wine)  BIN=$REPO/build/bin/pc64-windows/$PROG.exe; NAME=$PROG.exe ;;
    *) echo "mode is linux or wine" >&2; exit 2 ;;
esac
[ -f "$BIN" ] || { report fail "no binary: $BIN"; exit 0; }
[ $MODE = linux ] || command -v wine >/dev/null 2>&1 || { report fail "wine is not installed"; exit 0; }

DF=$(mktemp); Xvfb -displayfd 3 -screen 0 1024x768x24 3>"$DF" >/dev/null 2>&1 &
XPID=$!
for _ in $(seq 1 50); do [ -s "$DF" ] && break; sleep 0.1; done
[ -s "$DF" ] || { report fail "Xvfb did not start"; kill $XPID 2>/dev/null; exit 0; }
export DISPLAY=:$(head -n1 "$DF"); rm -f "$DF"
# the Xvfb goes with the script, also when the script is stopped (Ctrl-C, a kill)
trap 'kill $XPID 2>/dev/null' EXIT
unset WAYLAND_DISPLAY
export SDL_AUDIODRIVER=dummy
if [ $MODE = wine ]; then
    export WINEPREFIX=$HOME/.cache/$PFX WINEDEBUG=${WINEDEBUG:--all}
    . "$REGRESS_DIR/../wine_dummy_audio.sh"; wine_dummy_audio
fi

alive=0; bases=()
for run in $(seq 1 "$RUNS"); do
    WR=$(mktemp -d "$OUT/aslr-wr-XXXXXX"); TAG="aslr-$$-$GAME-$MODE-$run"
    ENVV=("SMOKE_TAG=$TAG" "${P}_DATA_DIR=$DATA" "${P}_WRITE_ROOT=$WR")
    [ -n "$CUE" ] && ENVV+=("${P}_CD_IMAGE=$CUE")
    ( cd "$WR"
      if [ $MODE = linux ]; then
          exec env SDL_VIDEODRIVER=x11 "${ENVV[@]}" "$BIN" > "$WR/log" 2>&1
      else
          exec env WINEDLLOVERRIDES="mscoree=d;mshtml=d;winepulse.drv=d;winealsa.drv=d;wineoss.drv=d" \
              "${ENVV[@]}" wine "$BIN" > "$WR/log" 2>&1
      fi ) &
    GPID=$!
    for _ in $(seq 1 $((RUN_SECS * 5))); do kill -0 $GPID 2>/dev/null || break; sleep 0.2; done
    # the process that holds the image: the tagged one that maps it
    base=
    for p in /proc/[0-9]*; do
        { tr '\0' '\n' < "$p/environ"; } 2>/dev/null | grep -qx "SMOKE_TAG=$TAG" || continue
        b=$(grep -m1 -F "/$NAME" "$p/maps" 2>/dev/null | cut -d- -f1)
        [ -n "$b" ] && { base=$b; pid=$p; break; }
    done
    if [ -n "$base" ]; then
        alive=$((alive + 1)); bases+=("$base")
    fi
    # stop it: every process of this run
    for p in /proc/[0-9]*; do
        { tr '\0' '\n' < "$p/environ"; } 2>/dev/null | grep -qx "SMOKE_TAG=$TAG" && kill -KILL "${p#/proc/}" 2>/dev/null
    done
    # and the game itself: once its main thread has ended, its environ reads empty
    # and the tag is not found, while its other threads go on (wait would wait for ever)
    kill -KILL $GPID 2>/dev/null
    wait $GPID 2>/dev/null
    [ $MODE = wine ] && wineserver -k 2>/dev/null
    rm -rf "$WR"
done
kill $XPID 2>/dev/null; wait $XPID 2>/dev/null

distinct=$(printf '%s\n' "${bases[@]}" | sort -u | grep -c .)
status=fail
if [ "$alive" = "$RUNS" ]; then
    status=ok
    [ $MODE = linux ] && [ "$distinct" -lt 2 ] && status=fail
fi
report $status "runs=$RUNS alive=$alive bases=$distinct first=0x${bases[0]:-none}"
