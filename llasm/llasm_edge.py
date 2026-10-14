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

"""Hand-written edge cases (error paths, include/macro handling, options) run through
both the D build and llasm.py; stdout, stderr and the exit status must be identical."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
# the D build: $LLASM_D, else llasm beside this script (see README.md)
D_BIN = Path(os.environ.get("LLASM_D") or HERE / "llasm")
PORT = HERE / "llasm.py"

P = "proc t\n{}\nendp\n"

# (name, files {name: text}, options, main file)
CASES = []


def case(name, text, opts=(), files=None):
    CASES.append((name, dict(files or {}, **{"t.llasm": text}), list(opts)))


case("empty", "")
case("missing-include", "include nothere.llinc\n")
case("include-no-name", "include\n")
case("recursive-include", "include a.llinc\n", files={"a.llinc": "include a.llinc\n"})
case("include-chain", "include a.llinc\nproc t\nret\nendp\n", files={"a.llinc": "include b.llinc\n", "b.llinc": "define X 5\n"})
case("include-subdir-I", "include z.llinc\n", ["-I", "sub"], files={"sub/z.llinc": "define Q 1\n"})
case("macro-no-name", "macro\nendm\n")
case("macro-dup", "macro A\nendm\nmacro A\nendm\n")
case("macro-keyword", "macro proc\nendm\n")
case("macro-instruction", "macro mov a, b\nendm\n")
case("macro-instruction-diff-params", "macro mov a\n  add eax, eax, 1\nendm\nproc t\nmov 5\nret\nendp\n")
case("macro-unterminated", "macro A\nmov eax, 1\n")
case("macro-endm-extra", "macro A\nendm junk\n")
case("macro-recursive", "macro A\nA\nendm\nproc t\nA\nendp\n")
case("macro-wrong-nparams", "macro A x, y\nendm\nA 1\n")
case("macro-params-prefix", "macro M a, ab, abc\nmov eax, \\a\nmov ebx, \\ab\nmov ecx, \\abc\nendm\nproc t\nM 1, 2, 3\nret\nendp\n")
case("macro-params-prefix-p", "macro M a, ab, abc\nmov eax, \\a\nmov ebx, \\ab\nmov ecx, \\abc\nendm\nproc t\nM 1, 2, 3\nret\nendp\n", ["-p"])
case("define-in-proc", "proc t\ndefine X 1\nendp\n")
case("define-noname", "define\n")
case("define-dup", "define A 1\ndefine A 2\n")
case("define-reg", "define eax 1\n")
case("define-novalue", "define A\n")
case("define-after-extern", "extern A\ndefine A 1\n")
case("extern-in-proc", "proc t\nextern A\nendp\n")
case("extern-noname", "extern\n")
case("extern-define-space", "define A b c\nextern A\n")
case("extern-dup", "extern A\nextern A\n")
case("extern-extra", "extern A B\n")
case("extern-eq-func", "func A\nextern A\n")
case("func-in-proc", "proc t\nfunc f\nendp\n")
case("func-noname", "func\n")
case("func-dup", "func f\nfunc f\n")
case("func-eq-define", "define f 1\nfunc f\n")
case("func-rename", "func f $, x\nproc t\ncall f $, 1\nret\nendp\n", ["-func-name", "f=g_renamed"])
case("func-rename-bad", "", ["-func-name", "f"])
case("func-rename-dup", "", ["-func-name", "f=g", "-func-name", "f=h"])
case("func-no-arg", "", ["-func-name"])
case("proc-in-proc", "proc a\nproc b\nendp\n")
case("proc-noname", "proc\n")
case("proc-alias-space", "define A b c\nproc A\nendp\n")
case("proc-dup", "proc a\nret\nendp\nproc a\nret\nendp\n")
case("proc-eq-extern", "extern a\nproc a\nendp\n")
case("proc-extra", "proc a junk\nendp\n")
case("proc-global-space", "proc a global x y\nret\nendp\n")
case("proc-global-bad", "proc a nonsense x\nendp\n")
case("proc-external", "proc a external\nproc t\ntcall a\nendp\n")
case("tcall-extern", "extern e\nproc t\ntcall e\nendp\n")
case("tcall-extern-m64", "extern e\nproc t\ntcall e\nendp\n", ["-m64", "-O"])
case("tcall-extern-pic", "extern e\nproc t\ntcall e\nendp\n", ["-pic", "-m64", "-O"])
case("tcall-extern-ptrofs", "extern e\nproc t\ntcall e\nendp\n", ["-ptrofs", "-m64", "-O2"])
case("tcall-extern-notail", "extern e\nproc t\ntcall e\nendp\n", ["-no-tail-calls"])
case("tcall-extern-ctcall", "extern e\nproc t\nctcallz eax, t\ntcall e\nendp\n", ["-O"])
case("tcall-extern-alias", "define E e\nextern e\nproc t\ntcall E\nendp\n")
case("tcall-undefined", "proc t\ntcall nothing\nendp\n")
case("proc-alias", "define AL real\nproc AL public\nmov eax, 1\nendp\n")
case("endp-outside", "endp\n")
case("endp-extra", "proc a\nendp junk\n")
case("instr-outside", "mov eax, 1\n")
case("instr-bad-params", "proc a\nmov eax\nendp\n")
case("instr-nonreg", "proc a\nmov 5, eax\nendp\n")
case("unterminated-proc", "proc a\nmov eax, 1\n")
case("empty-proc", "proc a\nendp\n")
case("unknown-word", "frobnicate\n")
case("ds-in-proc", "proc a\ndatasegment x\nendp\n")
case("ds-noname", "datasegment\n")
case("ds-dots", "datasegment a.b\ndlabel l\ndb 1\nendd\n")
case("ds-dup", "datasegment a\ndb 1\nendd\ndatasegment a\ndb 1\nendd\n")
case("ds-eq-proc", "proc a\nret\nendp\ndatasegment a\ndb 1\nendd\n")
case("ds-bad-attr", "datasegment a junk\nendd\n")
case("ds-constant", "datasegment a constant\ndlabel l\ndb 1\nendd\n")
case("ds-uninit", "datasegment a uninitialized\ndlabel l\ndskip 10\ndskip 3\nendd\n")
case("ds-uninit-db", "datasegment a uninitialized\ndb 1\nendd\n")
case("ds-uninit-daddr", "datasegment a uninitialized\ndaddr x\nendd\n")
case("ds-init-dskip", "datasegment a\ndskip 1\nendd\n")
case("ds-empty", "datasegment a\nendd\n")
case("ds-missing-endd", "datasegment a\ndb 1\n")
case("endd-outside", "endd\n")
case("endd-extra", "datasegment a\ndb 1\nendd junk\n")
case("dlabel-outside", "dlabel x\n")
case("dlabel-noname", "datasegment a\ndlabel\nendd\n")
case("dlabel-dup", "datasegment a\ndlabel x\ndb 1\ndlabel x\nendd\n")
case("dlabel-eq-proc", "proc x\nret\nendp\ndatasegment a\ndlabel x\ndb 1\nendd\n")
case("dlabel-bad-attr", "datasegment a\ndlabel x junk\ndb 1\nendd\n")
case("dalign-outside", "dalign 4\n")
case("dalign-noparam", "datasegment a\ndalign\nendd\n")
case("dalign-nan", "datasegment a\ndalign x\nendd\n")
case("dalign-extra", "datasegment a\ndalign 4 4\nendd\n")
case("dalign-bad", "datasegment a\ndalign 3\nendd\n")
case("dalign-ok", "datasegment a\ndb 1\ndalign 4\ndlabel l\ndb 2\ndalign 2\ndaddr t\ndalign 4\ndb 3 dup 3\nendd\nproc t\nret\nendp\n")
case("db-outside", "db 1\n")
case("db-noparam", "datasegment a\ndb\nendd\n")
case("db-nan", "datasegment a\ndb x\nendd\n")
case("db-big", "datasegment a\ndb 256\nendd\n")
case("db-neg", "datasegment a\ndb -1\nendd\n")
case("db-dup-nan", "datasegment a\ndb 1 dup x\nendd\n")
case("db-dup-zero", "datasegment a\ndb 1 dup 0\nendd\n")
case("db-dup-junk", "datasegment a\ndb 1 junk 3\nendd\n")
case("db-hex", "datasegment a\ndb 0xAb\ndb 0XAB\nendd\n")
case("db-dup-hex", "datasegment a\ndb 1 dup 0x10\nendd\n")
case("dinclude-ok", "datasegment a\ndinclude d.bin\ndb 1\ndinclude d.bin\nendd\n", files={"d.bin": "\x00\x01\xffabc"})
case("dinclude-missing", "datasegment a\ndinclude nothere.bin\nendd\n")
case("dinclude-noname", "datasegment a\ndinclude\nendd\n")
case("dinclude-empty", "datasegment a\ndinclude e.bin\ndb 1\nendd\n", files={"e.bin": ""})
case("daddr-noparam", "datasegment a\ndaddr\nendd\n")
case("daddr-extra", "datasegment a\ndaddr x y\nendd\n")
case("daddr-undefined", "datasegment a\ndaddr nothing\nendd\n")
case("daddr-offsets", "datasegment a\ndlabel l\ndaddr l+4\ndaddr l-2\ndaddr l+0\ndaddr -3\nendd\n")
case("daddr-define", "define D l+8\ndatasegment a\ndlabel l\ndaddr D\ndb 1\nendd\n")
case("daddr-proc-pic", "datasegment a\ndaddr t\ndlabel l\ndaddr l+4\nendd\nproc t\nret\nendp\n", ["-pic", "-m64"])
case("daddr-ptrofs", "datasegment a\ndaddr t\ndlabel l global\ndaddr l+4\nendd\nproc t\nret\nendp\n", ["-ptrofs", "-m64", "-O2"])
case("daddr-notail", "datasegment a constant\ndaddr t\nendd\nproc t\nret\nendp\n", ["-no-tail-calls"])
case("ctor-const", "datasegment a constant\ndaddr t\nendd\nproc t\nret\nendp\n", ["-pic", "-m64"])
case("no-input", "", ["-O1"])
case("bad-ptrofs", "", ["-ptrofs"])
case("two-inputs", "", ["t.llasm", "t.llasm"])
case("dispatcher", "proc t global c_t\nmov eax, 1\ntcall eax\nendp\nproc u global c_u\nmov eax, 2\nendp\n", ["-no-tail-calls", "-O1"])
case("returns", "proc t global c_t\nmov eax, 1\ntcall eax\nendp\nproc u global c_u\nmov eax, 2\ntcall t\nendp\n", ["-O2", "-m64", "-ptrofs"])
case("ctor-ptrofs", "datasegment a\ndaddr t\nendd\nproc t global c_t\nret\nendp\n", ["-ptrofs", "-m64", "-O2"])
case("hex-expr", "proc t\nmov eax, 0xFFFFFFFF + 1\nmov ebx, 0xfffffffff\nmov ecx, 0xg\nret\nendp\n", ["-O1"])
case("expr-ops", "proc t\nmov eax, 5 - 3 - 1\nmov ebx, 2 * 3 + 4\nmov ecx, 1 << 3 << 1\nmov edx, ~0\nmov esi, -5\nmov edi, 7 & 3 | 8 ^ 1\nret\nendp\n", ["-O2"])
case("expr-paren", "proc t\nmov eax, (1 + 2) * (3 + 4)\nmov ebx, ((5))\nmov ecx, (1\nmov edx, 1)\nret\nendp\n", ["-O2"])
case("expr-neg", "proc t\nmov eax, 3 - -2\nmov ebx, -(-1)\nmov ecx, - 5\nmov edx, ~ ~ 5\nret\nendp\n", ["-O2"])
case("expr-big", "proc t\nmov eax, 99999999999\nmov ebx, 9223372036854775807\nmov ecx, 9223372036854775808\nmov edx, 4294967295 * 4294967295\nret\nendp\n", ["-O2"])
case("shift-big", "proc t\nmov eax, 1 << 40\nmov ebx, 1 << 64\nmov ecx, -1 >> 1\nmov edx, 256 >> 4\nret\nendp\n", ["-O2"])
case("define-chain", "define A B\ndefine B C + 1\ndefine C 4\nproc t\nmov eax, A\nmov ebx, A * 2\nret\nendp\n", ["-O2"])
case("define-recursive", "define A B\ndefine B A\nproc t\nmov eax, A\nret\nendp\n")
case("define-ext", "extern e\ndefine E2 e\nproc t\nmov eax, E2\nmov ebx, E2[4]\nmov ecx, e[2 + 2]\nret\nendp\n", ["-O1"])
case("label-index-bad", "extern e\nproc t\nmov eax, e[x]\nret\nendp\n")
case("tabs-crlf", "define\tA\t5\r\nproc\tt\r\n\tmov\teax,\tA\t; c\r\n\tret\r\nendp\r\n", ["-O1"])
case("comments", "; only a comment\n   ;; another\nproc t ; trailing\n  mov eax, 1 ; x\nendp\n; end\n", ["-O2"])
case("no-trailing-newline", "proc t\nmov eax, 1\nendp", ["-O2"])
case("high-bytes", "proc t ; \xe9\xff\nmov eax, 1\nendp\n", ["-O1"])
case("static-if", "proc t\nmov eax, 0\nifz eax\nmov ebx, 1\nendif\nmov ecx, 5\nifnz ecx\nmov edx, 2\nendif\nmov esi, 1\ncmovslt esi, 1, esi, 5, 7\nifnz esi\nmov edi, 3\nendif\nret\nendp\n", ["-O1"])
case("static-if-O2", "proc t\nmov eax, 0\nifz eax\nmov ebx, 1\nendif\nmov ecx, 5\nifnz ecx\nmov edx, 2\nendif\nret\nendp\n", ["-O2"])
case("dyn-if-O2", "proc t\nmov eax, 1\nload ebx, eax, 4\nifz ebx\nmov ecx, 1\nendif\nadd edx, ecx, 1\nret\nendp\n", ["-O2"])
case("if-in-if", "proc t\nifz eax\nifz ebx\nendif\nendif\nendp\n")
case("endif-no-if", "proc t\nendif\nendp\n")
case("missing-endif", "proc t\nifz eax\nmov eax, 1\nendp\n")
case("after-tcall", "proc t\ntcall eax\nmov eax, 1\nendp\n")
case("after-ctcall", "proc t\nctcallz eax, t\nmov eax, 1\nendp\n")
case("uninit-tmp", "proc t\nadd eax, tmp0, 1\nendp\n", ["-O2"])
case("call-ok", "func f $, x\nfuncp g $, *p, x\nfuncv h $\nproc t\ncall f $, eax\ncall g $, ebx, 5\ncall h $\nret\nendp\n", ["-O2", "-m64", "-ptrofs"])
case("call-bad", "func f $, x\nproc t\ncall f eax, $\nendp\n")
case("call-nofunc", "proc t\ncall nothere $\nendp\n")
case("call-wrong-n", "func f $, x\nproc t\ncall f $\nendp\n")
case("call-noparams", "func f $\nproc t\ncall\nendp\n")
case("float-all", "proc t\nFLD1_VOID\nFLDZ_VOID\nFADD_ST 1\nFSTP_FLOAT\nFLDLN2_VOID\nFLDLG2_VOID\nFYL2X_VOID\nFSQRT_VOID\nFXCH_ST 1\nFCOMPP_VOID\nFNSTSW_VOID\nFNSTCW_VOID\nFLDCW_UINT16 5\nret\nendp\n", ["-O2", "-inline-float", "-inline-float2"])
case("float-O1", "proc t\nFLD1_VOID\nFLDZ_VOID\nFADD_ST 1\nFSTP_FLOAT\nFPOW_VOID\nFTAN_VOID\nFMOD_VOID\nFMODR_VOID\nFPOWR_VOID\nfld_x\nret\nendp\n", ["-O1", "-inline-float", "-inline-float2"])
case("div", "proc t\nDIV_64_ZE eax\nIDIV_64_SE 7\nret\nendp\n", ["-O2", "-inline-idiv"])
case("div-noinline", "proc t\nDIV_64_ZE eax\nendp\n")
case("misc-ops", "proc t\nctlz eax, ebx\nbswap eax, ebx\nimul eax, edx, ebx, ecx\numul eax, edx, ebx, 5\next8s eax, ebx\next16s eax, ebx\nins16 eax, eax, ebx\nins8ll eax, eax, ebx\nins8lh eax, eax, ebx\nins8hl eax, eax, ebx\nins8hh eax, eax, ebx\ncmovz eax, ebx, ecx, 5\ncmoveq eax, 1, ebx, 2, 3\ncmovult eax, 1, ebx, 2, 3\ncmovugt eax, 1, ebx, 2, 3\ncmovsgt eax, 1, ebx, 2, 3\nret\nendp\n", ["-O2"])
case("mem-ops", "extern e\ndatasegment a\ndlabel l\ndb 8 dup 0\nendd\nproc t\nload eax, e, 4\nload8s eax, l, 1\nload8z eax, l[2], 1\nload16s eax, l[2], 2\nload16z eax, ebx, 2\nstore eax, l, 4\nstore8 5, e, 1\nstore16 ebx, ebx, 2\nstore 7, l[4], 4\nmov eax, l\nmov eax, t\nmov ebx, e[3]\nret\nendp\n", ["-O2"])
case("mem-ops-pic64", "extern e\ndatasegment a\ndlabel l global\ndlabel m\ndb 8 dup 0\nendd\nproc t\nload eax, e, 4\nload8s eax, l, 1\nload8z eax, m[2], 1\nstore eax, m, 4\nmov eax, m\nmov eax, t\nret\nendp\n", ["-O1", "-pic", "-m64"])
case("mem-ops-ptrofs", "extern e\ndatasegment a\ndlabel l global\ndlabel m\ndb 8 dup 0\nendd\nproc t\nload eax, e, 4\nload8s eax, l, 1\nload8z eax, m[2], 1\nload16z ebx, ebx, 2\nstore eax, m, 4\nmov eax, m\nmov eax, t\ntcall eax\nendp\n", ["-O2", "-m64", "-ptrofs"])
case("mem-ops-ptrofs-O1", "extern e\ndatasegment a\ndlabel l global\ndlabel m\ndb 8 dup 0\nendd\nproc t\nload eax, e, 4\nload16z ebx, ebx, 2\nstore eax, m, 4\ntcall eax\nendp\n", ["-O1", "-m64", "-ptrofs"])
case("at-names", "proc _imp@12 external\nextern _var@4\nfunc _f@8 $, x\ndatasegment a\ndaddr _imp@12\ndlabel l@1\ndb 0 dup 4\nendd\nproc t global c@t\ncall _f@8 $, eax\nmov eax, _imp@12\nload ebx, _var@4, 4\nmov ecx, _var@4[8]\nmov edx, l@1\ntcall _imp@12\nendp\n", ["-O2"])
case("at-names-pic64", "proc _imp@12 external\nextern _var@4\nfunc _f@8 $, x\ndatasegment a\ndaddr _imp@12\ndlabel l@1 global\ndlabel m\ndb 0 dup 4\ndaddr m+2\nendd\nproc t global c@t\ncall _f@8 $, eax\nmov eax, _imp@12\nload ebx, _var@4, 4\nmov edx, m\ntcall _imp@12\nendp\n", ["-O1", "-pic", "-m64"])
case("at-names-notail", "proc _imp@12 external\nproc t global c@t\nmov eax, _imp@12\ntcall _imp@12\nendp\n", ["-no-tail-calls", "-O1"])
case("digit-names", "proc 1x external\nproc t\ntcall 1x\nendp\n", ["-O1"])
case("dash-names", "proc a-b external\nproc t\ntcall a-b\nendp\n", ["-O1"])


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True)
    return r.returncode, r.stdout, r.stderr


def main():
    bad = 0
    for name, files, opts in CASES:
        with tempfile.TemporaryDirectory(prefix="llasm-edge-") as td:
            for fname, text in files.items():
                path = Path(td) / fname
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(text.encode("latin-1"))
            args = opts if "t.llasm" in opts else [*opts, "t.llasm"] if "t.llasm" not in opts else opts
            if name in ("no-input", "bad-ptrofs", "func-rename-bad", "func-rename-dup", "func-no-arg"):
                args = opts
            d = run([str(D_BIN), *args], td)
            p = run([sys.executable, str(PORT), *args], td)
            # an uncaught exception prints a runtime-specific stack trace: only the status is comparable
            crashed = d[0] == 1 and p[0] == 1 and b"Exception" in d[2] and b"Traceback" in p[2]
            same = d[0] == p[0] and d[1] == p[1] and (d[2] == p[2] or crashed)
            if not same:
                bad += 1
                print(f"DIFF {name}: rc d={d[0]} py={p[0]}; stdout {len(d[1])}/{len(p[1])}")
                if d[2] != p[2]:
                    print("   D :", d[2][:300].decode("latin-1").strip().replace("\n", " | "))
                    print("   py:", p[2][:300].decode("latin-1").strip().replace("\n", " | "))
    print(f"{len(CASES)} edge cases: {bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
