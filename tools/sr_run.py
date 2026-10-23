#!/usr/bin/env python3
# Runs SR on a DOS program the way scons/game.py needs it: the routines the
# program hands to the system (INT 21h AH=25h) are found by SR itself
# (--list_handlers) and a second run takes them as code, as if the game's
# fixup_interpret_as_code.sci listed them.  The game's file stays as it is;
# the merged one is written into SR's working directory.
#
#   sr_run.py SR.EXE INPUT OUTPUT.asm [GAME_FIXUP.sci]
#
# SR's output goes to sr.out / sr.err of the working directory, the lists of
# each run are kept as handlers.txt.
import os
import subprocess
import sys

MAX_RUNS = 4        # a handler can lead to code that installs another one


def labels(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return [line.strip() for line in f if line.strip().startswith('loc_')]


def main():
    sr_exe, input_exe, output = sys.argv[1:4]
    game_sci = sys.argv[4] if len(sys.argv) > 4 else None
    merged = 'fixup_interpret_as_code.sci'
    base = []
    if game_sci and os.path.exists(game_sci):
        with open(game_sci) as f:
            base = f.read().splitlines()
    known = {line.split(';')[0].strip() for line in base}
    found = []

    # a first run of a game without any handler of this kind is the only one
    for run in range(MAX_RUNS):
        with open(merged, 'w') as f:
            f.write('\n'.join(base) + ('\n' if base else ''))
            if found:
                f.write('; found by SR (--list_handlers): handed to INT 21h AH=25h\n')
                f.write('\n'.join(found) + '\n')
        with open('sr.out', 'w') as out, open('sr.err', 'w') as err:
            rc = subprocess.call([sr_exe, '--list_handlers=handlers.txt', input_exe, output], stdout=out, stderr=err)
        if rc != 0:
            return rc
        new = [label for label in labels('handlers.txt') if label not in known and label not in found]
        if not new:
            break
        found += new
    if not found and not base and os.path.exists(merged):
        os.remove(merged)       # nothing to tell SR: the same run as without this tool
    return 0


sys.exit(main())
