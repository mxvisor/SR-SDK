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

"""Grow fixup_interpret_as_code.sci until SR emits all the code of the game.

SR emits the code it reaches by the flow analysis and the code the relocations
of the executable point at.  A function that is only called through a pointer
in a table of the data is reached by neither, and is written as data.  Its
address is a target of a relocation, so listing it in
fixup_interpret_as_code.sci makes SR take the relocation for a code pointer and
disassemble the function.

The relocations are taken from the same scan as SR-SDK/tools/analysis/
pe_relocations.py: a 4-byte field inside an instruction whose value appears in
the instruction's disassembly string (a real pointer, not a byte of a
displacement that happens to look like an address).

The loop: run SR, look at the code it emitted, add the missing relocation
targets to fixup_interpret_as_code.sci, run SR again, until the number of the
emitted instructions stops growing.

Usage: sr_cover.py [--dry-run] [--repo DIR] [--module NAME] [--variant NAME] [ITERATIONS]

DIR is the game's -recomp directory (default: the current one); the paths are
those of its build (`build/recomp/<module>/llasm`, `build/input/<module>/
<module>.EXE`, `game/recomp/<module>/<variant>/llasm`).  Defaults: module
Game-main, variant default.
"""
import os
import re
import subprocess
import sys

import capstone

REPO = os.getcwd()
MODULE = 'Game-main'
VARIANT = 'default'
SR_DIR = SR_EXE = IN_EXE = SCI_DIR = SCI = NODATA = None


def set_paths(repo, module, variant):
    global REPO, SR_DIR, SR_EXE, IN_EXE, SCI_DIR, SCI, NODATA, MODULE
    MODULE = module
    REPO = os.path.abspath(repo)
    SR_DIR = os.path.join(REPO, 'build', 'recomp', module, 'llasm')
    SR_EXE = os.path.join(REPO, 'build', 'host', 'linux-x64', 'sr-llasm', 'SRW.exe')
    IN_EXE = os.path.join(REPO, 'build', 'input', module, module + '.EXE')
    if not os.path.exists(IN_EXE):
        IN_EXE = os.path.join(REPO, 'build', 'input', module, module + '.DLL')     # a dll_modules= module
    SCI_DIR = os.path.join(REPO, 'game', 'recomp', module, variant, 'llasm')
    SCI = os.path.join(SCI_DIR, 'fixup_interpret_as_code.sci')
    NODATA = os.path.join(SCI_DIR, 'fixup_do_not_interpret_as_code.sci')


BITWISE = {'and', 'or', 'xor', 'test', 'bt', 'bts', 'btr', 'btc'}


def load_sci(path):
    out = set()
    if not os.path.exists(path):
        return out
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#') and not line.startswith(';'):
            out.add(int(line.split(',')[0].replace('loc_', ''), 16))
    return out


def run_sr():
    for name in os.listdir(SCI_DIR):
        if name.endswith('.sci'):
            src = os.path.join(SCI_DIR, name)
            dst = os.path.join(SR_DIR, name)
            if not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst):
                subprocess.run(['cp', src, dst], check=True)
    with open(os.path.join(SR_DIR, 'sr.out'), 'w') as so, \
            open(os.path.join(SR_DIR, 'sr.err'), 'w') as se:
        return subprocess.run([SR_EXE, IN_EXE, MODULE + '.asm'], cwd=SR_DIR,
                              stdout=so, stderr=se).returncode


def emitted_code():
    out = set()
    with open(os.path.join(SR_DIR, 'seg01_code.llinc'), encoding='latin1') as f:
        for m in re.finditer(r'^proc loc_([0-9A-Fa-f]{6,8})\b', f.read(), re.M):
            out.add(int(m.group(1), 16))
    return out


