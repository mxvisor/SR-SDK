#! /usr/bin/env python3
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

"""CloneCD (.ccd) images.

A CloneCD image is a .img of 2352-byte sectors, its track layout in a .ccd (an
INI file), and the subchannel data in a .sub. The [TRACK n] sections give the
mode (0 AUDIO, 1 MODE1/2352, 2 MODE2/2352) and the LBAs of the indexes; unlike
a cue sheet, INDEX 0 is written only when the pregap is in the .img. The cue
written here points at the .img beside the .ccd, which is used as it is.

One session is handled, and DataTracksScrambled must be 0.
"""
import os
import re

from cdimage import RAW, fail, link_next_to, write_cue

TRACK = re.compile(r'^track\s+(\d+)$', re.I)
MODES = {0: 'AUDIO', 1: 'MODE1/2352', 2: 'MODE2/2352'}


def read_sections(path):
    """{[section]: {key: value}} of a CloneCD INI file; keys lowercased."""
    sections, current = {}, None
    with open(path, encoding='latin1') as stream:
        for line in stream:
            line = line.strip()
            if not line:
                continue
            if line.startswith('[') and line.endswith(']'):
                current = sections.setdefault(line[1:-1].strip(), {})
            elif current is not None and '=' in line:
                key, value = line.split('=', 1)
                current[key.strip().lower()] = value.strip()
    return sections


def to_cue(path, out_dir, name, log=print):
    """Write out_dir/name.cue for path (a .ccd) and the .img beside it."""
    sections = read_sections(path)
    disc = sections.get('disc', {})
    if disc.get('sessions', '1') != '1':
        fail('%s: %s sessions are not handled' % (path, disc.get('sessions')))
    if disc.get('datatracksscrambled', '0') not in ('', '0'):
        fail('%s: scrambled data tracks are not handled' % path)
    image = os.path.splitext(path)[0] + '.img'
    if not os.path.isfile(image):
        fail('%s: no .img beside it' % path)
    size = os.path.getsize(image)
    tracks = []
    for title, section in sections.items():
        found = TRACK.match(title)
        if not found:
            continue
        number = int(found.group(1))
        mode = int(section.get('mode', '-1'))
        if mode not in MODES:
            fail('%s: track %d has mode %s' % (path, number, section.get('mode')))
        index1 = section.get('index 1')
        if index1 is None:
            fail('%s: track %d has no INDEX 1' % (path, number))
        track = {'file': '', 'number': number, 'mode': MODES[mode],
                 'index0': None, 'index1': int(index1)}
        if 'index 0' in section:
            track['index0'] = int(section['index 0'])
        tracks.append(track)
    tracks.sort(key=lambda track: track['number'])
    if not tracks:
        fail('%s: no [TRACK n] sections' % path)
    for track in tracks:
        if (track['index1'] + 1) * RAW > size:
            fail('%s: track %d starts after the end of %s' % (path, track['number'], image))
    file_name = link_next_to(image, out_dir)
    for track in tracks:
        track['file'] = file_name
    cue = os.path.join(out_dir, name + '.cue')
    write_cue(cue, tracks)
    log('%s: %d tracks, data %s, %s' % (cue, len(tracks), tracks[0]['mode'], file_name))
    return cue
