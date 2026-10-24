#!/usr/bin/env python3
"""Generate the Win32 import shim of a port that does not use the SDK's winapi layer
for everything (or, with winapi=yes, for the imports the layer lacks).

Input: the game's tools/import_args.tsv, one import per line, tab separated:
name, dll, argc (the stdcall argument count) and optionally `cdecl` (the caller
removes the arguments, as with VFWAPIV functions such as ICDecompress).  Lines starting with '#' are
settings or comments; the settings are:

    # title: Some Game                  the game's name in the generated comments
    # exe: GAME.EXE                     the original executable
    # extern: yes                       also write game/llasm/extern.llinc
    # handwritten: skip                 leave out the imports that the SDK's winapi layer
                                        (unless winapi=no) or another
                                        game/llasm/WinApi-*-asm.llasm already bridges
    # stubs: yes                        also write game/WinApi-game.c (see below)
    # alias: wsprintfA sprintf_asm2c    (any number) extern.llinc also gets `define wsprintfA
                                        sprintf_asm2c`, for an import that has an existing bridge

Writes, under the game repository's game/:
  llasm/WinApi-game-asm.llasm  one wrapper proc per import, popping its stdcall
                               arguments before returning into the emulated caller
  WinApi-game.h                prototypes of the C side (<name>_c); written only when
                               missing (afterwards it is the game's own file: the tool
                               lists the prototypes it lacks)
  llasm/extern.llinc           (extern: yes) `define <name> <name>_asm2c` per import
  WinApi-game.c                (stubs: yes) a stub returning 0 for every function
                               that no other game/*.c and not the SDK's
                               runtime/win32/winapi/WinApi-*.c (the layer and the stubs of the
                               common imports) defines; not written when none is
                               left; refused when the file
                               exists -- it is the game's own code after the first
                               run, so add new functions to it by hand

Run from the game repository:  python3 SR-SDK/tools/gen_import_shim.py [--root DIR]
"""
import datetime
import glob
import os
import re
import sys

ROOT = os.getcwd()
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--root':
        ROOT = os.path.abspath(args.pop(0))
    else:
        sys.exit(__doc__)
GAME = os.path.join(ROOT, 'game')
TSV = os.path.join(ROOT, 'tools', 'import_args.tsv')
TODAY = datetime.date.today().isoformat()

cfg = {}
aliases = []
rows = []
for line in open(TSV, encoding='utf-8'):
    line = line.rstrip('\n')
    if line.startswith('#'):
        m = re.match(r'#\s*(\w+)\s*:\s*(.*)$', line)
        if m and m.group(1) == 'alias':
            aliases.append(m.group(2).split())
        elif m:
            cfg[m.group(1)] = m.group(2).strip()
        continue
    if not line.strip():
        continue
    f = line.split('\t')
    rows.append((f[0], f[1], int(f[2]), len(f) > 3 and f[3] == 'cdecl'))
title = cfg.get('title', 'Game')
exe = cfg.get('exe', 'the original executable')


def cname(sym):
    return sym.replace('.', '_')


def write(path, lines, final_newline=True):
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + ('\n' if final_newline else ''))


ASM = os.path.join(GAME, 'llasm', 'WinApi-game-asm.llasm')
if cfg.get('handwritten') == 'skip':
    handwritten = set()
    sdk = os.path.join(ROOT, 'SR-SDK', 'runtime', 'win32', 'winapi', 'llasm')
    for path in (glob.glob(os.path.join(GAME, 'llasm', 'WinApi-*-asm.llasm')) +
                 (glob.glob(os.path.join(sdk, '*-asm.llasm'))
                  if 'winapi=no' not in open(os.path.join(GAME, 'game.cfg')).read() else [])):
        if os.path.abspath(path) == os.path.abspath(ASM):
            continue
        text = open(path, errors='replace').read()
        handwritten |= set(re.findall(r'proc\s+([A-Za-z0-9_.]+)_asm2c\s+public', text))
    generated = [r for r in rows if r[0] not in handwritten]
