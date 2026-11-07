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

"""The routines that return a result in the flags, from the executable itself
(no SR output needed): instruction_flags.sci lines for SRW.

SRW does not keep the flags across a ret.  A call site whose flags are read
after the call (jcc, setcc, adc ... before any instruction that writes them)
needs them from the call, and each ret of its callee must compute them; a call
right before such a ret (nothing but moves in between) passes them on, so its
callee is followed too.  Code that returns its result in the flags throughout
(machine-translated code of another CPU) has hundreds of such sites, which
retflags.py, on SR's output, finds only as far as SR has decoded the code.

usage: pe_callflags.py GAME.EXE [fixup_do_not_interpret_as_code.sci]
       (the .sci: data in code, where the walk through a callee stops)
prints  loc_<call>,0x00,<flags> ; call   and   loc_<ret>,<flags>,0x00 ; ret
"""

import collections
import os
import re
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from exe_image import PeImage

CF, PF, AF, ZF, SF, OF = 1, 2, 4, 8, 0x10, 0x20
CC = {'e': ZF, 'ne': ZF, 'b': CF, 'ae': CF, 'be': CF | ZF, 'a': CF | ZF, 's': SF, 'ns': SF, 'o': OF,
      'no': OF, 'l': SF | OF, 'ge': SF | OF, 'le': ZF | SF | OF, 'g': ZF | SF | OF, 'p': PF, 'np': PF}
NOWRITE = re.compile(r'^(mov|movzx|movsx|lea|push|pop|xchg|cwde|cbw|cdq|cwd|nop|movs\w*|stos\w*|lods\w*'
                     r'|bswap|jmp)$')

md = Cs(CS_ARCH_X86, CS_MODE_32)


def read_mask(m):
    """the flags an instruction reads, None for one that reads none"""
    mm = re.match(r'^(j|set|cmov)(\w+)$', m)
    if mm and m != 'jmp':
        return CC.get(mm.group(2), 0x3F)
    if m in ('adc', 'sbb', 'rcl', 'rcr', 'cmc'):
        return CF
    if m in ('lahf', 'pushfd', 'pushf'):
        return 0x3F
    return None


def target(i):
    if re.fullmatch(r'0x[0-9a-f]+', i.op_str):
        return int(i.op_str, 16)


class Walker:
    def __init__(self, pe, nocode):
        self.pe, self.nocode, self.cache = pe, nocode, {}

    def ins(self, a):
        if a not in self.cache:
            self.cache[a] = next(md.disasm(self.pe.bytes_at(a, 16), a, count=1), None)
        return self.cache[a]

    def reads_after(self, a):
        """the flags read from a (a return point) on before an instruction writes them;
        follows jcc and jmp"""
        tot = 0
        seen = set()
        st = [a]
        while st:
            x = st.pop()
            while x not in seen and self.pe.in_code(x):
                seen.add(x)
                i = self.ins(x)
                if i is None:
                    break
                r = read_mask(i.mnemonic)
                if r is not None:
                    tot |= r
                    if i.mnemonic in ('adc', 'sbb', 'rcl', 'rcr'):
                        break
                    if i.mnemonic.startswith('j'):
                        t = target(i)
                        if t:
                            st.append(t)
                    x = i.address + i.size
                    continue
                if i.mnemonic == 'jmp':
                    t = target(i)
                    if t:
                        st.append(t)
                    break
                if NOWRITE.match(i.mnemonic):
                    x = i.address + i.size
                    continue
                break
        return tot

    def rets_of(self, f):
        """the rets reached from f, and the calls right before them (moves in between)"""
        rets, tails, seen = set(), set(), set()
        st = [f]
        while st:
            x = st.pop()
            last_call = None
            while x not in seen and self.pe.in_code(x) and x not in self.nocode:
                seen.add(x)
                i = self.ins(x)
                if i is None:
                    break
                m = i.mnemonic
                if m == 'ret':
                    rets.add(x)
                    if last_call:
                        tails.add(last_call)
                    break
                if m == 'jmp':
                    t = target(i)
                    if t:
                        st.append(t)
                    break
                if m.startswith('j') or m.startswith('loop'):
                    t = target(i)
                    if t:
                        st.append(t)
                    last_call = None
                elif m == 'call':
                    last_call = (x, target(i))
                elif not NOWRITE.match(m):
                    last_call = None
                x = i.address + i.size
        return rets, tails


def main(argv):
    if len(argv) not in (1, 2):
        sys.exit(__doc__)
    pe = PeImage(argv[0])
    nocode = set()
    if len(argv) == 2:
        nocode = {int(l[4:].split()[0].split(';')[0], 16) for l in open(argv[1]) if l.startswith('loc_')}
    w = Walker(pe, nocode)
    # the call sites: a linear sweep of the code
    sd = Cs(CS_ARCH_X86, CS_MODE_32)
    sd.skipdata = True
    sites = {}
    for lo, hi in pe.code:
        for i in sd.disasm(pe.bytes_at(lo, hi - lo), lo):
            if i.mnemonic == 'call':
                t = target(i)
                if t and pe.in_code(t):
                    m = w.reads_after(i.address + i.size)
                    if m:
                        sites[i.address] = (t, m)
    need = collections.defaultdict(int)
    for a, (t, m) in sites.items():
        need[t] |= m
    callmask = {a: m for a, (t, m) in sites.items()}
    retmask = collections.defaultdict(int)
    work = list(need)
    done = {}
    while work:
        f = work.pop()
        m = need[f]
        if done.get(f) == m:
            continue
        done[f] = m
        rets, tails = w.rets_of(f)
        for r in rets:
            retmask[r] |= m
        for ca, ct in tails:
            callmask[ca] = callmask.get(ca, 0) | m
            if ct and need[ct] | m != need[ct]:
                need[ct] |= m
                work.append(ct)
            elif ct and ct not in done:
                work.append(ct)
    print('; the routines that return a result in the flags (pe_callflags.py):')
    print('; each call site takes the flags from the call, each ret of the callee computes them first')
    for a in sorted(callmask):
        print('loc_%X,0x00,0x%02X ; call' % (a, callmask[a]))
    for r in sorted(retmask):
        print('loc_%X,0x%02X,0x00 ; ret' % (r, retmask[r]))
    print('sites %d, callees %d, rets %d' % (len(sites), len(need), len(retmask)), file=sys.stderr)


if __name__ == '__main__':
    main(sys.argv[1:])
