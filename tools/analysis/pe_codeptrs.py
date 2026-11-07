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

"""Code or data: the addresses in code that a PE with base relocations stores
with `mov dword [mem], imm32`, for SRW's fixup lists.

SRW takes two adjacent relocations for a table of code addresses, and the
`mov dword [abs32], imm32` of an address in code is two adjacent relocations:
every such address becomes code, the data among them too (tables, strings,
data in the code section).  A routine stored into the field of a structure or
onto the stack (a WNDCLASS) is not taken for code at all.  This sorts them,
from a linear sweep of the code sections:

  code   a direct call or jump goes there; or the variable it is stored into is
         called through (`call/jmp [var]`, or a load into a register that is
         called) and it reads as code (the probe); or it is stored into a field
         of a structure or the stack and reads as code by the strict probe (no
         branch on the flags first, no `jmp [mem]` in the first instructions)
  data   the rest of the stored addresses, and the targets of adjacent
         relocations that are not known code and fail the probe

The probe: up to 12 instructions to a ret or jmp, none privileged or of the
16-bit era (segment loads, BCD, in/out), no `add [eax], al` twice in a row.

usage: pe_codeptrs.py [--data-var ADDR]... [--sci DIR] GAME.EXE
       pe_codeptrs.py --near ADDR [SPAN] GAME.EXE

--data-var ADDR  a variable that holds addresses of data as well as of code
                 (an emulated address register): what is stored into it is data
--sci DIR        adds the lists to DIR/fixup_interpret_as_code.sci and
                 DIR/fixup_do_not_interpret_as_code.sci (else prints them)
--near ADDR      the targets of relocations in the SPAN (512) bytes below ADDR,
                 the address of an SRW error: which of them begins the bad code
"""

import collections
import os
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_ESP

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exe_image import PeImage

BAD = {'int1', 'hlt', 'in', 'out', 'insb', 'insd', 'insw', 'outsb', 'outsd', 'outsw', 'cli', 'sti',
       'lds', 'les', 'lfs', 'lgs', 'lss', 'bound', 'arpl', 'into', 'iretd', 'iret', 'retf', 'ljmp',
       'lcall', 'salc', 'aaa', 'aas', 'daa', 'das', 'aam', 'aad', 'int3', 'wait', 'fwait', 'sldt',
       'str', 'lock'}
SEGS = ('ss', 'es', 'ds', 'fs', 'gs', 'cs')

md = Cs(CS_ARCH_X86, CS_MODE_32)


def probe(pe, t):
    n = 0
    prevz = False
    for i in md.disasm(pe.bytes_at(t, 80), t):
        m, s = i.mnemonic, i.op_str
        if m in BAD:
            return False
        if m in ('push', 'pop') and s in SEGS:
            return False
        z = m == 'add' and s == 'byte ptr [eax], al'
        if z and prevz:
            return False
        prevz = z
        n += 1
        if m in ('ret', 'jmp') or n >= 12:
            return True
    return n >= 3


def strict_probe(pe, t):
    if not probe(pe, t):
        return False
    ins = list(md.disasm(pe.bytes_at(t, 48), t, count=6))
    if not ins:
        return False
    m = ins[0].mnemonic
    if (m.startswith('j') and m != 'jmp') or m.startswith('loop') or m == 'jecxz':
        return False
    for i in ins[:4]:
        if i.mnemonic == 'jmp' and 'ptr [' in i.op_str:
            return False
        if i.mnemonic == 'ret':
            break
    return True


def mem_key(op):
    """('ds', addr) for a variable, ('off', disp) for a field, ('esp', disp) for the stack"""
    m = op.mem
    if m.base == 0 and m.index == 0:
        return ('ds', m.disp & 0xFFFFFFFF)
    return ('esp' if m.base == X86_REG_ESP else 'off', m.disp)


def sweep(pe):
    """the targets of the direct branches, the variables called through"""
    sd = Cs(CS_ARCH_X86, CS_MODE_32)
    sd.detail = True
    sd.skipdata = True
    direct, codevar = set(), set()
    for lo, hi in pe.code:
        L = list(sd.disasm(pe.bytes_at(lo, hi - lo), lo))
        for k, i in enumerate(L):
            if i.id == 0:
                continue
            m = i.mnemonic
            ops = i.operands
            if (m == 'call' or m.startswith('j')) and len(ops) == 1 and ops[0].type == X86_OP_IMM:
                direct.add(ops[0].imm & 0xFFFFFFFF)
            elif m in ('call', 'jmp') and len(ops) == 1 and ops[0].type == X86_OP_MEM and ops[0].size == 4:
                codevar.add(mem_key(ops[0]))
            elif m == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_REG and ops[0].size == 4 \
                    and ops[1].type == X86_OP_MEM:
                regs = {ops[0].reg}
                for j in L[k + 1:k + 8]:
                    if j.id == 0:
                        break
                    jo = j.operands
                    if j.mnemonic in ('call', 'jmp') and len(jo) == 1 and jo[0].type == X86_OP_REG:
                        if jo[0].reg in regs:
                            codevar.add(mem_key(ops[1]))
                        break
                    if j.mnemonic == 'mov' and len(jo) == 2 and jo[0].type == X86_OP_REG \
                            and jo[1].type == X86_OP_REG and jo[1].reg in regs:
                        regs.add(jo[0].reg)
                    if j.mnemonic.startswith('ret') or j.mnemonic == 'jmp':
                        break
    codevar = {k for k in codevar if k[0] != 'esp'}    # a stack slot is no variable
    return direct, codevar


