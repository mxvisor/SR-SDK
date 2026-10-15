#!/usr/bin/env python3
"""One entry point for the llasm.py tests.

  tests/run.py          quick: units, edge cases, 300 fuzz programs, compare over the tree
  tests/run.py --full   as quick, with 2000 fuzz programs (before a commit)
  tests/run.py --units  only the unit tests (no D build needed)

The D build is $LLASM_D or llasm beside llasm.py (gdc -O2 ../SR/llasm/llasm.d -o llasm).
Exit status: 0 when every test passed.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LLASM = os.path.dirname(HERE)


def run(title, cmd):
    print("== " + title, flush=True)
    r = subprocess.run([sys.executable] + cmd, cwd=LLASM)
    if r.returncode != 0:
        print("FAILED: " + title)
    return r.returncode == 0


def main(argv):
    full = "--full" in argv
    ok = run("units", ["tests/test_units.py"])
    if "--units" not in argv:
        d = os.environ.get("LLASM_D") or os.path.join(LLASM, "llasm")
        if not os.path.exists(d):
            print("no D build: set LLASM_D or build llasm.d (see README.md)")
            return 2
        jobs = str(min(8, os.cpu_count() or 1))
        ok = run("edge cases", ["llasm_edge.py"]) and ok
        ok = run("fuzz", ["llasm_fuzz.py", "-n", "2000" if full else "300", "-j", jobs]) and ok
        ok = run("compare", ["llasm_compare.py", "-j", jobs]) and ok
    print("ALL PASSED" if ok else "SOME FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
