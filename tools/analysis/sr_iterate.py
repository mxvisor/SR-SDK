#!/usr/bin/env python3
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

"""Iteratively grow fixup_interpret_as_code.sci for a SR-recompiled game.

Loop: run SR.exe, scan coverage (sr_cov.py), feed all recovered code
candidates back into fixup_interpret_as_code.sci, repeat until the
instruction count stops growing.

Usage: sr_iterate.py SECTIONS SR_EXE OUTDIR [--exe NAME] [--tier2]

OUTDIR holds the game's executable (default MAIN.EXE, or --exe NAME).

Strategy: tier 1 (call/jmp targets) first; `--tier2` then adds dd-pointer
targets. Each iteration writes the current sci to fixup_interpret_as_code.sci
in OUTDIR, runs SR there, and records statistics.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

import sr_cov

INSTR_RE = re.compile(r"^\s*(call|jmp|j\w+)\s", re.I)
DB_RE = re.compile(r"^\s*db\s")
DD_RE = re.compile(r"^\s*dd\s")

def instruction_lines(paths):
    n = 0
    for p in paths:
        with open(p) as f:
            for line in f:
                s = line.strip()
                if (s and not s.startswith(("%", ";"))
                        and not re.match(r"^(loc_|align|db|dw|dd\s|section|global)", s)):
                    n += 1
    return n

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sections")
    ap.add_argument("sr_exe")
    ap.add_argument("outdir")
    ap.add_argument("--exe", default="MAIN.EXE")
    ap.add_argument("--tier2", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    exe = os.path.join(args.outdir, args.exe)
    if not os.path.exists(exe):
        print("outdir must contain " + args.exe)
        return 1

    ranges = sr_cov.parse_seg_spec(args.sections)
    sci = os.path.join(args.outdir, "fixup_interpret_as_code.sci")
    known = set()
    prev = -1
    iteration = 0

    while True:
        iteration += 1
        subprocess.run([args.sr_exe, args.exe, "game.asm"],
                       cwd=args.outdir,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=True)
        incs = [os.path.join(args.outdir, f)
                for f in sorted(os.listdir(args.outdir))
                if re.fullmatch(r"seg\d+\.inc", f)]
        insts = instruction_lines(incs)
        print("iter %2d: %d instruction lines" % (iteration, insts))

        if insts == prev:
            print("converged after %d iterations" % iteration)
            break
        prev = insts
        if iteration > 40:
            print("stopping at iteration limit")
            break

        # rescan: candidates not yet in sci
        inc_names = [f for f in sorted(os.listdir(args.outdir))
                     if re.fullmatch(r"seg\d+\.inc", f)]
        new_batch = set()
        for fn in inc_names:
            with open(os.path.join(args.outdir, fn)) as f:
                text = f.read()
            for m in re.finditer(r"^\s*(?:call|jmp|j\w+)\s+.*?loc_([0-9A-Fa-f]+)", text, re.I | re.M):
                a = int(m.group(1), 16)
                if any(s <= a < e for s, e in ranges):
                    new_batch.add(a)
            if args.tier2:
                for m in re.finditer(r"^\s*dd\s+.*?loc_([0-9A-Fa-f]+)", text, re.M):
                    a = int(m.group(1), 16)
                    if any(s <= a < e for s, e in ranges):
                        new_batch.add(a)
        to_add = new_batch - known
        if not to_add:
            print("no new candidates, stopping")
            break
        known |= new_batch
        with open(sci, "w") as f:
            for a in sorted(known):
                f.write("loc_%X\n" % a)
        print("  +%d entries (total %d)" % (len(to_add), len(known)))

    return 0

if __name__ == "__main__":
    sys.exit(main())