def reloc_targets():
    """the code addresses the relocations of the executable point at.

    A 4-byte field inside an instruction is a real pointer only if its value
    appears in the instruction's disassembly string (the same scan as
    pe_relocations.py); a byte of a displacement that happens to look like an
    address is not.
    """
    import struct
    from pe_imports import sections
    data = open(IN_EXE, 'rb').read()
    _, imagebase, dirs, secs = sections(data)

    def rva2off(rva):
        for name, vaddr, vsize, raddr, rsize in secs:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raddr + (rva - vaddr)
        return None

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    md.skipdata_setup = (".byte", 0, 0)

    text = [s for s in secs if s[0] in ('.text', 'AUTO')][0]       # AUTO: the code section of a Watcom-linked DLL
    lo, hi = imagebase + text[1], imagebase + text[1] + max(text[2], text[4])
    raw = data[text[3]:text[3] + text[4]]
    limit = min(len(raw), text[4])
    out = {}
    for insn in md.disasm(raw, imagebase + text[1], limit):
        if insn.id == 0:
            continue
        start = insn.address - (imagebase + text[1])
        end = start + insn.size
        disasm_str = f"{insn.mnemonic} {insn.op_str}"
        for off in range(start, end - 3):
            val = struct.unpack_from('<I', raw, off)[0]
            if val == 0 or not (lo <= val < hi):
                continue
            val_str = f"0x{val:x}"
            if val_str not in disasm_str:
                continue
            if insn.mnemonic in BITWISE and insn.op_str.endswith(', ' + val_str):
                continue
            out.setdefault(val, []).append(insn.address)
    return out


def reads_as_code(v):
    """the bytes at v look like the start of a function (a jmp/ret/call
    within the first 16 bytes)"""
    from pe_imports import sections
    data = open(IN_EXE, 'rb').read()
    _, imagebase, dirs, secs = sections(data)

    def rva2off(rva):
        for name, vaddr, vsize, raddr, rsize in secs:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raddr + (rva - vaddr)
        return None

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    off = rva2off(v - imagebase)
    if off is None:
        return False
    for insn in md.disasm(data[off:off + 16], v):
        if insn.id == 0:
            return False
        if insn.mnemonic.startswith(('j', 'ret', 'call')):
            return True
    return False


def main():
    args = sys.argv[1:]
    dry = '--dry-run' in args
    opt = {'--repo': REPO, '--module': MODULE, '--variant': VARIANT}
    rest = []
    it_ = iter(args)
    for a in it_:
        if a in opt:
            opt[a] = next(it_)
        elif not a.startswith('--'):
            rest.append(a)
    args = rest
    set_paths(opt['--repo'], opt['--module'], opt['--variant'])
    rounds = int(args[0]) if args else 5
    for it in range(rounds):
        known = load_sci(SCI) | load_sci(NODATA)
        rc = run_sr()
        code = emitted_code()
        targets = reloc_targets()
        missing = sorted(t for t in targets if t not in code and t not in known)
        err = open(os.path.join(SR_DIR, 'sr.err'), encoding='latin1').read()
        print('iteration %d: rc=%d code=%d missing=%d errors=%d'
              % (it, rc, len(code), len(missing), err.count('Error:')))
        if not missing:
            print('the code coverage is complete')
            return 0
        bad = []
        for t in missing:
            if reads_as_code(t):
                print('   + loc_%X  (relocation at %s)'
                      % (t, ', '.join('%08X' % p for p in targets[t][:3])))
                if not dry:
                    with open(SCI, 'a', encoding='utf-8') as f:
                        f.write('loc_%X\n' % t)
            else:
                print('   ! loc_%X  (relocation at %s) - not code, do-not-interpret'
                      % (t, ', '.join('%08X' % p for p in targets[t][:3])))
                bad.append(t)
                if not dry:
                    with open(NODATA, 'a', encoding='utf-8') as f:
                        f.write('loc_%X\n' % t)
        if bad:
            print('stopped: %d relocation targets are not the start of a function' % len(bad))
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())