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

"""Find the rets whose flags a caller reads after the call, from SR's llasm output.

usage: retflags.py SEG01_CODE.llinc GAME.EXE (PE32 or LE)  [existing instruction_flags.sci]
prints sci lines  loc_<ret address>,0x3F,0x00 ; ret of <callee>
"""
import re, sys, collections
import capstone

llinc, exe = sys.argv[1], sys.argv[2]
sci = sys.argv[3] if len(sys.argv) > 3 else None

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_image
img = exe_image.load(exe)          # PE32 or LE (DOS4GW), at the addresses SR uses
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

icalls = []                             # (proc, x86 call, return point) of the indirect calls
procs = collections.OrderedDict()       # name -> dict(instrs=[comment], succ=[], calls=[], ret=bool)
cur = None
lastpush = None
for line in open(llinc):
    line = line.rstrip('\n')
    m = re.match(r'proc (loc_[0-9A-Fa-f]+)', line)
    if m:
        cur = dict(name=m.group(1), instrs=[], succ=[], calls=[], ret=False)
        procs[cur['name']] = cur
        lastpush = None
        continue
    if cur is None:
        continue
    if line.startswith('endp'):
        cur = None
        continue
    if line.startswith(';'):
        ins = re.sub(r'^(?:ACTION_[A-Z_]+;)+', '', line[1:])    # SR's DOS output: ;ACTION_CALL;call loc_X
        cur['instrs'].append(ins)
        if ins.strip() == 'ret' or ins.startswith('ret '):
            cur['ret'] = True
        continue
    m = re.match(r'PUSH (loc_[0-9A-Fa-f]+)$', line)
    if m:
        lastpush = m.group(1)
        continue
    m = re.match(r'(?:tcall|ctcall\w*(?: \w+,)?) (?:\w+, )?(loc_[0-9A-Fa-f]+)$', line)
    if m:
        tgt = m.group(1)
        if line.startswith('tcall') and lastpush is not None:
            cur['calls'].append((tgt, lastpush))      # call tgt, returns to lastpush
            cur['succ'].append(lastpush)              # control continues there in this procedure
        else:
            cur['succ'].append(tgt)
        lastpush = None
        continue
    m = re.match(r'tcall (\w+)$', line)
    if m and lastpush is not None:
        # an indirect call (call [esi+0x36], call eax): its callees are not known here
        icalls.append((cur['name'], cur['instrs'][-1].strip() if cur['instrs'] else '?', lastpush))
        cur['succ'].append(lastpush)
        lastpush = None
        continue
    if line.startswith('PUSH') or line.startswith('POP'):
        pass

NOWRITE = re.compile(r'^(mov|movzx|movsx|lea|push|pop|xchg|cwde|cbw|cdq|cwd|nop|movs\w*|stos\w*|lods\w*|ins\w*|out|in|pushad|popad|pushaw|popaw|bswap|jmp|call|ret|shld_)\b')
READER = re.compile(r'^(j(?!mp)\w+|set\w+|cmov\w+|adc|sbb|rcl|rcr|lahf|pushf\w*|loope|loopne|loopz|loopnz|cmc|salc)\b')

CF,PF,AF,ZF,SF,OF=0x01,0x02,0x04,0x08,0x10,0x20
CC={'z':ZF,'e':ZF,'nz':ZF,'ne':ZF,'b':CF,'c':CF,'nae':CF,'ae':CF,'nb':CF,'nc':CF,
    'be':CF|ZF,'na':CF|ZF,'a':CF|ZF,'nbe':CF|ZF,'s':SF,'ns':SF,'o':OF,'no':OF,
    'l':SF|OF,'nge':SF|OF,'ge':SF|OF,'nl':SF|OF,'le':ZF|SF|OF,'ng':ZF|SF|OF,'g':ZF|SF|OF,'nle':ZF|SF|OF,
    'p':PF,'pe':PF,'np':PF,'po':PF}
def reader_mask(op):
    m = re.match(r'^(j|set|cmov)(\w+)\b', op)
    if m and m.group(1)+m.group(2) not in ('jmp',):
        return CC.get(m.group(2), 0x3F)
    if re.match(r'^(adc|sbb|rcl|rcr|cmc|salc)\b', op): return CF
    if re.match(r'^(loope|loopz|loopne|loopnz)\b', op): return ZF
    if re.match(r'^(lahf|pushf\w*)\b', op): return 0x3F
    return None

