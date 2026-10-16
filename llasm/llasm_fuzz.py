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

"""Differential fuzzing of llasm.py against the D build: random llasm programs
(all instruction forms, if/endif, calls, data segments, macros, defines, a share of
deliberate mistakes) must give the same stdout, stderr and exit status.

usage: llasm_fuzz.py [-n COUNT] [-s SEED] [-j N] [--keep DIR]
"""

import os
import random
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from llasmlib import tables as L

HERE = Path(__file__).resolve().parent
# the D build: $LLASM_D, else llasm beside this script (see README.md)
os.environ["LLASM_LEGACY_TEMPS"] = "1"  # llasm.d keeps no temp registers across calls
D_BIN = Path(os.environ.get("LLASM_D") or HERE / "llasm")
PORT = HERE / "llasm.py"

OPTION_SETS = [
    ["-p"],
    ["-inline-float", "-inline-float2", "-inline-idiv"],
    ["-O1", "-m64", "-inline-idiv", "-inline-float", "-inline-float2"],
    ["-O2", "-inline-idiv", "-inline-float", "-inline-float2"],
    ["-O2", "-pic", "-m64", "-inline-float", "-inline-float2", "-inline-idiv"],
    ["-O2", "-ptrofs", "-m64", "-inline-float", "-inline-float2", "-inline-idiv"],
    ["-O1", "-no-tail-calls", "-inline-float", "-inline-float2", "-inline-idiv"],
    ["-O2", "-no-tail-calls", "-pic", "-m64", "-inline-float", "-inline-float2", "-inline-idiv"],
]

