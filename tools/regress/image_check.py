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

"""Converts each disc image of images.tsv with tools/image/image.py and hashes
the result: the cue sheet, the .bin when one is written, the data track as an
ISO and the sha256 of every file of its tree.

    image_check.py [GAME...]

One line per image, TAB-separated, as the other checks print:
    image  GAME  ok|fail  sha256-of-the-result  files=N
run_all.sh compares the hash with the reference run: a change is a change in
what the converter writes. The output is built in a directory of its own under
$REGRESS_OUT (default <workspace>/cache/sdk-regress) and removed afterwards."""
import hashlib
import os
import shutil
import subprocess
import sys

from regress_conf import rows

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(os.path.dirname(HERE))
ROOT = os.environ.get('DOS_RECOMP_ROOT') or os.path.dirname(SDK)
OUT = os.environ.get('REGRESS_OUT') or os.path.join(os.environ.get('DOS_RECOMP_ROOT') or os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')), 'cache', 'sdk-regress')
TOOL = os.path.join(SDK, 'tools', 'image', 'image.py')


def sha_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def check(game, image):
    work = os.path.join(OUT, 'image-' + game)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    source = os.path.join(ROOT, image)
    if not os.path.isfile(source):
        return 'fail', 'no image: %s' % image
    run = subprocess.run([sys.executable, '-I', TOOL, 'disc', source, '-o', work, '--iso',
                          '--extract', os.path.join(work, 'tree')], capture_output=True, text=True)
    if run.returncode:
        return 'fail', (run.stderr.strip().splitlines() or ['exit %d' % run.returncode])[-1]
    total = hashlib.sha256()
    files = 0
    for name in sorted(os.listdir(work)):
        path = os.path.join(work, name)
        if name == 'tree':
            for base, _, names in sorted(os.walk(path)):
                for item in sorted(names):
                    full = os.path.join(base, item)
                    total.update(('%s %s\n' % (os.path.relpath(full, path), sha_file(full))).encode())
                    files += 1
        elif os.path.islink(path):
            continue                        # the image itself, linked next to its cue
        elif name.endswith('.cue'):
            text = open(path, newline='').read().replace('\r\n', '\n')
            total.update(('%s\n%s' % (name, text)).encode())
        else:
            total.update(('%s %s\n' % (name, sha_file(path))).encode())
    shutil.rmtree(work, ignore_errors=True)
    return 'ok', '%s\tfiles=%d' % (total.hexdigest(), files)


def main():
    wanted = sys.argv[1:]
    for game, image in (f[:2] for f in rows('images.tsv')):
        if wanted and game not in wanted:
            continue
        status, detail = check(game, image)
        print('image\t%s\t%s\t%s' % (game, status, detail), flush=True)


if __name__ == '__main__':
    main()
