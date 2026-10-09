#! /usr/bin/env python3
#
# Based on the per-game compact_source*.py scripts of SR.
#
#  Copyright (C) 2016-2026 Roman Pauer
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

# Universal compact_source: compress data segments for the different backends
# used by SR (nasm/x86/x64, gas/arm, llasm).
#
# Usage:
#   compact_source.py --dialect llasm --module <module> --compact compact_file
#
# Or with environment variables: DIALECT, MODULE, COMPACT
#
# If --compact is given, it is a file with one segment name per line
# (comments '#' allowed). If absent, all files matching seg*_*.llinc/seg*.s/
# seg*.inc in cwd are considered.
#

import os
import sys

DIALECT_NASM = 'nasm'
DIALECT_GAS = 'gas'
DIALECT_LLASM = 'llasm'

def is_comment(line):
    s = line.strip()
    return s == '' or s.startswith('#')

def load_list(path):
    lst = []
    try:
        with open(path, 'rt', encoding='utf-8', errors='replace') as f:
            for line in f:
                s = line.rstrip('\n\r')
                t = s.split('#', 1)[0].strip()
                if t:
                    lst.append(t)
    except Exception:
        pass
    return lst

def find_all_segments(dialect):
    # common segment patterns by dialect
    candidates = []
    for name in os.listdir('.'):
        if dialect == DIALECT_LLASM:
            if name.startswith('seg') and (name.endswith('.llinc')):
                candidates.append(name)
        elif dialect == DIALECT_GAS:
            if name.startswith('seg') and (name.endswith('.s') or name.endswith('.inc')):
                candidates.append(name)
        elif dialect == DIALECT_NASM:
            if name.startswith('seg') and (name.endswith('.inc') or name.endswith('.asm') or name.endswith('.nas')):
                candidates.append(name)
    # sort: code before data doesn't matter, but stable by name
    candidates.sort()
    return candidates

def compact_segment_nasm(cfile):
    ctmp = cfile + 'tmp'
    try:
        with open(cfile, 'rt', encoding='utf-8', errors='replace') as fin, \
             open(ctmp, 'wt', encoding='utf-8', errors='replace') as fout:
            inum = 0
            crep = ''
            for cline in fin:
                if inum != 0:
                    if cline == crep:
                        inum += 1
                    else:
                        if inum == 1:
                            fout.write(crep)
                        elif crep.strip().startswith('resb') or crep.strip().startswith('resw') or crep.strip().startswith('resd') or crep.strip().startswith('resq'):
                            # keep as is? times not used for res; but rarely
                            fout.write(crep)
                            inum = 1
                            crep = cline
                            continue
                        elif inum == 2:
                            fout.write(crep)
                            fout.write(crep)
                        else:
                            r = crep.rstrip('\n\r')
                            sp = len(r) - len(r.rstrip())
                            pad = r[ len(r.rstrip()): ] if sp >= 0 else ''
                            if pad == '':
                                # try to keep newline
                                nl = '\n' if r.endswith('\n') else ('\r\n' if r.endswith('\r\n') else '')
                                core = r.rstrip('\n\r')
                                fout.write(core + ' times ' + str(inum) + nl)
                            else:
                                core = r.rstrip()
                                fout.write(core + ' times ' + str(inum) + pad)
                        inum = 0
                if inum == 0:
                    s = cline.strip()
                    if s.startswith('db ') or s.startswith('dw ') or s.startswith('dd ') or s.startswith('dq '):
                        inum = 1
                        crep = cline
                        continue
                    if s.startswith('times ') and s.count('db') + s.count('dw') + s.count('dd') + s.count('dq') >= 1:
                        # already compacted? skip
                        pass
                    fout.write(cline)
            # tail
            if inum != 0:
                if inum == 1:
                    fout.write(crep)
                elif inum == 2:
                    fout.write(crep)
                    fout.write(crep)
                else:
                    r = crep.rstrip('\n\r')
                    pad = r[len(r.rstrip()):] if len(r) > len(r.rstrip()) else ('\n' if r.endswith('\n') or True else '')
                    core = r.rstrip()
                    # default newline
                    nl = '\n'
                    if '\r\n' in r:
                        nl = '\r\n'
                    elif '\n' in r and not r.endswith('\r\n'):
                        nl = '\n'
                    # extract pad properly
                    m = r
                    stripped = m.rstrip('\n\r')
                    pad2 = m[len(stripped):] if len(m) > len(stripped) else nl
                    fout.write(stripped + ' times ' + str(inum) + pad2)
    except Exception as e:
        try:
            os.remove(ctmp)
        except:
            pass
        return False
    try:
        os.remove(cfile)
        os.rename(ctmp, cfile)
    except Exception:
        return False
    return True

