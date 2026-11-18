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

"""Checks fixup_interpret_as_code.sci against SR's output: an address that the
list calls code, but that the code reads or writes as memory, is a table, not
code.  SR does what the list says: it translates the table as a procedure, and
the port's copy of the data then holds other bytes in its place (a table of
note periods in code, `mov ax, [ebx+loc_X]`, made the music play wrong notes).

For each label of the list, from the x86 lines SR keeps as comments in its
llasm (`;mov ax, [ebx+loc_X]`) and from its data (`daddr loc_X`):
  error    the label is inside a memory operand ([...]): data;
  warning  nothing jumps to it, calls it or takes its address (no jmp, call,
           jcc, daddr, nor the label as an immediate): nothing says it is code.
A label with code references and a memory operand too is an error all the
same (a table that a jump table also points at is rare; check it with
exe_dis.py).

usage (from a -recomp directory, after `scons stage=gen`):
  sr_codecheck.py [--sci FILE --llasm DIR] [--quiet]
defaults: every game/recomp/<module>/<variant>/llasm/fixup_interpret_as_code.sci
with build/gen/<module>/llasm.  Exit status 1 when there is an error."""
import argparse, glob, os, re, sys

LABEL = re.compile(r"\bloc_[0-9A-F]+\b")
MEMOP = re.compile(r"\[[^\]]*\]")
LEA = re.compile(r"^;lea\b")
CONTROL = re.compile(r"^;(?:ACTION_[A-Z_]+;)?(?:call|jmp|j[a-z]{1,3}|loop[a-z]*)\b(?:\s+(?:short|near|far|dword))*\s+(loc_[0-9A-F]+)")


def read_list(path):
    out = []
    for line in open(path):
        line = line.split(";", 1)[0].split("#", 1)[0].strip()
        if line:
            out.append(line.split(",")[0].strip())
    return out


def scan(llasm_dir):
    """label -> {'mem': [x86 lines], 'ctl': n, 'imm': n, 'daddr': n}"""
    refs = {}

    def get(label):
        return refs.setdefault(label, {"mem": [], "ctl": 0, "imm": 0, "daddr": 0})

    paths = sorted(glob.glob(os.path.join(llasm_dir, "*.llinc")) + glob.glob(os.path.join(llasm_dir, "*.llasm")))
    for path in paths:
        for line in open(path, errors="replace"):
            if line.startswith("daddr "):
                get(line.split()[1])["daddr"] += 1
                continue
            if not line.startswith(";"):
                continue
            line = line.rstrip("\n")
            if LEA.match(line):
                # lea takes the address of its operand: no memory is touched
                for label in LABEL.findall(line):
                    get(label)["imm"] += 1
                continue
            for operand in MEMOP.findall(line):
                for label in LABEL.findall(operand):
                    get(label)["mem"].append(line)
            m = CONTROL.match(line)
            for label in LABEL.findall(MEMOP.sub("[]", line)):
                get(label)["ctl" if m and label == m.group(1) else "imm"] += 1
    return refs


def check(sci, llasm_dir, quiet):
    labels = read_list(sci)
    refs = scan(llasm_dir)
    empty = {"mem": [], "ctl": 0, "imm": 0, "daddr": 0}
    errors = warnings = 0
    for label in labels:
        r = refs.get(label, empty)
        if r["mem"]:
            errors += 1
            print(f"{sci}: error: {label} is read or written as memory ({len(r['mem'])}x), a table, not code:")
            for line in r["mem"][:3]:
                print(f"    {line}")
        elif r["ctl"] == 0 and r["imm"] == 0 and r["daddr"] == 0:
            warnings += 1
            if not quiet:
                print(f"{sci}: warning: {label}: no jump, call or address of it in SR's output")
    print(f"{sci}: {len(labels)} labels, {errors} errors, {warnings} warnings")
    return errors


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("--sci", help="a fixup_interpret_as_code.sci")
    ap.add_argument("--llasm", help="SR's llasm output of that module (build/gen/<module>/llasm)")
    ap.add_argument("--quiet", action="store_true", help="errors only")
    a = ap.parse_args()
    if a.sci or a.llasm:
        if not (a.sci and a.llasm):
            ap.error("--sci and --llasm go together")
        pairs = [(a.sci, a.llasm)]
    else:
        pairs = []
        for sci in sorted(glob.glob("game/recomp/*/*/llasm/fixup_interpret_as_code.sci")):
            module = sci.split(os.sep)[2]
            pairs.append((sci, os.path.join("build", "gen", module, "llasm")))
        if not pairs:
            ap.error("no game/recomp/*/*/llasm/fixup_interpret_as_code.sci here: run it in a -recomp directory")
    errors = 0
    for sci, llasm in pairs:
        if not os.path.isdir(llasm):
            print(f"{llasm}: missing: run `scons stage=gen` first", file=sys.stderr)
            return 2
        errors += check(sci, llasm, a.quiet)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
