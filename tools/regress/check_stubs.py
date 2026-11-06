#!/usr/bin/env python3
"""check_stubs.py [GAME_ID...]: the stubs of the Win32 API (runtime/win32/winapi/WinApi-stubs.c).

Each stub is an object of its own in libwinapistubs.a, and the linker takes it only
when nothing else of the port defines the function. For each win32 game of games.tsv
(or the ones named), from its pc64-linux build (nm of the objects and of the program):
  - linked:    the stubs the program takes from the library;
  - predicted: the stubs the llasm sources of the build call (`func <name>_c` in the
               game's llasm/ and the runtime's llasm directories), less those the game
               defines: the list a build could know before compiling anything. It has
               to cover what is linked;
  - the stubs the game's own objects define (they are never linked for it).
One row per game: "stubs ID ok|fail DETAIL", and a row "stubs sdk": a stub that the
objects of every built win32 game define is never linked at all and is a failure.
DOS games, and a win32 game with winapi=no, have no row. Exit status 1 on a failure.
"""
import glob, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = os.environ.get('DOS_RECOMP_ROOT') or os.path.dirname(SDK)
DEVICE = 'pc64-linux'


def games():
    out = []
    for line in open(os.path.join(HERE, 'games.tsv')):
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        target = f[9] if len(f) > 9 and f[9] else 'default'
        out.append((f[0], os.path.join(ROOT, f[1]), f[2], target))
    return out


def game_cfg(gamedir):
    cfg = {}
    path = os.path.join(gamedir, 'game.cfg')
    if os.path.exists(path):
        for line in open(path):
            if '=' in line and not line.lstrip().startswith('#'):
                k, v = line.split('=', 1)
                cfg[k.strip()] = v.strip()
    return cfg


def nm(args):
    r = subprocess.run(['nm'] + args, capture_output=True, text=True)
    return r.stdout.splitlines()


def c_names(lines):
    """the <name> of every <name>_c symbol in nm lines"""
    out = set()
    for l in lines:
        sym = l.split()[-1] if l.strip() else ''
        if sym.endswith('_c'):
            out.add(sym[:-2])
    return out


def check(repo, program, target):
    """(status, detail, stubs the game's objects define) of one game, or None"""
    gamedir = os.path.join(repo, 'game' if target == 'default' else 'game-' + target)
    cfg = game_cfg(gamedir)
    if cfg.get('platform') != 'win32' or cfg.get('winapi', 'yes') == 'no':
        return None
    build = os.path.join(repo, 'build' if target == 'default' else 'build/target-' + target)
    objdir = os.path.join(build, 'obj', DEVICE)
    lib = os.path.join(objdir, 'runtime/win32/winapi/libwinapistubs.a')
    binary = os.path.join(repo, 'build/bin', DEVICE, program)
    if not os.path.exists(lib) or not os.path.exists(binary):
        return 'fail', 'not built (%s)' % DEVICE, None

    stubs = c_names(nm(['--defined-only', lib]))
    # the objects of the port, without those of the stubs (runtime/win32/winapi/stubs/)
    stubdir = os.path.join(os.path.dirname(lib), 'stubs') + os.sep
    objs = [o for o in glob.glob(objdir + '/**/*.o', recursive=True) if not o.startswith(stubdir)]
    defined = c_names(nm(['--defined-only'] + objs)) & stubs if objs else set()
    linked = (c_names(nm(['--defined-only', binary])) & stubs) - defined

    # the llasm the build reads: the game's llasm/ first, then the runtime's
    sdk = os.path.join(repo, 'SR-SDK')
    dirs = [os.path.join(gamedir, 'llasm')]
    for d in ('runtime/core/llasm', 'runtime/win32/llasm', 'runtime/win32/winapi/llasm'):
        dirs.append(os.path.join(sdk, d))
    if cfg.get('glide') == 'yes':
        dirs.append(os.path.join(sdk, 'runtime/win32/glide/llasm'))
    seen, called = set(), set()
    for d in dirs:
        for p in sorted(glob.glob(d + '/*.ll*')):
            name = os.path.basename(p)
            if name in seen:
                continue
            seen.add(name)
            called |= set(re.findall(r'\bfunc\s+(\w+)_c\b', open(p, errors='replace').read()))
    predicted = (called & stubs) - defined

    missed = sorted(linked - predicted)
    detail = 'library %d, linked %d, predicted %d, defined by the game %d' % (
        len(stubs), len(linked), len(predicted), len(defined))
    if missed:
        return 'fail', detail + '; linked, not predicted: ' + ' '.join(missed), defined
    return 'ok', detail, defined


def main():
    ids = sys.argv[1:]
    bad = 0
    shadowed = None     # stubs every built win32 game defines itself
    for gid, repo, program, target in games():
        if not os.path.isdir(repo):
            continue
        result = check(repo, program, target)
        if result is None:
            continue
        status, detail, defined = result
        if defined is not None:
            shadowed = set(defined) if shadowed is None else shadowed & defined
        if ids and gid not in ids:
            continue
        print('stubs\t%s\t%s\t%s' % (gid, status, detail))
        bad += status != 'ok'
    if shadowed:
        print('stubs\tsdk\tfail\tdefined by every game, never linked: ' + ' '.join(sorted(shadowed)))
        bad += 1
    else:
        print('stubs\tsdk\tok\tno stub that every game defines itself')
    return 1 if bad else 0


sys.exit(main())