def compact_segment_gas(cfile):
    ctmp = cfile + 'tmp'
    try:
        with open(cfile, 'rt', encoding='utf-8', errors='replace') as fin, \
             open(ctmp, 'wt', encoding='utf-8', errors='replace') as fout:
            inum = 0
            crep = ''
            for cline in fin:
                if inum != 0:
                    if cline == crep:
                        inum += 1
                    else:
                        if inum == 1:
                            fout.write(crep)
                        elif crep.strip().startswith('.skip') or crep.strip().startswith('.space'):
                            r = crep.rstrip('\n\r')
                            stripped = r.rstrip('\n\r ')
                            # try to append count? original is .skip N or .space N
                            # but we saw runs of identical lines; just write
                            fout.write(r)
                            inum = 1
                            crep = cline
                            continue
                        elif inum == 2:
                            fout.write(crep)
                            fout.write(crep)
                        else:
                            r = crep.rstrip('\n\r')
                            stripped = r.rstrip('\n\r')
                            pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                            fout.write(stripped + ' / ' + str(inum) + pad)  # not quite; better .rept
                            # but original gas compact in repo varies; keep simple RLE form? use .rept
                            # standard: .rept N\n ... \n .endr  — but here we have single line
                            fout.write('.rept ' + str(inum) + pad)
                            fout.write(crep)
                            fout.write('.endr' + pad)
                        inum = 0
                if inum == 0:
                    s = cline.strip()
                    if s.startswith('.byte') or s.startswith('.word') or s.startswith('.long') or s.startswith('.quad') or s.startswith('.short') or s.startswith('.hword'):
                        inum = 1
                        crep = cline
                        continue
                    if s.startswith('.skip') or s.startswith('.space'):
                        # treat as potential repeat? keep
                        pass
                    fout.write(cline)
            if inum != 0:
                if inum == 1:
                    fout.write(crep)
                elif inum == 2:
                    fout.write(crep)
                    fout.write(crep)
                else:
                    r = crep.rstrip('\n\r')
                    stripped = r.rstrip('\n\r')
                    pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                    fout.write('.rept ' + str(inum) + pad)
                    fout.write(crep)
                    fout.write('.endr' + pad)
    except Exception:
        try:
            os.remove(ctmp)
        except:
            pass
        return False
    try:
        os.remove(cfile)
        os.rename(ctmp, cfile)
    except Exception:
        return False
    return True

def compact_segment_llasm(cfile):
    ctmp = cfile + 'tmp'
    try:
        with open(cfile, 'rt', encoding='utf-8', errors='replace') as fin, \
             open(ctmp, 'wt', encoding='utf-8', errors='replace') as fout:
            inum = 0
            crep = ''
            for cline in fin:
                if inum != 0:
                    if cline == crep:
                        inum += 1
                    else:
                        if inum == 1:
                            fout.write(crep)
                        elif crep.strip().startswith('dskip'):
                            if inum == 1:
                                fout.write(crep)
                            else:
                                r = crep.rstrip('\n\r')
                                stripped = r.rstrip('\n\r')
                                pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                                fout.write('dskip ' + str(inum) + pad)
                        elif inum == 2:
                            fout.write(crep)
                            fout.write(crep)
                        else:
                            r = crep.rstrip('\n\r')
                            stripped = r.rstrip('\n\r')
                            pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                            fout.write(stripped + ' dup ' + str(inum) + pad)
                        inum = 0
                if inum == 0:
                    s = cline.strip()
                    if s.startswith('db '):
                        inum = 1
                        crep = cline
                        continue
                    if s.strip() == 'dskip 1':
                        inum = 1
                        crep = cline
                        continue
                    fout.write(cline)
            if inum != 0:
                if inum == 1:
                    fout.write(crep)
                elif crep.strip().startswith('dskip'):
                    r = crep.rstrip('\n\r')
                    stripped = r.rstrip('\n\r')
                    pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                    fout.write('dskip ' + str(inum) + pad)
                else:
                    r = crep.rstrip('\n\r')
                    stripped = r.rstrip('\n\r')
                    pad = r[len(stripped):] if len(r) > len(stripped) else '\n'
                    fout.write(stripped + ' dup ' + str(inum) + pad)
    except Exception:
        try:
            os.remove(ctmp)
        except:
            pass
        return False
    try:
        os.remove(cfile)
        os.rename(ctmp, cfile)
    except Exception:
        return False
    return True

def compact_segment(cfile, dialect):
    if dialect == DIALECT_LLASM:
        return compact_segment_llasm(cfile)
    if dialect == DIALECT_GAS:
        return compact_segment_gas(cfile)
    if dialect == DIALECT_NASM:
        return compact_segment_nasm(cfile)
    return False

def parse_args(argv):
    import getopt
    try:
        opts, args = getopt.getopt(argv[1:], 'h', ['help', 'dialect=', 'module=', 'compact=', 'list='])
    except getopt.GetoptError:
        usage()
        sys.exit(2)
    dialect = os.environ.get('DIALECT', DIALECT_LLASM)
    module = os.environ.get('MODULE', '')
    compact_list = os.environ.get('COMPACT', '')
    for o, a in opts:
        if o in ('-h', '--help'):
            usage()
            sys.exit(0)
        if o == '--dialect':
            dialect = a
        if o == '--module':
            module = a
        if o == '--compact' or o == '--list':
            compact_list = a
    # normalize
    dialect = dialect.lower()
    if dialect not in (DIALECT_LLASM, DIALECT_GAS, DIALECT_NASM):
        dialect = DIALECT_LLASM
    return dialect, module, compact_list

def usage():
    print('Usage: compact_source.py [--dialect nasm|gas|llasm] [--module NAME] [--compact FILE]')

def main(argv):
    dialect, module, compact_list = parse_args(argv)
    if module:
        try:
            os.rename(os.path.join('.', module + '.asm'), os.path.join('.', module + '.llasm'))
        except Exception:
            pass
    segments = []
    if compact_list and os.path.exists(compact_list):
        segments = load_list(compact_list)
    if not segments:
        segments = find_all_segments(dialect)
    for s in segments:
        if os.path.exists(s):
            compact_segment(s, dialect)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