def store_key(pe, r):
    """the destination of the `mov dword [mem], imm32` whose immediate is at r, or None"""
    b = pe.bytes_at(r - 7, 7)
    if b[1:3] == b'\xc7\x05' and (r - 4) in pe.relocs:
        return ('ds', pe.u32(r - 4))
    if b[4] == 0xC7 and 0x40 <= b[5] <= 0x47 and b[5] != 0x44:
        return ('off', struct.unpack('b', b[6:7])[0])
    if b[1] == 0xC7 and 0x80 <= b[2] <= 0x87 and b[2] != 0x84:
        return ('off', struct.unpack_from('<i', b, 3)[0])
    if b[3:6] == b'\xc7\x44\x24':
        return ('esp', b[6])
    if b[0:3] == b'\xc7\x84\x24':
        return ('esp', struct.unpack_from('<i', b, 3)[0])
    return None


def classify(pe, data_vars):
    direct, codevar = sweep(pe)
    codevar -= {('ds', a) for a in data_vars}
    stored = {}                                 # address -> {(code?, key)}
    for r, v in sorted(pe.relocs.items()):
        if not pe.in_code(v):
            continue
        k = store_key(pe, r)
        if k is None:
            continue
        c = v in direct or (k in codevar and probe(pe, v)) or (k[0] != 'ds' and strict_probe(pe, v))
        stored.setdefault(v, set()).add((c, k))
    code = {v for v, s in stored.items() if any(c for c, _ in s)}
    data = set(stored) - code
    # what else SRW takes for code: an address in code held by one of two adjacent relocations
    known = set(direct)
    known |= {v for v, s in stored.items() if any(k in codevar for _, k in s)}
    adjacent = {v for r, v in pe.relocs.items()
                if pe.in_code(r) and pe.in_code(v) and ((r + 4) in pe.relocs or (r - 4) in pe.relocs)}
    nocode = {v for v in adjacent - known if not probe(pe, v)}
    print('stored: code %d data %d; adjacent: %d, %d not code' % (len(code), len(data), len(adjacent),
          len(nocode)), file=sys.stderr)
    return code, (data | nocode) - code


def near(pe, addr, span):
    refs = collections.defaultdict(list)
    for r, v in pe.relocs.items():
        if pe.in_code(v):
            refs[v].append(r)
    for t in sorted((t for t in refs if addr - span <= t <= addr), reverse=True)[:8]:
        s = ['%s %s' % (i.mnemonic, i.op_str) for i in md.disasm(pe.bytes_at(t, 20), t, count=3)]
        print(hex(t), 'refs', [hex(r) for r in refs[t][:4]], pe.bytes_at(t, 8).hex(), ' ; '.join(s))


def merge(path, addrs):
    old = [l.rstrip('\n') for l in open(path)] if os.path.exists(path) else []
    have = {l.split(';')[0].strip().lower() for l in old}
    new = ['loc_%X' % v for v in sorted(addrs) if ('loc_%X' % v).lower() not in have]
    with open(path, 'w') as f:
        f.write(''.join(l + '\n' for l in old + new))
    print('%s: %d new' % (path, len(new)), file=sys.stderr)


def main(argv):
    data_vars, sci, near_at, span = [], None, None, 512
    while argv and argv[0].startswith('--'):
        o = argv.pop(0)
        if o == '--data-var':
            data_vars.append(int(argv.pop(0), 16))
        elif o == '--sci':
            sci = argv.pop(0)
        elif o == '--near':
            near_at = int(argv.pop(0), 16)
            if len(argv) > 1:
                span = int(argv.pop(0))
        else:
            sys.exit(__doc__)
    if len(argv) != 1:
        sys.exit(__doc__)
    pe = PeImage(argv[0])
    if near_at is not None:
        near(pe, near_at, span)
        return
    code, nocode = classify(pe, data_vars)
    if sci:
        merge(os.path.join(sci, 'fixup_interpret_as_code.sci'), code)
        merge(os.path.join(sci, 'fixup_do_not_interpret_as_code.sci'), nocode)
    else:
        print('; fixup_interpret_as_code.sci')
        print(''.join('loc_%X\n' % v for v in sorted(code)), end='')
        print('; fixup_do_not_interpret_as_code.sci')
        print(''.join('loc_%X\n' % v for v in sorted(nocode)), end='')


if __name__ == '__main__':
    main(sys.argv[1:])
