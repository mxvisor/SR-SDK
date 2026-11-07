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

"""The instructions around an address of a PE or LE executable, at the
addresses SR uses (loc_<address> of an error, of a .sci file, of a crash).

usage: exe_dis.py GAME.EXE ADDR [BEFORE [AFTER]]     (hex; 0x20 and 0x40 bytes)

The bytes before ADDR are decoded from the first start that lands on ADDR
itself, so the line of ADDR (marked >) is an instruction boundary.
"""

import os
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exe_image import load


def main(argv):
    if len(argv) not in (2, 3, 4):
        sys.exit(__doc__)
    img = load(argv[0])
    addr = int(argv[1], 16)
    before = int(argv[2], 16) if len(argv) > 2 else 0x20
    after = int(argv[3], 16) if len(argv) > 3 else 0x40
    if not img.bytes_at(addr, 1):
        sys.exit('%X: not in the image' % addr)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    start = addr
    for s in range(addr - before, addr):
        a = s
        for i in md.disasm(img.bytes_at(s, addr - s), s):
            a = i.address + i.size
        if a == addr:
            start = s
            break
    md.skipdata = True
    for i in md.disasm(img.bytes_at(start, addr - start + after + 15), start):
        if i.address >= addr + after:
            break
        print('%s%08X  %-20s %s %s' % ('>' if i.address == addr else ' ', i.address,
                                       i.bytes.hex(), i.mnemonic, i.op_str))


if __name__ == '__main__':
    main(sys.argv[1:])
