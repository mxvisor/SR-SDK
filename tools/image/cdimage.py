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

"""What the disc-image formats have in common: sector layouts, cue sheets and
the ISO9660 directory tree of a data track."""
import os
import struct
import sys

RAW = 2352
DATA = 2048


def fail(message):
    sys.exit('image: ' + message)


def msf(lba):
    minutes, rest = divmod(lba, 75 * 60)
    seconds, frame = divmod(rest, 75)
    return '%02d:%02d:%02d' % (minutes, seconds, frame)


def parse_msf(text):
    m, s, f = (int(x) for x in text.split(':'))
    return (m * 60 + s) * 75 + f


def write_cue(path, tracks):
    """tracks: [{'file': name, 'number', 'mode', 'pregap', 'index0', 'index1'}],
    in file order; a FILE line is written when the file changes."""
    lines, current = [], None
    for track in tracks:
        if track['file'] != current:
            current = track['file']
            lines.append('FILE "%s" BINARY' % current)
        lines.append('  TRACK %02d %s' % (track['number'], track['mode']))
        if track.get('pregap'):
            lines.append('    PREGAP %s' % msf(track['pregap']))
        if track.get('index0') is not None:
            lines.append('    INDEX 00 %s' % msf(track['index0']))
        lines.append('    INDEX 01 %s' % msf(track['index1']))
    with open(path, 'w', newline='\r\n') as stream:
        stream.write('\n'.join(lines) + '\n')


def file_beside(base, name):
    """base/name, or the file of base whose name differs from it only in case: a
    cue sheet written on DOS or Windows names GAME.BIN for Game.bin."""
    path = os.path.join(base, name)
    if not os.path.exists(path) and os.path.isdir(base):
        match = [entry for entry in os.listdir(base) if entry.lower() == name.lower()]
        if len(match) == 1:
            return os.path.join(base, match[0])
    return path


def parse_cue(path):
    """Tracks of a cue sheet of BINARY files: number, mode, path (their file),
    index0, index1 (in sectors from the start of that file). One file for the
    whole disc or one per track, as redump has them. Returns (the first file,
    tracks)."""
    base = os.path.dirname(os.path.abspath(path))
    files, tracks = [], []
    for line in open(path, encoding='latin1'):
        words = line.strip().split()
        if not words:
            continue
        key = words[0].upper()
        if key == 'FILE':
            name = line.split('"')[1] if '"' in line else words[1]
            files.append((file_beside(base, os.path.basename(name.replace('\\', '/'))), words[-1].upper()))
        elif key == 'TRACK':
            if not files:
                fail('TRACK before FILE in %s' % path)
            tracks.append({'number': int(words[1]), 'mode': words[2].upper(), 'file': len(files) - 1,
                           'path': files[-1][0], 'index0': None, 'index1': None})
        elif key == 'INDEX' and tracks:
            tracks[-1]['index%d' % int(words[1])] = parse_msf(words[2])
    if not files or any(kind != 'BINARY' for _, kind in files):
        fail('expected BINARY files in %s' % path)
    return files[0][0], tracks


def link_next_to(path, out_dir):
    """The image's name in out_dir: the file itself when it is there already,
    else a symbolic link, so that the cue sheet finds it by its base name."""
    if os.path.realpath(os.path.dirname(os.path.abspath(path))) == os.path.realpath(out_dir):
        return os.path.basename(path)
    link = os.path.join(out_dir, os.path.basename(path))
    if not os.path.lexists(link):
        os.symlink(os.path.abspath(path), link)
    return os.path.basename(path)


def sector_layout(mode):
    """(bytes per sector, offset of the 2048 bytes of user data)."""
    if mode.endswith('/2048'):
        return DATA, 0
    if mode.endswith('/2336'):
        return 2336, 8
    return RAW, 24 if mode.startswith('MODE2') else 16


def track_span(tracks, position):
    """(file, start, end): the byte range of the track at `position` in its
    file. It ends where the next track of the same file begins (its pregap
    included), else at the end of the file."""
    track = tracks[position]
    start = track['index1'] * RAW
    later = tracks[position + 1] if position + 1 < len(tracks) else None
    if later is None or later['path'] != track['path']:
        return track['path'], start, os.path.getsize(track['path'])
    first = later['index0'] if later['index0'] is not None else later['index1']
    return track['path'], start, first * RAW


class Image:
    """The data track of a cue + bin pair, read sector by sector."""

    def __init__(self, path, track):
        self.file = open(path, 'rb')
        self.size, self.header = sector_layout(track['mode'])
        self.base = track['index1'] * self.size

    def sector(self, lba):
        self.file.seek(self.base + lba * self.size + self.header)
        return self.file.read(DATA)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])

    def root(self):
        """(lba, size) of the ISO9660 root directory."""
        pvd = self.sector(16)
        if pvd[1:6] != b'CD001':
            fail('no ISO9660 volume in the data track')
        return struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]


def open_data_track(cue_path):
    image_path, tracks = parse_cue(cue_path)
    data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
    if data is None:
        fail('no data track in %s' % cue_path)
    return Image(data['path'], data), image_path, tracks


def records(image, lba, size):
    """(name, extent, length, directory) of the entries of one directory."""
    for n in range((size + DATA - 1) // DATA):
        block = image.sector(lba + n)
        at = 0
        while at + 34 <= DATA and block[at] != 0:
            length = block[at]
            record = block[at:at + length]
            name = record[33:33 + record[32]]
            at += length
            if name in (b'\0', b'\1'):
                continue
            yield (name.split(b';')[0].rstrip(b'.').decode('latin1'),
                   struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0],
                   bool(record[25] & 2))


def enumerate_tree(image, lba, size):
    """Every entry of the data track: {disc-relative path: (extent, length,
    directory)}."""
    result = {}

    def walk(lba, size, prefix):
        for name, extent, length, directory in records(image, lba, size):
            rel = prefix + name
            result[rel] = (extent, length, directory)
            if directory:
                walk(extent, length, rel + '/')

    walk(lba, size, '')
    return result


def extract_tree(image, out_dir):
    """Write the files of the data track under out_dir, which gets the disc's
    names (ISO9660 version suffix dropped). Returns (files, bytes)."""
    count = total = 0
    for rel, (extent, length, directory) in sorted(enumerate_tree(image, *image.root()).items()):
        target = os.path.join(out_dir, rel)
        if directory:
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as handle:
            left, lba = length, extent
            while left > 0:
                chunk = image.read(lba, min(left, 1 << 20))
                handle.write(chunk)
                left -= len(chunk)
                lba += (1 << 20) // DATA
        count += 1
        total += length
    return count, total


def write_data_iso(image, path, sectors):
    """The data track alone as a 2048-byte-per-sector ISO."""
    with open(path, 'wb') as target:
        for lba in range(sectors):
            target.write(image.sector(lba))