ERR = 0.002
REGS = L.registers_base_list[:9] + L.registers_base_list[12:]
HIDDEN_REGS = ["st_top", "st_sw_cond", "st_cw"]


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.externs = ["ext_a", "ext_b"]
        self.dlabels = ["dl_one", "dl_two", "dl_three"]
        self.procs = ["p_main", "p_two", "p_three", "p_ext"]
        self.funcs = {
            "f_void": ("funcv", ["$"]),
            "f_ret": ("func", ["$", "x"]),
            "f_ptr": ("funcp", ["$", "*p", "x"]),
            "f_none": ("funcv", []),
        }
        self.defines = {
            "CONST_A": "16", "CONST_B": "0x20", "CONST_C": "CONST_A + 3", "CONST_D": "(CONST_B << 2) | 1",
            "ALIAS_E": "ext_a", "ALIAS_P": "p_two",
        }

    def const(self):
        r = self.r
        k = r.randrange(14)
        if k == 0:
            return str(r.choice([0, 1, 2, 3, 4, 7, 8, 255, 65535, -1, -2, 2147483647, 4294967295]))
        if k == 1:
            return "0x%x" % r.choice([0, 1, 0xff, 0x7fffffff, 0xffffffff, 0x80000000, 0x1234abcd])
        if k == 2:
            return r.choice(["CONST_A", "CONST_B", "CONST_C", "CONST_D"])
        if k == 3:
            return "%d %s %d" % (r.randrange(0, 100), r.choice("+-*&|^"), r.randrange(0, 100))
        if k == 4:
            return "(%d + %d) * %d" % (r.randrange(10), r.randrange(10), r.randrange(10))
        if k == 5:
            return "%d %s %d" % (r.randrange(1, 1000), r.choice(["<<", ">>"]), r.randrange(0, 8))
        if k == 6:
            return "~%d" % r.randrange(0, 1000)
        if k == 7:
            return "-%d + %d" % (r.randrange(0, 100), r.randrange(0, 200))
        if k == 8:
            return "CONST_%s %s 0x%x" % (r.choice("ABCD"), r.choice("+-*"), r.randrange(0, 255))
        if k == 9:
            return "((%d))" % r.randrange(0, 50)
        if k == 10 and r.random() < 0.05:
            return "%d - - %d" % (r.randrange(0, 50), r.randrange(0, 50))
        return str(r.randrange(0, 40))

    def reg(self):
        return self.r.choice(REGS)

    def regconst(self):
        return self.reg() if self.r.random() < 0.6 else self.const()

    def addr(self):
        r = self.r
        k = r.randrange(8)
        if k == 0:
            return r.choice(self.externs + ["ALIAS_E"])
        if k == 1:
            return r.choice(self.dlabels)
        if k == 2:
            return "%s[%s]" % (r.choice(self.dlabels + self.externs), self.const())
        if k == 3:
            return "%s[ %d ]" % (r.choice(self.dlabels), r.randrange(0, 40))
        return self.reg()

    def param(self, spec):
        r = self.r
        alts = spec.split("/")
        # a share of deliberate type mistakes
        if r.random() < ERR:
            alts = r.choice([["reg"], ["const"], ["procaddr"], ["externaddr"]])
        a = r.choice(alts)
        if a == "reg":
            return self.reg() if r.random() > ERR else r.choice(HIDDEN_REGS)
        if a == "const":
            return self.const() if r.random() > 0.5 else str(r.randrange(0, 8))
        if a == "procaddr":
            return r.choice(self.procs + ["ALIAS_P"])
        if a == "externaddr":
            return self.addr()
        if a == "addr":
            return r.choice(self.dlabels + self.externs)
        if a == "funcaddr":
            return r.choice(list(self.funcs))
        return "?"

    def instruction(self, line_spec):
        r = self.r
        name, _, rest = line_spec.partition(" ")
        if name == "call":
            fname = r.choice(list(self.funcs))
            args = []
            for p in self.funcs[fname][1]:
                args.append("$" if p == "$" else self.regconst())
            if r.random() < ERR:
                args = args[:-1] if args else ["$"]
            return "call %s %s" % (fname, ", ".join(args)) if args else "call %s" % fname
        if "[" in rest:
            rest = rest[:rest.index("[")]
        specs = [s.strip() for s in rest.split(",") if s.strip()]
        if specs and r.random() < ERR:
            specs = specs[:-1]
        params = [self.param(s) for s in specs]
        return name + (" " + ", ".join(params) if params else "")

    def proc_body(self, instrs, allow_float):
        r = self.r
        lines = []
        n = r.randrange(1, 25)
        usable = [i for i in instrs if not i.startswith(("tcall", "ctcall", "ifz", "ifnz", "endif"))]
        i = 0
        while i < n:
            k = r.random()
            if k < 0.10:
                # if block, sometimes static
                kind = r.choice(["ifz", "ifnz"])
                reg = r.choice(["tmp0", "tmp1", "eax", "ebx"])
                if r.random() < 0.5:
                    lines.append("mov %s, %s" % (reg, r.choice(["0", "1", "5"])))
                    if r.random() < 0.3:
                        lines.append("cmovslt %s, %s, %s, %s, %s" % (reg, r.choice(["1", "3"]), reg, r.choice(["0", "2"]), r.choice(["0", "9"])))
                lines.append("%s %s" % (kind, reg))
                for _ in range(r.randrange(0, 5)):
                    lines.append(self.instruction(r.choice(usable)))
                if r.random() < 1 - ERR:
                    lines.append("endif")
            else:
                lines.append(self.instruction(r.choice(usable)))
            i += 1
        t = r.random()
        if t < 0.35:
            lines.append("tcall %s" % r.choice(["eax", "tmp0"] + self.procs + self.externs))
        elif t < 0.5:
            lines.append("ctcall%s %s, %s" % (r.choice(["z", "nz"]), self.reg(), r.choice(self.procs)))
            lines.append("tcall %s" % r.choice(self.procs + self.externs))
        elif t < 0.55:
            lines.append("mov eax, 1")
        return lines

    def program(self):
        r = self.r
        out = []
        if r.random() < 0.9:
            for k, v in self.defines.items():
                out.append("define %s %s" % (k, v))
        for e in self.externs:
            out.append("extern %s" % e)
        for k, (kw, ps) in self.funcs.items():
            out.append("%s %s %s" % (kw, k, ", ".join(ps)))

        # macros
        macro_names = []
        for m in range(r.randrange(0, 4)):
            name = "MAC%d" % m
            params = r.sample(["a", "ab", "abc", "x", "xy", "val", "n"], r.randrange(0, 3))
            out.append("macro %s %s" % (name, ", ".join(params)))
            for _ in range(r.randrange(0, 4)):
                ln = "mov %s, %s" % (r.choice(["eax", "ebx", "tmp1"]), "\\" + params[0] if params else "5")
                if len(params) > 1 and r.random() < 0.5:
                    ln = "add eax, eax, \\" + params[1]
                out.append("    " + ln)
            out.append("endm")
            macro_names.append((name, len(params)))

        # data
        for d in range(r.randrange(1, 4)):
            kind = r.choice(["", "", "constant", "uninitialized"])
            out.append("datasegment seg%d %s" % (d, kind))
            labels = list(self.dlabels) if d == 0 else []
            if d == 0:
                for lb in labels:
                    out.append("dlabel %s %s" % (lb, "global" if r.random() < 0.3 else ""))
                    self._data(out, kind)
            else:
                out.append("dlabel seg_label%d" % d)
                self._data(out, kind)
            out.append("endd")

        # procedures
        instrs = list(L.instructions_base_list) + L.instructions_idiv_list + L.instructions_float_list + L.instructions_float2_list
        for pname in self.procs:
            if pname == "p_ext":
                out.append("proc p_ext external")
                continue
            attr = r.choice(["", "", "public", "global gname_" + pname])
            out.append("proc %s %s" % (pname, attr))
            body = self.proc_body(instrs, True)
            if r.random() < 0.8:
                body = ["mov %s, %s" % (t, r.choice(["0", "1"])) for t in L.registers_base_list[12:]] + body
            for ln in body:
                out.append("    " + ln if r.random() < 0.9 else "\t" + ln + "   ; comment")
                if macro_names and r.random() < 0.1:
                    m, np = r.choice(macro_names)
                    out.append("    %s %s" % (m, ", ".join(self.regconst() for _ in range(np))))
            out.append("endp")

        # a share of damaged programs
        if r.random() < 0.03:
            k = r.randrange(len(out))
            out[k] = r.choice(["", "bogus", out[k][:-1], "endp", "endd", out[k] + " extra", "datasegment", "proc", "extern"])
        text = "\n".join(out) + "\n"
        if r.random() < 0.2:
            text = text.replace("\n", "\r\n")
        return text

    def _data(self, out, kind):
        r = self.r
        for _ in range(r.randrange(1, 5)):
            if kind == "uninitialized":
                out.append("dskip %s" % r.choice(["1", "4", "16", "0x10", "100"] * 20 + ["0"]))
            else:
                t = r.randrange(6)
                if t == 0:
                    out.append("db %s" % r.choice(["0", "1", "255", "0xff", "0x10"] * 20 + ["256"]))
                elif t == 1:
                    out.append("db %d dup %s" % (r.randrange(1, 40), r.choice(["3", "2", "0x4"] * 20 + ["0"])))
                elif t == 2:
                    out.append("dalign %s" % r.choice(["1", "2", "4"] * 20 + ["8"]))
                elif t == 3:
                    out.append("daddr %s" % r.choice((self.procs + self.dlabels + ["ext_a", "dl_one+4", "dl_two-2"]) * 8 + ["dl_one + 3", "nothing"]))
                elif t == 4:
                    out.append("db %d" % r.randrange(0, 255))
                else:
                    out.append("db 7 dup 1")


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True)
    return r.returncode, r.stdout, r.stderr


