#!/usr/bin/env python3
"""check_layout.py [GAME_ID...]: a game's repository holds only what builds and runs it.

The notes and the research of a port live one level up, in <GAME>/ (<GAME>/research/,
<GAME>/*.md), not in its <game>-recomp/ repository. For each game of games.tsv (or the
ones named), the repository must not have, tracked or only on the disk:
  - research/;
  - tools/ (the input of tools/gen_import_shim.py is game/import_args.tsv);
  - *.md at its root.
SR-SDK/ and build/ are not looked at. Exit status 1 when something is found.
"""
import glob, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = os.environ.get('DOS_RECOMP_ROOT') or os.path.dirname(SDK)
FORBIDDEN_DIRS = ('research/', 'tools/')

def games():
    out = []
    for line in open(os.path.join(HERE, 'games.tsv')):
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        out.append((f[0], os.path.join(ROOT, f[1])))
    return out

def tracked(repo):
    r = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True)
    return [p.decode('utf-8', 'replace') for p in r.stdout.split(b'\0') if p]

def check(gid, repo, found):
    for p in tracked(repo):
        if p.startswith(FORBIDDEN_DIRS) or ('/' not in p and p.endswith('.md')):
            found.append('%s: %s is tracked' % (gid, p))
    for d in FORBIDDEN_DIRS:
        if os.path.isdir(os.path.join(repo, d)):
            found.append('%s: %s is on the disk' % (gid, d))
    for p in sorted(glob.glob(os.path.join(repo, '*.md'))):
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
