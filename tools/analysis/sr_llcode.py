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

"""The SR input of a DOS (LE) game, grown from SR's llasm output: the code that
SR still writes as data, and the flags at the start of a region.

Run in a -recomp directory; each command runs scons itself:

  sr_llcode.py candidates [--kinds mov,PUSH,daddr] [--range LO HI]
      code SR wrote as data (`dlabel loc_X` in an executable object) whose
      address the program takes: `mov reg, loc_X`, `PUSH loc_X`,
      `daddr loc_X` in a table.  A label that is loaded
      or stored as a variable, or is the base of a memory operand, is left out.
      Prints `loc_X kinds` lines; these are only candidates.
  sr_llcode.py add FILE
      adds the labels of FILE (one per line, the first word) to
      fixup_interpret_as_code.sci one at a time: SR's "unknown flags on start of
      region" is answered with a line of instruction_flags.sci (the routine
      reads the flags its caller set, clc/stc before an indirect call), any
      other error of SR, or of llasm.py on SR's output (data that SR decodes
      without a word, two zero bytes as `add al, [eax]`), drops the label.
  sr_llcode.py bisect
      SR (or llasm.py) fails on fixup_interpret_as_code.sci: finds the lines
      that make it fail, one at a time, and moves them to <sci>.bad.
  sr_llcode.py grow [--rejected FILE] [--kinds mov,PUSH]
      candidates (of the kinds mov,PUSH by default), all added at once; when
      SR or llasm.py fails, one at a time as add does (the dropped ones go to
      FILE, default build/sr_llcode_rejected.txt, and are never tried again);
      until nothing new.
  sr_llcode.py flags
      SR stops at "unknown flags on start of region": when the region begins
      with a flag reader (jcc, adc, sbb, setcc, cmovcc) right after a call,
      the callee returns a result in the flags; a line `loc_<return>,0x00,
      <mask>` of instruction_flags.sci tells SR so.  Repeats until SR passes
      or a region it cannot explain (printed with the instructions there).

--module and --variant choose the SR input (default: the only one of
game/recomp/).  SR does what fixup_interpret_as_code.sci says, also with a
table: SR's warning "is in fixup_interpret_as_code.sci, but ... reads or writes
it as memory" and sr_codecheck.py find those; check every list grown here with
it, and with the program running (a jump table of code inside a routine, a
table of offsets, data between routines all look like code)."""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FLAG_READER = re.compile(r'^(?:j(?!mp)\w+|adc|sbb|set\w+|cmov\w+|rcl|rcr|pushf\w*|lahf)$')
UNKNOWN_FLAGS = re.compile(r'Error: unknown flags on start of region - \d+ - \d+ \(0x([0-9a-f]+)\) - 0x([0-9a-f]+)')


class Input:
    """The paths of one SR input of the game."""

    def __init__(self, module, variant):
        inputs = sorted(glob.glob('game/recomp/*/*/'))
        if not inputs:
            sys.exit('no game/recomp/<module>/<variant>/ here: run it in a -recomp directory')
        if module is None or variant is None:
            match = [p for p in inputs
                     if (module is None or p.split('/')[2] == module) and (variant is None or p.split('/')[3] == variant)]
            if len(match) != 1:
                sys.exit('choose one with --module and --variant: %s' % ', '.join(p[len('game/recomp/'):-1] for p in inputs))
            module, variant = match[0].split('/')[2:4]
        self.module, self.variant = module, variant
        sci = os.path.join('game', 'recomp', module, variant, 'llasm')
        self.code_sci = os.path.join(sci, 'fixup_interpret_as_code.sci')
        self.flags_sci = os.path.join(sci, 'instruction_flags.sci')
        self.gen = os.path.join('build', 'gen', module, 'llasm')
        self.err = os.path.join('build', 'recomp', module, 'llasm', 'sr.err')
        exes = glob.glob(os.path.join('build', 'input', module, '*'))
        self.exe = exes[0] if len(exes) == 1 else None

    def image(self):
        import exe_image
        if self.exe is None:
            subprocess.run(['scons', 'stage=input'], capture_output=True)
            exes = glob.glob(os.path.join('build', 'input', self.module, '*'))
            if len(exes) != 1:
                sys.exit('no build/input/%s/<executable>: `scons stage=input` failed' % self.module)
            self.exe = exes[0]
        return exe_image.load(self.exe)


