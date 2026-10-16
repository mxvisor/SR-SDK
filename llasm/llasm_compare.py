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

"""Compare llasm.py with the D build (llasm) over the .llasm files of the tree,
for several option sets: stdout, stderr and exit status must be identical.

usage: llasm_compare.py [-j N] [-I DIR]... [FILE...]

Without FILE, every .llasm under the repository; -I is passed to both.
"""

import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
# the D build: $LLASM_D, else llasm beside this script (see README.md)
os.environ["LLASM_LEGACY_TEMPS"] = "1"  # llasm.d keeps no temp registers across calls
D_BIN = Path(os.environ.get("LLASM_D") or HERE / "llasm")
PORT = HERE / "llasm.py"

OPTION_SETS = [
    ["-p"],
    [],
    ["-O1", "-m64", "-inline-idiv"],
    ["-O2", "-m64", "-inline-idiv", "-inline-float", "-inline-float2"],
    ["-O2", "-pic", "-m64"],
    ["-O1", "-ptrofs", "-m64", "-inline-float"],
    ["-O2", "-no-tail-calls", "-inline-float", "-inline-float2"],
]


def run(cmd, cwd):
    t = time.perf_counter()
    r = subprocess.run(cmd, cwd=cwd, capture_output=True)
    return r.returncode, r.stdout, r.stderr, time.perf_counter() - t


INCLUDES = []


def compare(path, opts):
    cwd = path.parent
    opts = [*opts, *INCLUDES]
    d = run([str(D_BIN), *opts, path.name], cwd)
    p = run([sys.executable, str(PORT), *opts, path.name], cwd)
    # D's uncaught exceptions print a stack trace and exit 1: only the status is comparable
    same = d[0] == p[0] and d[1] == p[1] and (d[2] == p[2] or (d[0] != 0 and p[0] != 0 and d[0] == 1))
    return path, opts, same, d, p


def main(argv):
    jobs = 4
    files = []
    i = 1
    while i < len(argv):
        if argv[i] == "-j":
            jobs = int(argv[i + 1])
            i += 2
        elif argv[i] == "-I":
            INCLUDES.extend(["-I", str(Path(argv[i + 1]).resolve())])
            i += 2
        else:
            files.append(Path(argv[i]).resolve())
            i += 1
    if not files:
        files = sorted(p for p in REPO.rglob("*.llasm") if "__pycache__" not in p.parts)
    files = [f for f in files if f.read_bytes()[:4] != b"\x7fELF" and b"\0" not in f.read_bytes()[:4096]]

    tasks = [(f, o) for f in files for o in OPTION_SETS]
    bad = 0
    t_d = t_p = 0.0
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for path, opts, same, d, p in ex.map(lambda a: compare(*a), tasks):
            t_d += d[3]
            t_p += p[3]
            if not same:
                bad += 1
                print(f"DIFF {path} {' '.join(opts) or '(no options)'}: rc d={d[0]} py={p[0]}, "
                      f"stdout {len(d[1])}/{len(p[1])} bytes, stderr {'same' if d[2] == p[2] else 'differs'}")
                if d[2] != p[2]:
                    print("   D :", d[2][:200].decode("latin-1").strip())
                    print("   py:", p[2][:200].decode("latin-1").strip())
    print(f"{len(tasks)} runs over {len(files)} files: {bad} differ; total time D {t_d:.1f}s, python {t_p:.1f}s")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