def one(args):
    seed, keep = args
    rnd = random.Random(seed)
    text = Gen(rnd).program()
    opts = OPTION_SETS[seed % len(OPTION_SETS)]
    with tempfile.TemporaryDirectory(prefix="llasm-fuzz-") as td:
        f = Path(td) / "t.llasm"
        f.write_bytes(text.encode("latin-1"))
        d = run([str(D_BIN), *opts, "t.llasm"], td)
        p = run([sys.executable, str(PORT), *opts, "t.llasm"], td)
        # status 1: an uncaught exception, whose stack trace is runtime-specific;
        # status 82: llasm.d checks the daddr of each datasegment in the order of
        # its associative array, so of several bad ones it names any
        same = d[0] == p[0] and d[1] == p[1] and (d[2] == p[2] or (d[0] == p[0] and d[0] in (1, 82)))
        if not same and keep:
            Path(keep).mkdir(parents=True, exist_ok=True)
            (Path(keep) / ("seed%d.llasm" % seed)).write_bytes(text.encode("latin-1"))
        return seed, opts, same, d, p


def main(argv):
    n, seed0, jobs, keep = 300, 1, 4, None
    i = 1
    while i < len(argv):
        if argv[i] == "-n":
            n = int(argv[i + 1]); i += 2
        elif argv[i] == "-s":
            seed0 = int(argv[i + 1]); i += 2
        elif argv[i] == "-j":
            jobs = int(argv[i + 1]); i += 2
        elif argv[i] == "--keep":
            keep = argv[i + 1]; i += 2
        else:
            i += 1
    bad = 0
    ok_rc = {}
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for seed, opts, same, d, p in ex.map(one, [(s, keep) for s in range(seed0, seed0 + n)]):
            ok_rc[d[0]] = ok_rc.get(d[0], 0) + 1
            if not same:
                bad += 1
                print("DIFF seed=%d opts=%s rc d=%d py=%d stdout %d/%d" % (seed, " ".join(opts), d[0], p[0], len(d[1]), len(p[1])))
                if d[2] != p[2]:
                    print("   D :", d[2][:300].decode("latin-1").strip().replace("\n", " | "))
                    print("   py:", p[2][:300].decode("latin-1").strip().replace("\n", " | "))
    print("%d programs: %d differ; exit statuses of the D build: %s" % (n, bad, dict(sorted(ok_rc.items()))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