def reads_flags_mask(start):
    """the flags that the code at proc `start` reads before it writes them"""
    total = 0
    seen = set()
    stack = [start]
    while stack:
        n = stack.pop()
        if n in seen or n not in procs:
            continue
        seen.add(n)
        p = procs[n]
        stopped = False
        for ins in p['instrs']:
            op = ins.strip()
            if not op:
                continue
            rm = reader_mask(op)
            if rm is not None:
                total |= rm
                if re.match(r'^(adc|sbb|rcl|rcr)\b', op):
                    stopped = True; break        # also writes
                continue
            if op.startswith('call'):
                stopped = True     # what follows reads the flags of that callee, its own entry
                break
            if NOWRITE.match(op):
                continue
            stopped = True
            break                  # a flag writer comes first
        if not stopped:
            stack.extend(p['succ'])
    return total

callees = {}
for p in procs.values():
    for tgt, ret in p['calls']:
        m = reads_flags_mask(ret)
        if m:
            callees[tgt] = callees.get(tgt, 0) | m

def ret_addr(pname):
    p = procs[pname]
    va = int(pname[4:], 16)
    n = len(p['instrs'])
    ins = list(cs.disasm(img.bytes_at(va, 16 * (n + 2)), va, count=n))
    if len(ins) != n or ins[-1].mnemonic != 'ret':
        return None
    return ins[-1].address

cont_map = collections.defaultdict(set)      # return-point proc -> call targets
for p in procs.values():
    for tgt, ret in p['calls']:
        cont_map[ret].add(tgt)

def passthrough(n):
    """a proc that is the return point of a call and reaches its ret without writing flags"""
    if n not in cont_map:
        return False
    for ins in procs[n]['instrs']:
        op = ins.strip()
        if not op:
            continue
        if op == 'ret' or op.startswith('ret '):
            return True
        if not NOWRITE.match(op):
            return False
    return False

def collect():
    rets = collections.OrderedDict()
    for callee, cmask in callees.items():
        seen = set()
        stack = [callee]
        while stack:
            n = stack.pop()
            if n in seen or n not in procs:
                continue
            seen.add(n)
            p = procs[n]
            if p['ret']:
                rets.setdefault(n, [0, set()])
                rets[n][0] |= cmask
                rets[n][1].add(callee)
            stack.extend(p['succ'])
    return rets

while True:
    rets_p = collect()
    changed = False
    for n, (m, cs_) in rets_p.items():
        if passthrough(n):
            for t in cont_map[n]:
                if callees.get(t, 0) | m != callees.get(t, 0):
                    callees[t] = callees.get(t, 0) | m
                    changed = True
    if not changed:
        break

rets = collections.OrderedDict()
for n, (m, cs_) in rets_p.items():
    if passthrough(n):
        continue
    a = ret_addr(n)
    if a is not None:
        rets[a] = [m, cs_]

have = set()
if sci:
    for l in open(sci):
        m = re.match(r'loc_([0-9A-Fa-f]+),', l)
        if m:
            have.add(int(m.group(1), 16))

print("; rets whose flags a caller reads after the call (retflags.py)", file=sys.stderr)
new = 0
for a in sorted(rets):
    if a in have:
        continue
    new += 1
    print("loc_%X,0x%02X,0x00 ; ret of %s" % (a, rets[a][0], ' '.join(sorted(rets[a][1]))[:60]))
print("%d callees, %d rets, %d new" % (len(callees), len(rets), new), file=sys.stderr)

# an indirect call has no callee list here: if the code after it reads the flags,
# the rets of its targets (fields set with mov [reg+N], loc_X, the code addresses
# of the fixups, a trace) need lines this tool cannot give
iread = [(p, c, r, reads_flags_mask(r)) for p, c, r in icalls]
iread = [x for x in iread if x[3]]
print("%d indirect calls, %d read the flags after the return%s" % (len(icalls), len(iread), ':' if iread else ''), file=sys.stderr)
for p, c, r, m in iread:
    print("  %s: %s, returns to %s, reads 0x%02X" % (p, c, r, m), file=sys.stderr)
