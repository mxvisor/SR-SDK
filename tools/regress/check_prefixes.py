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

"""check_prefixes.py [GAME_ID|sdk...]: no file names a game it does not belong to.

The markers of the games (names, prefixes) are the workspace's markers.tsv (regress_conf.py):
one line per env prefix of games.tsv, and "*" lines for games no repository here ports
(those only the SDK must not name).
For each game of games.tsv, the tracked files of its repository (outside SR-SDK/, and
without *.md) are searched, names and text, for the markers of the other games; the games
of one env prefix (one repository's several programs) share their markers. The SDK
names no game at all: every tracked file of it, *.md included, is searched for the markers
of every line. Exit status 1 when something is found.
"""
import os, re, subprocess, sys

from regress_conf import SDK, ROOT, rows

MARKERS = [(f[0], f[1]) for f in rows('markers.tsv')]
SKIP_DIRS = ('SR-SDK/', 'research/', 'build/')

def games():
    return [(f[0], os.path.join(ROOT, f[1]), f[3]) for f in rows('games.tsv')]

def tracked(repo, prefix=''):
    r = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True)
    return [p.decode('utf-8', 'replace') for p in r.stdout.split(b'\0') if p and p.decode('utf-8', 'replace').startswith(prefix)]

def scan(repo, files, rx, label, found, skip_md=True):
    for p in files:
        if (skip_md and p.endswith('.md')) or any(p.startswith(d) for d in SKIP_DIRS):
            continue
        if os.path.isdir(os.path.join(repo, p)):
            continue                        # a submodule
        m = rx.search(p)
        if m:
            found.append('%s: %s: name has "%s"' % (label, p, m.group(0)))
        path = os.path.join(repo, p)
        try:
            data = open(path, 'rb').read()
        except OSError:
            continue
        if b'\0' in data[:4096]:
            continue
        text = data.decode('utf-8', 'replace')
        for n, line in enumerate(text.split('\n'), 1):
            m = rx.search(line)
            if m:
                found.append('%s: %s:%d: "%s"' % (label, p, n, m.group(0)))
                break   # one line per file is enough to find it

def main():
    ids = sys.argv[1:]
    found = []
    for gid, repo, pfx in games():
        if (ids and gid not in ids) or ids == ['sdk']:
            continue
        if not os.path.isdir(repo):
            continue
        foreign = [v for k, v in MARKERS if k not in (pfx, '*')]
        scan(repo, tracked(repo), re.compile('|'.join(foreign)), gid, found)
    if not ids or 'sdk' in ids:
        scan(SDK, tracked(SDK), re.compile('|'.join(v for _, v in MARKERS)), 'sdk', found, skip_md=False)
    for l in found:
        print(l)
    print('check_prefixes: %d finding(s)' % len(found))
    return 1 if found else 0

sys.exit(main())
