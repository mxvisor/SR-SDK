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

# SR (SRW) crashes on a game's input: an -O0 -g build of the game's own copy of
# it (build/host) runs under gdb on what the build gave it (build/input,
# build/recomp), and prints the backtrace and cur_ofs (the address SR was at).
#
# usage: sr_gdb.sh REPO [MODULE] [ARCH]    (in ~/.cache/sr-gdb; MODULE Game-main, ARCH llasm)
# More gdb commands: SR_GDB_EX="-ex 'p/x some_var'".

set -e
REPO=$(cd "$1" && pwd)
MODULE=${2:-Game-main}
ARCH=${3:-llasm}
SRC=$(ls -d "$REPO"/build/host/*/sr-$ARCH | head -n 1)
INPUT=$(ls "$REPO"/build/input/$MODULE/$MODULE.* | head -n 1)
[ -d "$SRC" ] && [ -f "$INPUT" ] || { echo "no build/host/*/sr-$ARCH or build/input/$MODULE: build stage=recomp first" >&2; exit 1; }
D=${XDG_CACHE_HOME:-$HOME/.cache}/sr-gdb
rm -rf "$D"; mkdir -p "$D/run"; cp -r "$SRC" "$D/src"
cd "$D/src"
U=$(ls -d udis86-* | head -n 1)
rm -f *.o "$U"/libudis86/*.o
for f in *.c "$U"/libudis86/*.c; do gcc -g -O0 -c -DHAVE_STRING_H=1 -I"$U" "$f" -o "${f%.c}.o"; done
for f in *.cpp; do g++ -g -O0 -c -DHAVE_STRING_H=1 -I"$U" "$f" -o "${f%.cpp}.o"; done
g++ -o sr.dbg *.o "$U"/libudis86/*.o
cp "$REPO"/build/recomp/$MODULE/$ARCH/*.sci "$REPO"/build/recomp/$MODULE/$ARCH/SR.cfg "$D/run/" 2>/dev/null || true
cp "$REPO"/build/recomp/$MODULE/$ARCH/relocations.csv "$D/run/" 2>/dev/null || true
cd "$D/run"
eval gdb -q -batch -ex run -ex bt -ex "'p/x cur_ofs'" $SR_GDB_EX --args "$D/src/sr.dbg" "$INPUT" "$MODULE.asm" 2>&1 \
    | grep -v '^\[' | tail -n 20