def read_lines(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return [line.rstrip('\n') for line in f if line.strip()]


def write_lines(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + ('\n' if lines else ''))


def append_line(path, line):
    lines = read_lines(path)
    write_lines(path, lines + [line])


def label_of(line):
    return line.split(';', 1)[0].split('#', 1)[0].split(',')[0].strip()


def scons(stage):
    return subprocess.run(['scons', 'stage=' + stage], capture_output=True, text=True).returncode == 0


def llasm_error(inp):
    """'' when SR's output of the module passes llasm.py (data that SR took for
    code without a word, `add al, [eax]` of two zero bytes, fails there), else
    llasm.py's message"""
    if not scons('gen'):
        return '`scons stage=gen` failed'
    here = os.getcwd()
    includes = [os.path.join(here, 'game', 'llasm'),
                os.path.join(here, 'SR-SDK', 'runtime', 'core', 'llasm'),
                os.path.join(here, 'SR-SDK', 'runtime', 'dos', 'llasm'),
                os.path.join(here, 'SR-SDK', 'SR', 'SR', 'llasm-support')]
    command = [sys.executable, os.path.join('SR-SDK', 'llasm', 'llasm.py'),
               os.path.join(inp.gen, inp.module + '.llasm'), '-m64', '-pic', '-ptrofs']
    for include in includes:
        command += ['-I', include]
    r = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    return '' if r.returncode == 0 else ('llasm.py: ' + ' '.join(r.stderr.split('\n')[:2]).strip())


def sr_errors(inp):
    if not os.path.exists(inp.err):
        return ''
    with open(inp.err, errors='replace') as f:
        return f.read()


# --- candidates ---------------------------------------------------------------

def candidates(inp, kinds, code_range):
    if not scons('gen'):
        sys.exit('`scons stage=gen` fails: fix SR first (bisect)')
    if code_range:
        ranges = [code_range]
    else:
        ranges = inp.image().code
    in_code = lambda a: any(lo <= a < hi for lo, hi in ranges)
    files = sorted(glob.glob(os.path.join(inp.gen, '*.llinc')))
    text = [open(path, errors='replace').read() for path in files]
    dlabels = set()
    for t in text:
        for m in re.finditer(r'^dlabel loc_([0-9A-Fa-f]+)', t, re.M):
            a = int(m.group(1), 16)
            if in_code(a):
                dlabels.add(a)
    refs = {}
    for t in text:
        for line in t.split('\n'):
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            word = line.split()[0]
            if word in ('dlabel', 'proc', 'endp', 'datalabel'):
                continue
            if word == 'daddr':
                kind = 'daddr'
            elif line.startswith('mov tmpadr,'):
                kind = 'base'           # the base of a memory operand: a table or a variable
            else:
                kind = word
            for m in re.finditer(r'\bloc_([0-9A-Fa-f]+)\b', line):
                a = int(m.group(1), 16)
                if a in dlabels:
                    refs.setdefault(a, set()).add(kind)
    out = []
    for a in sorted(refs):
        k = refs[a]
        if 'base' in k or any(x.startswith(('load', 'store')) for x in k):
            continue
        if kinds is None or k & kinds:
            out.append(('loc_%X' % a, sorted(k)))
    return out


# --- add, bisect --------------------------------------------------------------

def add(inp, labels, quiet=False):
    """adds each label; returns the labels SR rejected"""
    have = {label_of(line) for line in read_lines(inp.code_sci)}
    rejected = []
    for label in labels:
        if label in have:
            continue
        before_flags = read_lines(inp.flags_sci)
        append_line(inp.code_sci, label)
        added_flags = []
        while True:
            if scons('recomp'):
                error = llasm_error(inp)
                if not error:
                    if not quiet:
                        print('%s: taken%s' % (label, ', %d flag lines' % len(added_flags) if added_flags else ''))
                    have.add(label)
                    break
                found = []
            else:
                error = ''
                found = UNKNOWN_FLAGS.findall(sr_errors(inp))
            found = [(a, m) for a, m in found if 'loc_%X' % int(a, 16) not in {label_of(x) for x in added_flags}]
            if not found:
                errors = [error] if error else [line for line in sr_errors(inp).split('\n') if line.startswith('Error')][:2]
                print('%s: dropped: %s' % (label, '; '.join(errors) or 'scons failed'))
                write_lines(inp.code_sci, [x for x in read_lines(inp.code_sci) if label_of(x) != label])
                write_lines(inp.flags_sci, before_flags)
                rejected.append(label)
                break
            for a, mask in found:
                line = 'loc_%X,0x00,0x%s ; the flags at the start of %s (sr_llcode.py add)' % (int(a, 16), mask, label)
                added_flags.append(line)
                append_line(inp.flags_sci, line)
    return rejected


def bisect(inp):
    """moves the lines that make SR fail to <sci>.bad; returns them"""
    sci = inp.code_sci
    lines = read_lines(sci)
    shutil.copyfile(sci, sci + '.orig')

    def ok(subset):
        write_lines(sci, subset)
        return scons('recomp') and not llasm_error(inp)

    bad = []
    if not ok([]):
        write_lines(sci, lines)
        sys.exit('SR fails even with an empty %s: not a line of it' % sci)
    while not ok(lines):
        lo, hi = 0, len(lines)          # the shortest failing prefix is lines[:hi]
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if ok(lines[:mid]):
                lo = mid
            else:
                hi = mid
        print('fails with: %s' % lines[hi - 1])
        bad.append(lines.pop(hi - 1))
    write_lines(sci, lines)
    if bad:
        with open(sci + '.bad', 'a') as f:
            f.write('\n'.join(bad) + '\n')
    os.remove(sci + '.orig')
    print('kept %d lines, %d to %s.bad' % (len(lines), len(bad), sci))
    return bad


def grow(inp, rejected_path, kinds):
    rejected = set(label_of(x) for x in read_lines(rejected_path))
    for rnd in range(1, 20):
        kept = {label_of(x) for x in read_lines(inp.code_sci)}
        new = [label for label, _ in candidates(inp, kinds, None) if label not in kept and label not in rejected]
        print('round %d: %d new candidates' % (rnd, len(new)))
        if not new:
            break
        write_lines(inp.code_sci, read_lines(inp.code_sci) + new)
        if not scons('recomp') or llasm_error(inp):
            # one at a time: the flags answered, what breaks SR or llasm dropped
            write_lines(inp.code_sci, [x for x in read_lines(inp.code_sci) if label_of(x) not in new])
            bad = add(inp, new, quiet=True)
        else:
            bad = []
        rejected.update(bad)
        if bad:
            os.makedirs(os.path.dirname(rejected_path) or '.', exist_ok=True)
            with open(rejected_path, 'a') as f:
                f.write('\n'.join(bad) + '\n')
    print('%s: %d lines' % (inp.code_sci, len(read_lines(inp.code_sci))))


# --- flags --------------------------------------------------------------------

def flags(inp, max_steps=100):
    import capstone
    img = inp.image()
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for _ in range(max_steps):
        passed = scons('recomp')
        m = UNKNOWN_FLAGS.search(sr_errors(inp))
        if not m:
            print('no "unknown flags on start of region" left: %s' % ('SR passes' if passed else 'other errors of SR remain'))
            return 0
        addr, mask = int(m.group(1), 16), int(m.group(2), 16)
        ins = list(cs.disasm(img.bytes_at(addr, 0x40), addr))[:8]
        if ins and FLAG_READER.match(ins[0].mnemonic):
            # a region that begins with the reader: the call (or a replacement) just before it
            line = 'loc_%X,0x00,0x%02X ; %s at the start of a region' % (addr, mask, ins[0].mnemonic)
        else:
            k = next((i for i, x in enumerate(ins) if x.mnemonic == 'call'), None)
            if k is None or k + 1 >= len(ins) or not FLAG_READER.match(ins[k + 1].mnemonic):
                print('loc_%X: cannot explain the flags 0x%X here:' % (addr, mask))
                for x in ins:
                    print('    %08X  %s %s' % (x.address, x.mnemonic, x.op_str))
                return 1
            line = 'loc_%X,0x00,0x%02X ; %s after the call at %X' % (ins[k + 1].address, mask, ins[k + 1].mnemonic, ins[k].address)
        if label_of(line) in {label_of(x) for x in read_lines(inp.flags_sci)}:
            print('%s is in %s already, SR still asks: check it by hand' % (label_of(line), inp.flags_sci))
            return 1
        print(line)
        append_line(inp.flags_sci, line)
    return 1


def main():
    doc = __doc__ or ''
    ap = argparse.ArgumentParser(description=doc.split('\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=doc.split('\n\n', 1)[-1])
    ap.add_argument('command', choices=('candidates', 'add', 'bisect', 'grow', 'flags'))
    ap.add_argument('file', nargs='?', help='add: the labels to add')
    ap.add_argument('--module')
    ap.add_argument('--variant')
    ap.add_argument('--kinds', help='candidates, grow: only these kinds of reference (mov,PUSH,daddr,...)')
    ap.add_argument('--range', nargs=2, metavar=('LO', 'HI'), help='candidates: the code addresses (hex), default the executable objects')
    ap.add_argument('--rejected', default=os.path.join('build', 'sr_llcode_rejected.txt'), help='grow: the labels SR rejected')
    a = ap.parse_args()
    inp = Input(a.module, a.variant)
    kinds = set(a.kinds.split(',')) if a.kinds else None
    if a.command == 'candidates':
        rng = (int(a.range[0], 16), int(a.range[1], 16)) if a.range else None
        for label, k in candidates(inp, kinds, rng):
            print('%s %s' % (label, ','.join(k)))
    elif a.command == 'add':
        if not a.file:
            ap.error('add needs FILE')
        add(inp, [label_of(x) for x in read_lines(a.file) if label_of(x).startswith('loc_')])
    elif a.command == 'bisect':
        bisect(inp)
    elif a.command == 'grow':
        grow(inp, a.rejected, kinds or {'mov', 'PUSH'})
    else:
        return flags(inp)
    return 0


if __name__ == '__main__':
    sys.exit(main())
