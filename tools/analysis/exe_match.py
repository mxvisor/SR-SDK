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

"""The same library code in another executable: the functions of KNOWN.EXE at
ADDR... are looked for in GAME.EXE (PE or LE, either) by their shape -- the
first N instructions, mnemonics and small operands, with the addresses and the
targets of calls and jumps masked.  A routine of the C library, of the Miles
AIL or of the extender found in a game already ported is so found in the next.

usage: exe_match.py [-n N] KNOWN.EXE GAME.EXE ADDR...     (hex; N 14)
prints  <addr in KNOWN> -> <addrs in GAME>, then the addresses without a match
"""

import os
import re
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exe_image import load

md = Cs(CS_ARCH_X86, CS_MODE_32)
NUM = re.compile(r'0x[0-9a-f]{5,}')


def shape(data, off, n):
    out = []
    for i in md.disasm(data[off:off + 16 * n], 0):
        if i.mnemonic in ('call', 'jmp') or (i.mnemonic[0] == 'j' and i.size > 2):
            out.append(i.mnemonic)
        else:
            out.append(i.mnemonic + ' ' + NUM.sub('X', i.op_str))
        if len(out) == n:
            return tuple(out)
    return None


def main(argv):
    n = 14
    if argv[:1] == ['-n']:
        n = int(argv[1])
        argv = argv[2:]
    if len(argv) < 3:
        sys.exit(__doc__)
    known, game = load(argv[0]), load(argv[1])
    wanted = {}
    for a in (int(x, 16) for x in argv[2:]):
        k = shape(known.bytes_at(a, 16 * n), 0, n)
        if k:
            wanted.setdefault(k, []).append(a)
    found = {}
    for lo, data, code in game.segments:
        if not code:
            continue
        for off in range(len(data) - 16):
            k = shape(data, off, n)
            if k in wanted:
                for a in wanted[k]:
                    found.setdefault(a, []).append(lo + off)
    for a in sorted(found):
        print('%x -> %s' % (a, ' '.join('%x' % x for x in found[a])))
    print('unmatched:', ' '.join('%x' % a for v in wanted.values() for a in v if a not in found))


if __name__ == '__main__':
    main(sys.argv[1:])
