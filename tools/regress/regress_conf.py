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
