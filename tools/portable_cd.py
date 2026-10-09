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

# A portable copy of a game's disc, run from the game repository's root:
#
#     portable_cd.py OUT_DIR [--cfg game/portable.cfg] [--cue CUE]
#                    [--audio flac|wav|ogg] [--quality Q]
#
# The port runs the game from a copy of the disc, without installing it: a
# game directory with the disc's files is taken as the disc. This makes such a
# directory, OUT_DIR, which must not exist yet:
#
# 1. the files of the disc's data track (ISO9660), as they are, except those
#    game/portable.cfg names (the installer) and except a directory called
#    like audio_dir: on a disc it holds entries whose extents are the audio
#    tracks themselves, not files;
# 2. extra_files, the files the installer would have written, as they are;
# 3. audio_dir/: the audio tracks, one file each, and a cue sheet listing
#    them. flac decodes to the disc's samples bit for bit; wav is the same
#    uncompressed; ogg is lossy, only smaller.
#
# game/portable.cfg (paths relative to the game repository's root):
#     cue=<the disc's cue sheet>       one BINARY file (required)
#     skip_files=<patterns>            files not to copy, at any depth
#     skip_dirs=<patterns>             directories not to copy, at any depth
#     extra_files=<paths>              copied into OUT_DIR as they are
#     audio_dir=AUDIO                  where the tracks go
#     audio_cue=<name>.cue             the tracks' cue sheet (required)
#     audio=ogg                        flac, wav or ogg (default ogg)
#     quality=6                        ogg only: -1..10
# Patterns are fnmatch patterns, separated by spaces, matched against the
# name in upper case.
#
# Needs ffmpeg. Nothing is written outside OUT_DIR.

import argparse
import fnmatch
import os
import shutil
import struct
import subprocess
import sys
from typing import NoReturn

RAW = 2352
DATA = 2048


def fail(message) -> NoReturn:
    sys.exit('portable_cd: ' + message)


def read_cfg(path):
    # as scons/game.py reads it
    cfg = {}
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line:
                key, value = line.split('=', 1)
                cfg[key.strip()] = value.strip()
    return cfg


def parse_msf(text):
    m, s, f = (int(x) for x in text.split(':'))
    return (m * 60 + s) * 75 + f


def parse_cue(path):
    """Tracks of a single-BINARY-file cue: number, mode, index0, index1."""
    base = os.path.dirname(os.path.abspath(path))
    files, tracks = [], []
    for line in open(path, encoding='latin1'):
        words = line.strip().split()
        if not words:
            continue
        key = words[0].upper()
        if key == 'FILE':
            name = line.split('"')[1] if '"' in line else words[1]
            files.append((os.path.join(base, os.path.basename(name.replace('\\', '/'))), words[-1].upper()))
        elif key == 'TRACK':
            tracks.append({'number': int(words[1]), 'mode': words[2].upper(), 'file': len(files) - 1,
                           'index0': None, 'index1': None})
        elif key == 'INDEX' and tracks:
            tracks[-1]['index%d' % int(words[1])] = parse_msf(words[2])
    if len(files) != 1 or files[0][1] != 'BINARY':
        fail('expected one BINARY file in %s' % path)
    return files[0][0], tracks


def sector_layout(mode):
    if mode.endswith('/2048'):
        return DATA, 0
    if mode.endswith('/2336'):
        return 2336, 8
    return RAW, 24 if mode.startswith('MODE2') else 16


class Image:
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


def records(image, lba, size):
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


def matches(name, patterns):
    upper = name.upper()
    return any(fnmatch.fnmatchcase(upper, pattern.upper()) for pattern in patterns)


class Copy:
    def __init__(self, image, skip_files, skip_dirs, audio_dir):
        self.image = image
        self.skip_files, self.skip_dirs = skip_files, skip_dirs
        self.audio_dir = audio_dir.upper()
        self.count = self.total = self.skipped = 0

    def tree(self, lba, size, out, top=True):
        for name, extent, length, directory in records(self.image, lba, size):
            if directory:
                if top and name.upper() == self.audio_dir:
                    continue
                if matches(name, self.skip_dirs):
                    self.skipped += 1
                    continue
                os.mkdir(os.path.join(out, name))
                self.tree(extent, length, os.path.join(out, name), False)
            elif matches(name, self.skip_files):
                self.skipped += 1
            else:
                with open(os.path.join(out, name), 'wb') as target:
                    target.write(self.image.read(extent, length))
                self.count += 1
                self.total += length


