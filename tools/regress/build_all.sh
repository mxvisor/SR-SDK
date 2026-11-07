#!/bin/sh
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

# make linux, make windows and make distrib-data (the data of the smoke tests) in
# every game's repository, sequentially (they must never run at once: they share build/).
#
#   build_all.sh [-t linux,windows,distrib-data] [GAME...]      (GAME: ids of games.tsv)
#
# Prints one line per game and target, TAB-separated:
#   build  GAME:TARGET  ok|fail  rc=N secs=N log=PATH
# Logs go to $REGRESS_OUT/build-<game>-<target>.log. Exit status 1 if anything failed.
. "$(dirname "$0")/lib.sh"
targets="linux windows distrib-data"
if [ "$1" = -t ]; then targets=$(echo "$2" | tr ',' ' '); shift 2; fi
failed=0
for id in $(select_games "$@"); do
    repo=$(game_repo "$id")
    for target in $targets; do
        log="$OUT/build-$id-$target.log"
        start=$(date +%s)
        if make -C "$repo" "$target" TARGETS="$(game_target "$id")" > "$log" 2>&1; then rc=0; else rc=$?; fi
        secs=$(( $(date +%s) - start ))
        if [ "$rc" = 0 ]; then status=ok; else status=fail; failed=1; fi
        printf 'build\t%s:%s\t%s\trc=%s secs=%s log=%s\n' "$id" "$target" "$status" "$rc" "$secs" "$log"
    done
done
exit $failed