else:
    handwritten = set()
    generated = rows

# The SDK's winapi layer has C functions that take host pointers (void *ptc) where the
# bridges of the layer itself pass guest addresses: a bridge to such a function declares
# these parameters `*aN`, which llasm turns into host pointers.
SDK_C = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'runtime', 'win32', 'winapi')
sdk_protos = {}
if 'winapi=no' not in open(os.path.join(GAME, 'game.cfg')).read():
    for path in glob.glob(os.path.join(SDK_C, 'WinApi-*.c')):
        for m in re.finditer(r'^\w[\w \*]*?\bCCALL\s+(\w+_c)\s*\(([^)]*(?:\([^)]*\)[^)]*)*)\)\s*$',
                            open(path, errors='replace').read(), re.M):
            sdk_protos[m.group(1)] = m.group(2)


def pointer_params(fn, argc):
    """indexes of the parameters of the SDK's definition of fn that are host pointers"""
    text = sdk_protos.get(fn)
    if text is None or text.strip() in ('', 'void'):
        return set()
    params = re.split(r',(?![^(]*\))', text)
    return {i for i, prm in enumerate(params[:argc]) if '*' in re.sub(r'PTR32\([^)]*\)', '', prm)}


# --- llasm wrappers ---
l = [';;',
     ';;  %s Win32 import bridges, generated from the import table of %s' % (title, exe),
     ';;  (argc from the MinGW x86 headers) by SR-SDK/tools/gen_import_shim.py.',
     ';;  Each proc pops its stdcall arguments before returning into the emulated caller.',
     ';;  Generated: %s' % TODAY, ';;', '',
     'include llasm.llinc', 'include Game-asm-calls.llinc', '']
# Game-asm-calls.llinc has Call_Asm_Stack0..9, 12 and 15
for n in sorted({a for _, _, a, _ in generated} & {10, 11}):
    l.append('; Call_Asm_Stack%d is not in Game-asm-calls.llinc. Same shape as the others.' % n)
    l.append('macro Call_Asm_Stack%d proc_name' % n)
    for i in range(n):
        off = '' if i == 0 else ' + %d*4' % i
        l.append('    add tmp%d, esp, 4%s' % (i, off))
        l.append('    load tmp%d, tmp%d, 4' % (i, i))
    l.append('    call \\proc_name ' + ', '.join('tmp%d' % i for i in range(n)))
    l.append('    mov eax, tmp0')
    l.append('endm')
    l.append('')
for n, dll, argc, cdecl in generated:
    fn = cname(n) + '_c'
    l.append('; (%s)' % dll)
    ptrs = pointer_params(fn, argc)
    l.append('func %s%s' % (fn, (' ' + ', '.join(('*a%d' if i in ptrs else 'a%d') % i for i in range(argc))) if argc else ''))
    l.append('proc %s_asm2c public' % n)
    l.append('')
    l.append('        Call_Asm_Stack%d %s' % (argc, fn))
    l.append('')
    if cdecl:
        l.append('        ; %s is cdecl: the caller removes its arguments' % n)
    l.append('        Call_Asm_Return %d' % (0 if cdecl else argc))
    l.append('')
    l.append('endp ; end procedure %s_asm2c' % n)
    l.append('')
write(ASM, l, final_newline=False)

# --- extern.llinc ---
if cfg.get('extern') == 'yes':
    ext = os.path.join(GAME, 'llasm', 'extern.llinc')
    old = open(ext, encoding='utf-8').read() if os.path.exists(ext) else ''
    # lines other than the generated header and defines are the game's own: keep them
    keep = [x for x in old.split('\n')
            if x.strip() and not x.startswith(';;') and not x.startswith('define ')]
    e = [';;',
         ';;  %s import aliases, generated by SR-SDK/tools/gen_import_shim.py: each' % title,
         ';;  name imported by %s resolves to its bridge proc.' % exe,
         ';;  Generated: %s' % TODAY, ';;', '']
    if keep:
        e += keep + ['']
    for n, dll, argc, _ in sorted(rows, key=lambda r: cname(r[0])):
        e.append('define %s %s_asm2c' % (n, n))
    for name, target in aliases:
        e.append('define %s %s' % (name, target))
    write(ext, e)

