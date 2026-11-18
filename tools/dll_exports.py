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

"""A DLL of the original recompiled as a module (dll_modules= of game.cfg): what the
runtime needs to load it.

    dll_exports.py alias  <module> <DLL> <global_aliases.sci>
        before SRW: adds the entry point of the DLL (DllMain) to the aliases SRW reads, so
        SRW decodes it as a procedure of its own, named DllEntry
    dll_exports.py table  <module> <DLL> <master.llasm> [<EXE>]
        after compact_source: appends to the master the table of the module,
        `<module>_dll` (a global data label the runtime declares):

            daddr DllEntry (0 when there is none)
            then, per exported function: daddr <name>, daddr <function>
            and a final pair of zeros

        The names are the export names of the DLL as the PE has them (`_drv_init@0`),
        the strings follow the table.  The procedures SRW made global for the exports
        (`proc X global c_X`) become plain ones: the table is the module's interface, and
        two modules may export the same names.  But the exports that <EXE> (the program,
        the main module) imports from the DLL by its import table become `proc X public`:
        its calls of them link to the module directly, and the runtime runs the DllMain
        of such a module before the program's entry (GAME_DLL_STATIC of dll_modules.h,
        made by scons/game.py with imported_names()).
"""
import os
import re
import struct
import sys


def read_pe(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        sys.exit('%s: not a PE file' % path)
    nsec, = struct.unpack_from('<H', data, pe + 6)
    optsize, = struct.unpack_from('<H', data, pe + 20)
    opt = pe + 24
    entry, = struct.unpack_from('<I', data, opt + 16)
    base, = struct.unpack_from('<I', data, opt + 28)
    exp_rva, exp_size = struct.unpack_from('<II', data, opt + 96)
    sections = []
    for i in range(nsec):
        off = opt + optsize + 40 * i
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', data, off + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr))

    def at(rva):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + rva - vaddr
        sys.exit('%s: no section at RVA 0x%x' % (path, rva))

    def cstr(rva):
        start = at(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')

    exports = []
    if exp_rva:
        (_, _, _, _, _, _, _, nnames, addr_f, addr_n, addr_o) = \
            struct.unpack_from('<IIHHIIIIIII', data, at(exp_rva))
        for i in range(nnames):
            name_rva, = struct.unpack_from('<I', data, at(addr_n) + 4 * i)
            ordinal, = struct.unpack_from('<H', data, at(addr_o) + 2 * i)
            func_rva, = struct.unpack_from('<I', data, at(addr_f) + 4 * ordinal)
            if exp_rva <= func_rva < exp_rva + exp_size:
                continue                       # forwarded to another DLL
            exports.append((cstr(name_rva), base + func_rva))
    return base, base + entry if entry else 0, exports


def imported_names(exe, module):
    """The names the PE <exe> imports from the DLL <module> (<module>.dll, the case
    ignored), by its import table; [] when it imports none."""
    data = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        return []
    nsec, = struct.unpack_from('<H', data, pe + 6)
    optsize, = struct.unpack_from('<H', data, pe + 20)
    opt = pe + 24
    imp_rva, _ = struct.unpack_from('<II', data, opt + 96 + 8)
    sections = []
    for i in range(nsec):
        off = opt + optsize + 40 * i
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', data, off + 8)
        if rawptr:
            sections.append((vaddr, max(vsize, rawsize), rawptr))

    def at(rva):
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + rva - vaddr
        return None

    def cstr(rva):
        start = at(rva)
        return data[start:data.index(b'\0', start)].decode('latin1')

    names = []
    off = at(imp_rva) if imp_rva else None
    while off is not None:
        ilt, _, _, name_rva, iat = struct.unpack_from('<IIIII', data, off)
        if name_rva == 0:
            break
        dll = cstr(name_rva).lower()
        if dll == module.lower() + '.dll' or dll == module.lower():
            thunk = at(ilt or iat)
            while thunk is not None:
                value, = struct.unpack_from('<I', data, thunk)
                if value == 0:
                    break
                if not value & 0x80000000:      # by ordinal: no name to link by
                    names.append(cstr(value + 2))
                thunk += 4
        off += 20
    return names


def label(address):
    return 'loc_%X' % address          # as SR_get_label of SRW: no leading zeros


def alias(_module, dll, sci):
    _, entry, _ = read_pe(dll)
    line = '%s,DllEntry' % label(entry)
    have = open(sci).read() if os.path.exists(sci) else ''
    if entry and line not in have.split():
        with open(sci, 'a') as out:
            if have and not have.endswith('\n'):
                out.write('\n')
            out.write(line + '\n')


def table(module, dll, master, exe=None):
    _, entry, exports = read_pe(dll)
    public = set(imported_names(exe, module)) if exe else set()
    directory = os.path.dirname(os.path.abspath(master))
    text = open(master).read()
    rename = dict(re.findall(r'^define (loc_\w+) (\S+)\s*$', text, re.M))
    procs = set()
    for name in os.listdir(directory):
        if name.endswith(('.llinc', '.llasm')):
            path = os.path.join(directory, name)
            source = open(path).read()
            procs.update(re.findall(r'^proc (\S+)', source, re.M))
            plain = re.sub(r'^proc (\S+) global \S+[ \t]*$',
                           lambda m: 'proc %s%s' % (m.group(1), ' public' if m.group(1) in public else ''),
                           source, flags=re.M)
            if plain != source:
                with open(path, 'w') as stream:
                    stream.write(plain)
    out = ['', '; the table of the module %s for the runtime (scons: tools/dll_exports.py)' % module,
           'datasegment %s_exports constant' % module,
           'dlabel %s_dll global' % module,
           'daddr DllEntry' if entry else 'db 0 dup 4']
    names = []
    for index, (name, address) in enumerate(sorted(exports)):
        target = rename.get(label(address), label(address))
        if target not in procs:
            continue                           # data or an address SRW did not take as code
        out.append('daddr %s_name%d' % (module, index))
        out.append('daddr %s' % target)
        names.append((index, name))
    out.append('db 0 dup 8')
    for index, name in names:
        out.append('dlabel %s_name%d' % (module, index))
        out.extend('db %d' % b for b in name.encode('ascii') + b'\0')
    out.append('endd')
    with open(master, 'a') as stream:
        stream.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    if not (len(sys.argv) == 5 or (len(sys.argv) == 6 and sys.argv[1] == 'table')) \
            or sys.argv[1] not in ('alias', 'table'):
        sys.exit(__doc__)
    {'alias': alias, 'table': table}[sys.argv[1]](*sys.argv[2:])
