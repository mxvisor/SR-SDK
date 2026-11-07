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

"""check_layout.py [GAME_ID...]: a game's repository holds only what builds and runs it.

The notes and the research of a port live one level up, in <GAME>/ (<GAME>/research/,
<GAME>/*.md), not in its <game>-recomp/ repository. For each game of games.tsv (or the
ones named), the repository must not have, tracked or only on the disk:
  - research/;
  - tools/ (the input of tools/gen_import_shim.py is game/import_args.tsv);
  - *.md at its root, except README.md (the repository's page on GitHub).
SR-SDK/ and build/ are not looked at. Exit status 1 when something is found.
"""
import glob, os, subprocess, sys

from regress_conf import ROOT, rows
FORBIDDEN_DIRS = ('research/', 'tools/')
ALLOWED_MD = ('README.md',)

def games():
    return [(f[0], os.path.join(ROOT, f[1])) for f in rows('games.tsv')]

def tracked(repo):
    r = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True)
    return [p.decode('utf-8', 'replace') for p in r.stdout.split(b'\0') if p]

def check(gid, repo, found):
    for p in tracked(repo):
        if p.startswith(FORBIDDEN_DIRS) or ('/' not in p and p.endswith('.md') and p not in ALLOWED_MD):
            found.append('%s: %s is tracked' % (gid, p))
    for d in FORBIDDEN_DIRS:
        if os.path.isdir(os.path.join(repo, d)):
            found.append('%s: %s is on the disk' % (gid, d))
    for p in sorted(glob.glob(os.path.join(repo, '*.md'))):
        if os.path.basename(p) in ALLOWED_MD:
            continue
        found.append('%s: %s is on the disk' % (gid, os.path.basename(p)))

def main():
    ids = sys.argv[1:]
    found = []
    for gid, repo in games():
        if (ids and gid not in ids) or not os.path.isdir(repo):
            continue
        check(gid, repo, found)
    for l in found:
        print(l)
    print('check_layout: %d finding(s)' % len(found))
    return 1 if found else 0

sys.exit(main())