# --- header ---
h = ['/*',
     ' *  %s Win32 import shim - generated from the import table of %s' % (title, exe),
     ' *  by SR-SDK/tools/gen_import_shim.py. Generated: %s' % TODAY,
     ' */', '',
     '#if !defined(_WINAPI_GAME_H_INCLUDED_)', '#define _WINAPI_GAME_H_INCLUDED_', '',
     '#include "platform.h"', '#include <stdint.h>', '',
     '#ifdef __cplusplus', 'extern "C" {', '#endif', '']
for n, dll, argc, _ in generated:
    params = ', '.join('uint32_t a%d' % i for i in range(argc)) if argc else 'void'
    h.append('uint32_t CCALL %s_c(%s);' % (cname(n), params))
h += ['', '#ifdef __cplusplus', '}', '#endif', '', '#endif']
hpath = os.path.join(GAME, 'WinApi-game.h')
if not os.path.exists(hpath):
    write(hpath, h)
else:
    have = open(hpath, errors='replace').read()
    missing = [cname(n) + '_c' for n, _, _, _ in generated
               if not re.search(r'\b%s_c\s*\(' % re.escape(cname(n)), have)]
    if missing:
        print('WinApi-game.h lacks prototypes for: ' + ' '.join(missing))

# --- stubs ---
if cfg.get('stubs') == 'yes':
    cpath = os.path.join(GAME, 'WinApi-game.c')
    if os.path.exists(cpath):
        print('WinApi-game.c exists, not touched')
    else:
        implemented = set()
        # the SDK's own winapi layer counts too (WinApi-*.c, with the stubs of
        # WinApi-stubs.c), unless the game leaves the layer out
        sdk_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                               'runtime', 'win32', 'winapi')
        use_sdk = 'winapi=no' not in open(os.path.join(GAME, 'game.cfg')).read()
        for path in glob.glob(os.path.join(GAME, '*.c')) + (glob.glob(os.path.join(sdk_dir, '*.c')) if use_sdk else []):
            implemented |= set(re.findall(r'^\w[\w \*]*?\bCCALL\s+(\w+_c)\s*\(',
                                          open(path, errors='replace').read(), re.M))
        todo = [n for n in generated if cname(n[0]) + '_c' not in implemented]
        c = ['/*', ' *  %s Win32 import shim - link-correct stubs.' % title, ' *',
             ' *  Every import resolves and every wrapper pops the right number of stdcall',
             ' *  arguments, so the port links. Replace a stub by the real function.',
             ' *', ' *  Generated: %s' % TODAY, ' */', '',
             '#include "WinApi-game.h"', '',
             '#ifdef __cplusplus', 'extern "C" {', '#endif', '']
        for n, dll, argc, _ in todo:
            fn = cname(n) + '_c'
            params = ', '.join('uint32_t a%d' % i for i in range(argc)) if argc else 'void'
            c += ['/* %s (%s) */' % (n, dll), 'uint32_t CCALL %s(%s)' % (fn, params), '{']
            if argc:
                c.append('    (void)' + '; (void)'.join('a%d' % i for i in range(argc)) + ';')
            c += ['    return 0;', '}', '']
        c += ['#ifdef __cplusplus', '}', '#endif']
        if todo:
            write(cpath, c)
        else:
            print('every import is implemented or stubbed by the SDK: no WinApi-game.c')

print('imports: %d, bridged here: %d, bridged by the SDK or by hand: %d'
      % (len(rows), len(generated), len(rows) - len(generated)))
