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

"""Find code addresses in SR.exe output that are still interpreted as data.

Parses seg*.inc produced by the M-HT SR static recompiler and reports every
`loc_XXXX` inside the code sections that is the target of a call/jump or is
referenced by an absolute `dd loc_XXXX` pointer (jump tables, function-pointer
tables) while being emitted as raw data.

Usage: sr_cov.py SEG_SPEC [seg*...]

  SEG_SPEC   tab-separated file, one line per CODE section:
             start_addr  end_addr
  seg*...    the SR .inc files to scan.

Output:   fixup_interpret_as_code.sci candidate lines, sorted, deduplicated.
stderr:   iteration diagnostics (instruction count per code file).

Candidates are recovered purely from absolute `loc_` tokens inside
call/jmp instructions and dd-pointer lines -> independent of label tracking.
"""

import re
import sys

LABEL_RE = re.compile(r"^loc_([0-9A-Fa-f]+):")
TOKEN_RE = re.compile(r"loc_([0-9A-Fa-f]+)")

INSTR_OR_DD = re.compile(r"^\s*(call|jmp|j\w+)\s", re.I | re.M)
DD_LINE = re.compile(r"^\s*dd\s", re.M)

def parse_seg_spec(path):
    ranges = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            start, end = (int(x, 16) for x in line.split()[:2])
            ranges.append((start, end))
    return ranges

def in_code(addr, ranges):
    return any(s <= addr < e for s, e in ranges)

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    ranges = parse_seg_spec(sys.argv[1])
    inc_files = sys.argv[2:]

    call_targets = set()   # absolute address targets of call/jmp instructions
    dd_targets = set()     # absolute addresses referenced by `dd loc_XXXX`
    code_files = set()

    for fn in inc_files:
        with open(fn) as f:
            text = f.read()

        # is this file a code section file? (its label addresses fall in code ranges)
        labels = sorted({int(m, 16) for m in LABEL_RE.findall(text)})
        if labels and in_code(labels[0], ranges):
            code_files.add(fn)

        for m in LABEL_RE.finditer(text):
            pass  # (labels only used for the file classification above)

        # instruction lines: call/jmp/jXX
        lines = text.splitlines()
        idx = 0
        while idx < len(lines):
            line = lines[idx]
            if DD_LINE.match(line):
                for tok in TOKEN_RE.findall(line):
                    t = int(tok, 16)
                    if in_code(t, ranges):
                        dd_targets.add(t)
                idx += 1
                continue
            if INSTR_OR_DD.match(line):
                if re.match(r"^\s*(jmp|call|j\w+)", line, re.I):
                    for tok in TOKEN_RE.findall(line):
                        call_targets.add(int(tok, 16))
            idx += 1

    # instruction-count metric for code-sections files
    inst_total = 0
    noncode_total = 0
    for fn in sorted(code_files):
        with open(fn) as f:
            text = f.read()
        n_inst = 0
        n_other = 0
        for line in text.splitlines():
            s = line.strip()
            if not s or s.startswith(("%", ";")):
                continue
            if re.match(r"^(loc_|align|db|dw|dd\s|section|global)", s):
                n_other += 1
                continue
            n_inst += 1
        inst_total += n_inst
        noncode_total += n_other
        print("#   %-12s instructions=%-7d other-lines=%d" % (fn, n_inst, n_other),
              file=sys.stderr)

    tier1 = {a for a in call_targets if in_code(a, ranges)}
    tier2 = {a for a in dd_targets if a in call_targets}
    tier3 = {a for a in dd_targets if a not in call_targets}

    if tier1:
        print("# call/jmp targets (tier 1)")
        print("\n".join("loc_%X" % a for a in sorted(tier1)))
    if tier2:
        print("# dd pointers that are also call/jmp targets (tier 2, overlapping)")
        print("\n".join("loc_%X" % a for a in sorted(tier2)))
    if tier3:
        print("# dd pointers only (tier 3)")
        print("\n".join("loc_%X" % a for a in sorted(tier3)))

    print("# --- diagnostics ---", file=sys.stderr)
    print("# code files:       %s" % ", ".join(sorted(code_files)), file=sys.stderr)
    print("# call/jmp targets: %d" % len(tier1), file=sys.stderr)
    print("# dd targets:       %d (tier2 overlap %d, tier3-only %d)"
          % (len(dd_targets), len(tier2), len(tier3)), file=sys.stderr)
    print("# instructions (all code files): %d" % inst_total, file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())