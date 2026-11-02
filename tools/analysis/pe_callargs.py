#!/usr/bin/env python3
"""Arg count of the import calls of a PE32 executable, from the call sites.

The stack depth of every import call is computed: scanning backwards from the
call within the function, the pushes add to the depth and the pops take from
it; the scan stops at the frame setup (`sub esp`), at `mov esp, ebp` and at a
control transfer.  The register saves pushed before the arguments are popped
after the call, so the arg count is the depth at the call minus the pops that
follow it.  Both call forms are seen: `call dword [IAT]` and the call of the
`jmp dword [IAT]` thunk.
"""
import sys
import capstone
from pe_imports import imports, sections

path = sys.argv[1]
data = open(path, 'rb').read()
pe, imagebase, dirs, secs = sections(data)
imp, _ = imports(path)
iat = {}
for dll, nm, va in imp:
    iat[va] = (dll, nm)

vaddr = vsize = raddr = None
for name, va_, vs, ra, rs in secs:
    if name in ('.text', 'CODE', '.code'):
        vaddr, vsize, raddr = va_, vs, ra
code = data[raddr:raddr + vsize]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
md.skipdata = True
insns = [i for i in md.disasm(code, imagebase + vaddr) if i.id != 0]

STOP = ('call', 'jmp', 'ret', 'leave', 'int3', 'int', 'hlt', 'loop', 'iretd', 'jecxz')

# the thunks: jmp dword [IAT]
thunk_of = {}
for i in insns:
    if i.mnemonic == 'jmp' and i.operands[0].type == capstone.x86.X86_OP_MEM:
        op = i.operands[0]
        if op.mem.base == 0 and op.mem.index == 0:
            slot = op.mem.disp if op.mem.disp > imagebase else imagebase + op.mem.disp
            if slot in iat:
                thunk_of[i.address] = iat[slot][1]

starts = set()
for i in insns:
    if i.mnemonic == 'call' and i.operands[0].type == capstone.x86.X86_OP_IMM:
        starts.add(i.operands[0].imm)
    if i.mnemonic == 'jmp' and i.operands[0].type == capstone.x86.X86_OP_IMM:
        starts.add(i.operands[0].imm)
starts.add(imagebase + dirs[10][0])


def func_start_idx(i):
    j = i
    while j >= 0 and insns[j].address not in starts:
        j -= 1
    return j if j >= 0 else 0


def depth_at(i):
    d = 0
    j = i - 1
    lo = func_start_idx(i)
    while j > lo:
        p = insns[j]
        m = p.mnemonic
        if m in STOP:
            break
        if m == 'push':
            d += 1
        elif m == 'pop':
            d -= 1
        elif m in ('sub', 'add') and p.op_str.startswith('esp'):
            break
        elif m == 'mov' and p.op_str.startswith('esp,'):
            break
        j -= 1
    return d


def pops_after(i):
    d = 0
    j = i + 1
    while j < len(insns):
        p = insns[j]
        m = p.mnemonic
        if m in STOP:
            break
        if m == 'pop':
            d += 1
        elif m in ('sub', 'add') and p.op_str.startswith('esp'):
            break
        j += 1
    return d


sites = {}
for n, i in enumerate(insns):
    nm = None
    if i.mnemonic == 'call':
        op = i.operands[0]
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            slot = op.mem.disp if op.mem.disp > imagebase else imagebase + op.mem.disp
            if slot in iat:
                nm = iat[slot][1]
        elif op.type == capstone.x86.X86_OP_IMM and op.imm in thunk_of:
            nm = thunk_of[op.imm]
    if nm:
        d = depth_at(n)
        pa = pops_after(n)
        sites.setdefault(nm, []).append((i.address, d, pa, d - pa))

for nm in sorted(sites):
    vals = sorted(set(a for _, _, _, a in sites[nm]))
    print('%-28s %s' % (nm, ' '.join('%d' % v for v in vals)))