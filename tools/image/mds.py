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

"""Alcohol 120% images (.mds + .mdf).

An .mdf whose tracks are stored as raw 2352-byte sectors without subchannel
data is a BINARY image as a cue sheet describes it, so the cue sheet only
points at it: nothing is copied or converted.

The .mds gives, per track, its mode, its pregap and the byte offset of its
INDEX 01 in the .mdf. A pregap that is not in the file becomes PREGAP; the
INDEX 01 times are positions in the file, as the cue format wants them.
"""
import os
import struct

from cdimage import RAW, fail, link_next_to, write_cue


def detect_mds(path):
    with open(path, 'rb') as stream:
        return stream.read(16) == b'MEDIA DESCRIPTOR'


def read_tracks(path):
    with open(path, 'rb') as stream:
        data = stream.read()
    if data[:16] != b'MEDIA DESCRIPTOR':
        fail('%s: not an MDS file' % path)
    sessions = struct.unpack_from('<H', data, 0x14)[0]
    session_offset = struct.unpack_from('<I', data, 0x50)[0]
    tracks = []
    for session in range(sessions):
        (_, _, _, blocks, _, _, _, _, track_offset) = struct.unpack_from(
            '<iiHBBHHII', data, session_offset + 0x18 * session)
        for block in range(blocks):
            at = track_offset + 0x50 * block
            mode, subchannel, _, _, point = struct.unpack_from('<BBBBB', data, at)
            if point < 1 or point > 99:
                continue                    # lead-in entries (A0h-A2h)
            extra_offset = struct.unpack_from('<I', data, at + 0x0C)[0]
            sector_size = struct.unpack_from('<H', data, at + 0x10)[0]
            file_offset = struct.unpack_from('<Q', data, at + 0x28)[0]
            pregap = struct.unpack_from('<II', data, extra_offset)[0] if extra_offset else 0
            if sector_size != RAW or subchannel != 0:
                fail('%s: track %d: %d-byte sectors, subchannel %d: not a BINARY image'
                     % (path, point, sector_size, subchannel))
            if mode == 0xA9:
                kind = 'AUDIO'
            elif mode == 0xAA:
                kind = 'MODE1/2352'
            elif mode in (0xAB, 0xAC, 0xEC):
                kind = 'MODE2/2352'
            else:
                fail('%s: track %d: unknown mode %02Xh' % (path, point, mode))
            tracks.append((point, kind, pregap, file_offset // RAW))
    return sorted(tracks)


def mds_to_cue(mds, out_dir, name, log=print, mdf=None):
    """Write out_dir/name.cue for mds + mdf. Returns the cue's path."""
    mdf = mdf or os.path.splitext(mds)[0] + '.mdf'
    if not os.path.isfile(mdf):
        fail('%s: not found (the .mds image data)' % mdf)
    tracks = read_tracks(mds)
    image_name = link_next_to(mdf, out_dir)
    rows = []
    for number, kind, pregap, start in tracks:
        # the first track's 2 s lead-in is never in an image
        rows.append({'file': image_name, 'number': number, 'mode': kind,
                     'pregap': pregap if number != tracks[0][0] else 0, 'index1': start})
    cue = os.path.join(out_dir, name + '.cue')
    write_cue(cue, rows)
    log('%s: %d tracks, %d audio, file %s'
        % (cue, len(tracks), sum(1 for t in tracks if t[1] == 'AUDIO'), image_name))
    return cue
