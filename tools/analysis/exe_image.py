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

"""An executable as it lies in memory, for the tools of this directory.

load(path) gives a PeImage (PE32) or an LeImage (LE of DOS4GW, at the
addresses of its object table, the ones SR uses).  Both have .segments (the
(start, bytes, code?) of the sections or objects), .code (the (start, end) of
the executable ones), .bytes_at(va, n), .in_code(va), .u32(va).

PeImage also has .base (the image base) and .relocs (the HIGHLOW base
relocations: address of the field -> the address it holds).
"""

import struct


class Image:
    def bytes_at(self, va, n):
        for lo, data, _ in self.segments:
            if lo <= va < lo + len(data):
                return data[va - lo:va - lo + n]
        return b''

    def u32(self, va):
        return struct.unpack('<I', self.bytes_at(va, 4))[0]

    def in_code(self, va):
        return any(lo <= va < hi for lo, hi in self.code)


class PeImage(Image):
    def __init__(self, path):
        d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsize = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from('<I', d, opt + 28)[0]
        size = struct.unpack_from('<I', d, opt + 56)[0]
        hdrs = struct.unpack_from('<I', d, opt + 60)[0]
        reloc_rva, reloc_size = struct.unpack_from('<II', d, opt + 96 + 5 * 8)
        img = bytearray(size)
        img[:hdrs] = d[:hdrs]
        secs = []
        s = opt + optsize
        for _ in range(nsec):
            vs, va, rsz, rp = struct.unpack_from('<IIII', d, s + 8)
            chars = struct.unpack_from('<I', d, s + 36)[0]
            n = min(rsz, vs or rsz, len(img) - va)
            # uninitialized data (WATCOM's .bss: a raw size, no raw data) stays zero
            if not chars & 0x80 and rp:
                m = max(0, min(n, len(d) - rp))
                img[va:va + m] = d[rp:rp + m]
            secs.append((va, vs or rsz, bool(chars & 0x20000000)))
            s += 40
        self.img = bytes(img)
        self.segments = [(self.base + va, self.img[va:va + n], c) for va, n, c in secs]
        self.code = [(lo, lo + len(data)) for lo, data, c in self.segments if c]
        self.relocs = {}
        o, end = reloc_rva, reloc_rva + reloc_size
        while o + 8 <= end:
            page, bsize = struct.unpack_from('<II', self.img, o)
            if bsize < 8:
                break
            for k in range(o + 8, o + bsize, 2):
                e = struct.unpack_from('<H', self.img, k)[0]
                if e >> 12 == 3:
                    rva = page + (e & 0xFFF)
                    self.relocs[self.base + rva] = struct.unpack_from('<I', self.img, rva)[0]
            o += bsize

    def bytes_at(self, va, n):
        return self.img[va - self.base:va - self.base + n]


class LeImage(Image):
    """the objects' pages lie one after another in the file (as Watcom links them)"""
    def __init__(self, path):
        d = open(path, 'rb').read()
        mz = le_offset(d)
        lf = mz + struct.unpack_from('<I', d, mz + 0x3C)[0]
        pagesize, lastpage = struct.unpack_from('<II', d, lf + 0x28)
        ot, nobj = struct.unpack_from('<II', d, lf + 0x40)
        pos = mz + struct.unpack_from('<I', d, lf + 0x80)[0]   # from the MZ the LE belongs to
        self.segments = []
        for i in range(nobj):
            vs, rb, fl, _, np, _ = struct.unpack_from('<IIIIII', d, lf + ot + i * 24)
            n = np * pagesize
            if i == nobj - 1 and lastpage:
                n = n - pagesize + lastpage
            data = d[pos:pos + min(n, vs)].ljust(vs, b'\0')
            self.segments.append((rb, data, bool(fl & 4)))
            pos += np * pagesize
        self.code = [(lo, lo + len(data)) for lo, data, c in self.segments if c]


def le_offset(d):
    """the MZ whose e_lfanew names the LE header: the file's own, or for an
    executable bound to DOS/4G the one after the extender's BW blocks (each
    holds the file offset of the next header at 0x1C)"""
    pos = 0
    for _ in range(16):
        sig = d[pos:pos + 2]
        if sig == b'MZ':
            lf = pos + struct.unpack_from('<I', d, pos + 0x3C)[0]
            if d[lf:lf + 2] == b'LE':
                return pos
            last, pages = struct.unpack_from('<HH', d, pos + 2)
            pos += pages * 512 - (512 - last if last else 0)
        elif sig == b'BW':
            pos = struct.unpack_from('<I', d, pos + 0x1C)[0]
        else:
            break
    return None


def load(path):
    with open(path, 'rb') as f:
        d = f.read()
    sig = d[struct.unpack_from('<I', d, 0x3C)[0]:][:4]
    if sig == b'PE\0\0':
        return PeImage(path)
    if le_offset(d) is not None:
        return LeImage(path)
    raise ValueError('%s: neither PE nor LE' % path)
