#!/usr/bin/env python3
"""Hashes of a game's llasm output, to tell whether a change in SR, in llasm.py
or in the build altered what the port is made of.

For a built game repository (its build/gen must exist: `scons stage=gen`):
  - sha256 of every file of build/gen/**: SR's compacted output;
  - sha256 of the LLVM IR that llasm.py prints for every .llasm, run with the
    commands scons would run (taken from `scons -n`), for each device.

  llasm_hashes.py REPO [DEVICE ...]                  print "<sha256>  <key>" lines
  llasm_hashes.py REPO [DEVICE ...] --ref FILE --id ID
                                                     compare with a saved listing and print
                                                     two check lines for run_all (TAB-separated):
                                                       llasm-gen   ID  ok|changed  detail
                                                       llasm-llvm  ID  ok|changed  detail
Options: --llasm PATH  the llasm.py to run (default: REPO/SR-SDK/llasm/llasm.py).
Devices default to pc64-linux and pc64-windows. Exit status 1 on a difference.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def collect(repo, devices, llasm):
    result = []
    gen = os.path.join(repo, 'build', 'gen')
    for dirpath, dirs, files in os.walk(gen):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            with open(path, 'rb') as f:
                result.append((sha(f.read()), 'gen/' + os.path.relpath(path, gen)))
    dummy = os.path.join(tempfile.gettempdir(), 'llasm_dummy_%d' % os.getpid())
    open(dummy, 'a').close()
    try:
        for device in devices:
            # llasm=<file> changes the action of every .llasm -> .o, so a dry run
            # prints them all; the file is replaced by llasm.py below
            out = subprocess.run(['scons', '-n', '-Q', 'stage=bin', 'device=' + device, 'llasm=' + dummy],
                                 cwd=repo, capture_output=True, text=True).stdout
            for line in out.splitlines():
                if dummy not in line or ' | ' not in line:
                    continue
                line = line.strip()
                if line.startswith("bash -o pipefail -c '") and line.endswith("'"):
                    line = line[len("bash -o pipefail -c '"):-1]    # scons/llasm.SConscript wraps the pipe
                    line = line.split(' || {')[0]                  # and removes the object on failure
                cmd = line.split(' | ')[0].replace('"%s"' % dummy, '"%s" "%s"' % (sys.executable, llasm), 1)
                # the object is `> X` or `-o X`, depending on the SDK's scons/game.py
                target = re.search(r'(?:> |-o )(\S+)$', line)
                r = subprocess.run(cmd, shell=True, cwd=repo, capture_output=True)
                key = '%s/%s' % (device, re.sub(r'.*/(gen/[^/]+)\.o$', r'\1', target.group(1)) if target else '?')
                result.append((sha(r.stdout), 'llvm/%s rc=%d' % (key, r.returncode)))
    finally:
        os.unlink(dummy)
    return result


def parse(lines):
    return [tuple(l.rstrip('\n').split('  ', 1)) for l in lines if l.strip() and not l.startswith('#')]


def diff(kind, now, ref):
    """Returns (number of differing keys, total keys, first differing key)."""
    a = {k: h for h, k in now if k.startswith(kind + '/')}
    b = {k: h for h, k in ref if k.startswith(kind + '/')}
    keys = sorted(set(a) | set(b))
    bad = [k for k in keys if a.get(k) != b.get(k)]
    return len(bad), len(keys), bad[0] if bad else ''


def main(argv):
    repo, devices, llasm, ref, ident = None, [], None, None, None
    it = iter(argv)
    for arg in it:
        if arg == '--llasm':
            llasm = os.path.abspath(next(it))
        elif arg == '--ref':
            ref = next(it)
        elif arg == '--id':
            ident = next(it)
        elif repo is None:
            repo = os.path.abspath(arg)
        else:
            devices.append(arg)
    if repo is None:
        print(__doc__, file=sys.stderr)
        return 2
    llasm = llasm or os.path.join(repo, 'SR-SDK', 'llasm', 'llasm.py')
    now = collect(repo, devices or ['pc64-linux', 'pc64-windows'], llasm)
    if ref is None:
        for h, k in now:
            print('%s  %s' % (h, k))
        return 0
    with open(ref) as f:
        want = parse(f)
    status = 0
    for kind in ('gen', 'llvm'):
        n, total, first = diff(kind, now, want)
        if n:
            status = 1
        print('llasm-%s\t%s\t%s\t%d of %d differ%s' % (kind, ident or repo, 'changed' if n else 'ok', n, total,
                                                       ', first: ' + first if n else ''))
    return status


sys.exit(main(sys.argv[1:]))
