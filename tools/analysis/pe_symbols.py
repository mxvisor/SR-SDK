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

"""The COFF symbol table a PE executable may have kept (a release linked
without stripping it): the names of its functions and variables.

usage: pe_symbols.py GAME.EXE
prints  <address> <section> <type> <name>  (external, static and label symbols)
"""

import struct
import sys


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    d = open(argv[0], 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    symptr, nsym = struct.unpack_from('<II', d, pe + 12)
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    if not symptr or not nsym:
        sys.exit('%s: no COFF symbol table' % argv[0])
    strtab = symptr + 18 * nsym
    i = 0
    while i < nsym:
        name, val, sec, typ, cls, naux = struct.unpack_from('<8sIhHBB', d, symptr + 18 * i)
        if name[:4] == b'\0\0\0\0':
            o = strtab + struct.unpack_from('<I', name, 4)[0]
            n = d[o:d.index(b'\0', o)].decode('latin1')
        else:
            n = name.rstrip(b'\0').decode('latin1')
        if sec > 0 and cls in (2, 3, 6):       # external, static, label
            print('%08x %d %02x %s' % (base + val, sec, typ, n))
        i += 1 + naux


if __name__ == '__main__':
    main(sys.argv[1:])