def main():
    parser = argparse.ArgumentParser(description="Makes a portable copy of a game's disc.")
    parser.add_argument('out', help='the new game directory; it must not exist yet')
    parser.add_argument('--cfg', default=os.path.join('game', 'portable.cfg'))
    parser.add_argument('--cue', help="the disc's cue sheet (default: cue= of the cfg)")
    parser.add_argument('--audio', choices=('flac', 'wav', 'ogg'))
    parser.add_argument('--quality', help='ogg only: quality -1..10')
    args = parser.parse_args()

    root = os.getcwd()
    cfg = read_cfg(args.cfg)
    cue_path = args.cue or (os.path.join(root, cfg['cue']) if cfg.get('cue') else fail(args.cfg + ' has no cue'))
    audio_cue = cfg.get('audio_cue') or fail(args.cfg + ' has no audio_cue')
    audio_dir = cfg.get('audio_dir', 'AUDIO')
    audio_format = args.audio or cfg.get('audio', 'ogg')
    if audio_format not in ('flac', 'wav', 'ogg'):
        fail('audio=%s: flac, wav or ogg' % audio_format)
    quality = args.quality or cfg.get('quality', '6')
    extra = [os.path.join(root, path) for path in cfg.get('extra_files', '').split()]
    for path in extra:
        if not os.path.isfile(path):
            fail('no such file: %s' % path)

    image_path, tracks = parse_cue(cue_path)
    data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
    if data is None:
        fail('no data track in %s' % cue_path)
    if os.path.exists(args.out):
        fail('%s exists; give a new directory' % args.out)
    os.makedirs(args.out)

    # 1. the data track, without the installer
    image = Image(image_path, data)
    pvd = image.sector(16)
    if pvd[1:6] != b'CD001':
        fail('no ISO9660 volume in the data track')
    root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
    copy = Copy(image, cfg.get('skip_files', '').split(), cfg.get('skip_dirs', '').split(), audio_dir)
    copy.tree(root_lba, root_size, args.out)
    print('data track: %d files, %.1f MB (%d installer entries skipped)'
          % (copy.count, copy.total / 1e6, copy.skipped))

    # 2. what the installer would have written
    for path in extra:
        shutil.copy(path, args.out)
        print('extra: %s' % os.path.basename(path))

    # 3. the audio tracks and their cue sheet
    music = os.path.join(args.out, audio_dir)
    os.mkdir(music)
    image_size = os.path.getsize(image_path)
    audio = [t for t in tracks if t['mode'] == 'AUDIO']
    cue_lines = []
    for position, track in enumerate(audio):
        start = track['index1'] * RAW
        later = audio[position + 1] if position + 1 < len(audio) else None
        end = ((later['index0'] if later['index0'] is not None else later['index1']) * RAW
               if later is not None else image_size)
        name = 'track%02d.%s' % (track['number'], audio_format)
        target = os.path.join(music, name)
        with open(image_path, 'rb') as source:
            source.seek(start)
            pcm = source.read(end - start)
        codec = {'ogg': ['-c:a', 'libvorbis', '-q:a', quality],
                 'flac': ['-c:a', 'flac', '-compression_level', '8'], 'wav': ['-c:a', 'pcm_s16le']}[audio_format]
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '44100', '-ac', '2', '-i', '-']
                       + codec + [target], input=pcm, check=True)
        cue_lines += ['FILE "%s" %s' % (name, 'WAVE' if audio_format == 'wav' else audio_format.upper()),
                      '  TRACK %02d AUDIO' % track['number'], '    INDEX 01 00:00:00']
        print('track %2d: %6.1f s -> %s/%s' % (track['number'], (end - start) / RAW / 75, audio_dir, name))
    with open(os.path.join(music, audio_cue), 'w') as cue:
        cue.write('\n'.join(cue_lines) + '\n')
    print('%s/%s: %d tracks' % (audio_dir, audio_cue, len(audio)))


if __name__ == '__main__':
    main()
