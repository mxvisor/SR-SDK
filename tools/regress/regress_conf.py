"""The workspace's regression data, for the tools of tools/regress.

ROOT is the workspace root ($DOS_RECOMP_ROOT, or the parent of SR-SDK); CONF the directory of
the data ($REGRESS_CONF, or ROOT/regress): games.tsv, images.tsv, markers.tsv, sr-inputs.sh
and ref/. The SDK names no game itself: which games there are is the workspace's."""
import os

SDK = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
ROOT = os.environ.get('DOS_RECOMP_ROOT') or os.path.dirname(SDK)
CONF = os.environ.get('REGRESS_CONF') or os.path.join(ROOT, 'regress')


def rows(name):
    """The rows of CONF/<name>, a TAB-separated table with # comments, as lists of fields."""
    out = []
    for line in open(os.path.join(CONF, name)):
        if line.startswith('#') or not line.strip():
            continue
        out.append(line.rstrip('\n').split('\t'))
    return out
