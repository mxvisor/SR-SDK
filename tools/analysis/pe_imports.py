#!/usr/bin/env python3
"""List the imports of a PE32 executable: dll, name, IAT slot (VA), argc.

The argc is taken from the MinGW-w64 headers (the stdcall argument count of
the API), which is what SR-SDK/tools/gen_import_shim.py needs.
"""
import re
import struct
import sys


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    imagebase = struct.unpack_from('<I', data, opt + 28)[0]
    ndir = struct.unpack_from('<I', data, opt + 92)[0]
    dirs = []
    for i in range(min(ndir, 16)):
        dirs.append(struct.unpack_from('<II', data, opt + 96 + i * 8))
    secs = []
    sh = opt + optsz
    for i in range(nsec):
        o = sh + i * 40
        name = data[o:o + 8].rstrip(b'\0').decode('latin1')
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return pe, imagebase, dirs, secs


def rva2off(secs, rva):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None


def cstr(data, off):
    end = data.index(b'\0', off)
    return data[off:end].decode('latin1')


def imports(path):
    data = open(path, 'rb').read()
    pe, imagebase, dirs, secs = sections(data)
    imp_rva, imp_size = dirs[1]
    off = rva2off(secs, imp_rva)
    out = []
    while True:
        ilt, ts, fc, name_rva, iat = struct.unpack_from('<IIIII', data, off)
        if name_rva == 0:
            break
        dll = cstr(data, rva2off(secs, name_rva))
        thunk = ilt if ilt else iat
        toff = rva2off(secs, thunk)
        slot = iat
        while True:
            val = struct.unpack_from('<I', data, toff)[0]
            if val == 0:
                break
            if val & 0x80000000:
                nm = '#%d' % (val & 0xffff)
            else:
                no = rva2off(secs, val)
                nm = cstr(data, no + 2)
            out.append((dll, nm, imagebase + slot))
            slot += 4
            toff += 4
        off += 20
    return out, imagebase


if __name__ == '__main__':
    for dll, nm, va in imports(sys.argv[1])[0]:
        print('%s\t%s\t0x%X' % (dll, nm, va))
