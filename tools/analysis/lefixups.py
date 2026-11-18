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

"""Dump the LE fixup (relocation) records of a DOS4GW executable.

Mirrors the fixup walk in SR/SR/SR_loader.c, so the addresses SR complains
about ("output not found", "unknown flags ...") can be traced back to the
relocation that produced them.

Usage: lefixups.py EXE [START [END]]
       prints one line per fixup whose *target* lies in [START, END)
       (or every fixup when no range is given).
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leparse

FT = {2: "FT_SEGMENT", 5: "FT_16BITOFS", 6: "FT_SEGOFS32", 7: "FT_NORMAL",
      8: "FT_SELFREL", 19: "FT_SEGOFS16"}


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def i16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def i32(d, o):
    return struct.unpack_from("<i", d, o)[0]


class Fixup:
    __slots__ = ("ssec", "sadr", "tsec", "tadr", "ftype")

    def __init__(self, ssec, sadr, tsec, tadr, ftype):
        self.ssec, self.sadr, self.tsec, self.tadr, self.ftype = ssec, sadr, tsec, tadr, ftype


def fixups(exe_path):
    """Every relocation of the executable, in table order."""
    d = leparse.le_data(exe_path)
    lf = u32(d, 0x3C)
    hdr = leparse.read_header(d, lf)
    pagesize = hdr["PageSize"]
    numobjs = hdr["NumObjects"]
    objtab = lf + hdr["ObjectTableOffset"]
    fixpage = lf + hdr["FixupPageTableOffset"]
    fixrec = lf + hdr["FixupRecordTableOffset"]

    objs = []
    for i in range(numobjs):
        e = objtab + i * 24
        vs, rb, fl, pt, np, _ = struct.unpack_from("<IIIIII", d, e)
        objs.append(dict(vsize=vs, relbase=rb, flags=fl, pt=pt, np=np))

    for entry in range(numobjs):
        o = objs[entry]
        page = o["pt"] - 1
        for remaining in range(o["np"], 0, -1):
            cur = u32(d, fixpage + page * 4)
            nxt = u32(d, fixpage + (page + 1) * 4)
            while cur < nxt:
                stype = d[fixrec + cur]
                ttype = d[fixrec + cur + 1]
                sofs = ((o["np"] - remaining) * pagesize
                        + i16(d, fixrec + cur + 2))
                tobj = d[fixrec + cur + 4]
                if stype == 2:
                    tofs, cur = 0x80000000, cur + 5
                elif ttype == 16:
                    tofs, cur = i32(d, fixrec + cur + 5), cur + 9
                else:
                    tofs, cur = u16(d, fixrec + cur + 5), cur + 7
                ftype = FT.get(stype, "?%d" % stype)
                if tofs != 0x80000000 and 1 <= tobj <= numobjs:
                    yield Fixup(entry, o["relbase"] + sofs,
                                tobj - 1, objs[tobj - 1]["relbase"] + tofs, ftype)
            page += 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    d = leparse.le_data(sys.argv[1])
    lf = u32(d, 0x3C)
    hdr = leparse.read_header(d, lf)
    objtab = lf + hdr["ObjectTableOffset"]
    fixrec = lf + hdr["FixupRecordTableOffset"]
    numobjs = hdr["NumObjects"]
    objs = []
    for i in range(numobjs):
        e = objtab + i * 24
        vs, rb, fl, pt, np, _ = struct.unpack_from("<IIIIII", d, e)
        objs.append(dict(vsize=vs, relbase=rb, flags=fl, pt=pt, np=np))

    print("objects: " + ", ".join(
        "obj%d[0x%X,0x%X) pages=%d" % (i, o["relbase"], o["relbase"] + o["vsize"], o["np"])
        for i, o in enumerate(objs)))
    print("fixup page table @0x%X, fixup record table @0x%X\n" % (lf + hdr["FixupPageTableOffset"], fixrec))

    want = (int(sys.argv[2], 16), int(sys.argv[3], 16)) if len(sys.argv) > 3 else None
    n = 0
    for f in fixups(sys.argv[1]):
        show = want is None or (want[0] <= f.tadr < want[1] or want[0] <= f.sadr < want[1])
        if show:
            n += 1
            print("obj%d  src=0x%08X -> obj%d  tgt=0x%08X  %s"
                  % (f.ssec, f.sadr, f.tsec, f.tadr, f.ftype))
    print("\n%d fixups" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
