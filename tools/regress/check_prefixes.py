#!/usr/bin/env python3
"""check_prefixes.py [GAME_ID|sdk...]: no file of a game carries another game's name.

For each game of games.tsv, the tracked files of its repository (outside SR-SDK/, and
without *.md) are searched, names and text, for the markers of the other games (markers.tsv
beside games.tsv: one line per env prefix; the games of one family share it). The SDK's
own runtime/, scons/ and android/ are searched for any of them.
Exit status 1 when something is found.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = os.environ.get('DOS_RECOMP_ROOT') or os.path.dirname(SDK)

FAMILY = dict(l.rstrip('\n').split('\t', 1) for l in open(os.path.join(HERE, 'markers.tsv'))
              if l.strip() and not l.startswith('#'))
SKIP_DIRS = ('SR-SDK/', 'research/', 'build/')

def games():
    out = []
    for line in open(os.path.join(HERE, 'games.tsv')):
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        out.append((f[0], os.path.join(ROOT, f[1]), f[3]))
    return out

def tracked(repo, prefix=''):
    r = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True)
    return [p.decode('utf-8', 'replace') for p in r.stdout.split(b'\0') if p and p.decode('utf-8', 'replace').startswith(prefix)]

def scan(repo, files, rx, label, found, name_rx=None):
    for p in files:
        if p.endswith('.md') or any(p.startswith(d) for d in SKIP_DIRS):
            continue
        m = (name_rx or rx).search(p)
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
        foreign = [v for k, v in FAMILY.items() if k != pfx]
        scan(repo, tracked(repo), re.compile('|'.join(foreign)), gid, found)
    if not ids or 'sdk' in ids:
        sdk_files = [p for p in tracked(SDK) if p.startswith(('runtime/', 'scons/', 'android/'))]
        allrx = '|'.join(FAMILY.values())
        scan(SDK, sdk_files, re.compile(allrx), 'sdk', found, re.compile(allrx))
    for l in found:
        print(l)
    print('check_prefixes: %d finding(s)' % len(found))
    return 1 if found else 0

sys.exit(main())
