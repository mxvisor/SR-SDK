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

"""List the exports of a PE32 DLL with the stdcall arg count.

The arg count is taken from the `ret N` of the exported function (N/4).
"""
import struct
import sys
import capstone


def pe_info(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    imagebase = struct.unpack_from('<I', data, opt + 28)[0]
    dirs = [struct.unpack_from('<II', data, opt + 96 + i * 8) for i in range(16)]
    secs = []
    sh = opt + optsz
    for i in range(nsec):
        o = sh + i * 40
        name = data[o:o + 8].rstrip(b'\0').decode('latin1')
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return imagebase, dirs, secs


def rva2off(secs, rva):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None


def exports(data):
    imagebase, dirs, secs = pe_info(data)
    off = rva2off(secs, dirs[0][0])
    (chars, ts, mj, mn, name_rva, base, nfun, nnam,
     afun, anam, aord) = struct.unpack_from('<IIHHIIIIIII', data, off)
    noff = rva2off(secs, anam)
    aoff = rva2off(secs, aord)
    funoff = rva2off(secs, afun)
    out = []
    for i in range(nnam):
        nrva = struct.unpack_from('<I', data, noff + i * 4)[0]
        n = data[rva2off(secs, nrva):]
        nm = n[:n.index(b'\0')].decode('latin1')
        fidx = struct.unpack_from('<H', data, aoff + i * 2)[0]
        frva = struct.unpack_from('<I', data, funoff + fidx * 4)[0]
        out.append((nm, frva))
    return imagebase, secs, out


if __name__ == '__main__':
    path = sys.argv[1]
    data = open(path, 'rb').read()
    imagebase, secs, exps = exports(data)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    want = sys.argv[2:] if len(sys.argv) > 2 else None
    for nm, rva in sorted(exps, key=lambda x: x[0]):
        if want and nm not in want:
            continue
        if rva == 0:
            print('%-28s forwarded' % nm)
            continue
        off = rva2off(secs, rva)
        code = data[off:off + 64]
        ins = list(md.disasm(code, imagebase + rva))
        ret = None
        for i in ins[:20]:
            if i.mnemonic == 'ret':
                ret = i.op_str
                break
            if i.mnemonic == 'jmp':
                ret = 'jmp ' + i.op_str
                break
        print('%-28s %08X  %-12s %s' % (nm, imagebase + rva, ret or '', ' | '.join('%s %s' % (x.mnemonic, x.op_str) for x in ins[:6])))
