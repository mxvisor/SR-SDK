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
  llasm_hashes.py REPO [DEVICE ...] --ref FILE --id ID --renew
                                                     the same lines go to stderr, the listing
                                                     becomes FILE and the lines on stdout say ok
Options: --llasm PATH  the llasm.py to run (default: REPO/SR-SDK/llasm/llasm.py).
         --target NAME the target of a repository of several (scons/game.py): its
                       build/target-NAME/gen, and scons target=NAME.
Devices default to x64-linux and x64-windows. Exit status 1 on a difference.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def collect(repo, devices, llasm, build_target='default'):
    result = []
    gen = os.path.join(repo, 'build', 'gen') if build_target == 'default' else \
        os.path.join(repo, 'build', 'target-' + build_target, 'gen')
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
            out = subprocess.run(['scons', '-n', '-Q', 'stage=bin', 'device=' + device, 'llasm=' + dummy,
                                  'target=' + build_target],
                                 cwd=repo, capture_output=True, text=True).stdout
            for line in out.splitlines():
                if dummy not in line or ' | ' not in line:
                    continue
                line = line.strip()
                if line.startswith("bash -o pipefail -c '") and line.endswith("'"):
                    line = line[len("bash -o pipefail -c '"):-1]    # scons/llasm.SConscript wraps the pipe
                    line = line.split(' || {')[0]                  # and removes the object on failure
                cmd = line.split(' | ')[0].replace('"%s"' % dummy, '"%s" "%s"' % (sys.executable, llasm), 1)
                # the object is `> X` or `-o X`, or the bitcode `-o build/obj/ir/X.bc`
                # (scons/llasm.SConscript); the key stays gen/<name> in all of them
                target = re.search(r'(?:> |-o )(\S+)$', line)
                r = subprocess.run(cmd, shell=True, cwd=repo, capture_output=True)
                key = '%s/%s' % (device, re.sub(r'.*/(?:gen|ir)/([^/]+)\.(?:o|bc)$', r'gen/\1', target.group(1)) if target else '?')
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
    repo, devices, llasm, ref, ident, renew, target = None, [], None, None, None, False, 'default'
    it = iter(argv)
    for arg in it:
        if arg == '--llasm':
            llasm = os.path.abspath(next(it))
        elif arg == '--ref':
            ref = next(it)
        elif arg == '--id':
            ident = next(it)
        elif arg == '--target':
            target = next(it)
        elif arg == '--renew':
            renew = True
        elif repo is None:
            repo = os.path.abspath(arg)
        else:
            devices.append(arg)
    if repo is None:
        print(__doc__, file=sys.stderr)
        return 2
    llasm = llasm or os.path.join(repo, 'SR-SDK', 'llasm', 'llasm.py')
    now = collect(repo, devices or ['x64-linux', 'x64-windows'], llasm, target)
    if ref is None:
        for h, k in now:
            print('%s  %s' % (h, k))
        return 0
    want = []
    if os.path.exists(ref) or not renew:
        with open(ref) as f:
            want = parse(f)
    status = 0
    for kind in ('gen', 'llvm'):
        n, total, first = diff(kind, now, want)
        if n:
            status = 1
        line = 'llasm-%s\t%s\t%s\t%d of %d differ%s' % (kind, ident or repo, 'changed' if n else 'ok', n, total,
                                                       ', first: ' + first if n else '')
        if renew:
            # the old reference's verdict stays in the log; the new one is this listing
            print('was: ' + line, file=sys.stderr)
            line = 'llasm-%s\t%s\tok\t0 of %d differ' % (kind, ident or repo, len([k for _, k in now if k.startswith(kind + '/')]))
        print(line)
    if renew:
        with open(ref, 'w') as f:
            f.writelines('%s  %s\n' % (h, k) for h, k in now)
        return 0
    return status


sys.exit(main(sys.argv[1:]))
