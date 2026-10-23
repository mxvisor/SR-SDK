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
# 3. place=<glob> <dest>..., the files the installer would have written to
#    subdirectories, relocated from the disc tree to OUT_DIR: each <glob> is
#    a disc-path glob (case-insensitive fnmatch against the ISO9660 name) and
#    <dest> is either a directory (ending in "/") or a file name; placed files
#    are copied from the disc and not also written in their disc location;
# 4. audio_dir/: the audio tracks, one file each, and a cue sheet listing
#    them. flac decodes to the disc's samples bit for bit; wav is the same
#    uncompressed; ogg is lossy, only smaller.
#
# game/portable.cfg (paths relative to the game repository's root):
#     cue=<the disc's cue sheet>       BINARY file(s): one for the disc or one per
#                                      track, as redump has them (required)
#     patch=<archive>[ <dir>]          archives over the disc, in order: the
#                                      copy is taken from disc_tree.py's
#                                      build/disc, not the bare image
#     unpack=<disc file>[ <dir>]       an InstallShield .z of the disc, unpacked
#                                      into the copy (so is build/disc)
#     image=<the disc image>           .nrg, .mds, .bin or .iso: when the cue
#                                      sheet is not there yet it is made from
#                                      this, with image/image.py
#     skip_files=<patterns>            files not to copy, at any depth
#     skip_dirs=<patterns>             directories not to copy, at any depth
#     extra_files=<paths>              copied into OUT_DIR as they are
#     place=<glob1> <dest1> ...        disc-glob + destination, repeatable
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image'))
from cdimage import (RAW, Image, enumerate_tree, parse_cue, track_span,  # noqa: E402
                     records)


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


def make_cue(image, cue):
    """The cue sheet of a disc image, next to where the cfg wants it."""
    if not os.path.isfile(image):
        fail('no such disc image: %s' % image)
    out_dir = os.path.dirname(os.path.abspath(cue))
    name = os.path.splitext(os.path.basename(cue))[0]
    command = [sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image', 'image.py'),
               'disc', image, '-o', out_dir, '--name', name]
    print('disc image: %s -> %s' % (image, cue))
    if subprocess.run(command).returncode or not os.path.isfile(cue):
        fail('cannot make %s from %s' % (cue, image))


def read_place_rules(path):
    """Every `place=<glob> <dest>` line: the disc files matching <glob> are
    written to OUT_DIR/<dest> instead of their disc location. One rule per
    line, several lines allowed."""
    rules = []
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if not line or not line.lower().startswith('place='):
                continue
            _, value = line.split('=', 1)
            value = value.strip()
            if not value:
                continue
            words = value.split()
            if len(words) < 2:
                fail('place= needs <glob> <dest>: %s' % value)
            rules.append((words[0], ' '.join(words[1:])))
    return rules


def matches(name, patterns):
    upper = name.upper()
    return any(fnmatch.fnmatchcase(upper, pattern.upper()) for pattern in patterns)


class Copy:
    def __init__(self, image, skip_files, skip_dirs, audio_dir, place_rules=None, root=''):
        self.image = image
        self.root = root
        self.skip_files, self.skip_dirs = skip_files, skip_dirs
        self.audio_dir = audio_dir.upper()
        self.place_rules = place_rules or []
        self.placed = set()
        self.count = self.total = self.skipped = 0

    def place_dest(self, rel):
        for glob, dest in self.place_rules:
            if fnmatch.fnmatchcase(rel.upper(), glob.upper()):
                return dest
        return None

    def tree(self, lba, size, out, prefix='', top=True):
        for name, extent, length, directory in records(self.image, lba, size):
            if directory:
                if top and name.upper() == self.audio_dir:
                    continue
                if matches(name, self.skip_dirs):
                    self.skipped += 1
                    continue
                os.mkdir(os.path.join(out, name))
                self.tree(extent, length, os.path.join(out, name), prefix + name + '/', False)
            elif matches(name, self.skip_files):
                self.skipped += 1
            else:
                rel = prefix + name
                dest = self.place_dest(rel)
                if dest is not None:
                    self.placed.add(rel)
                    target = os.path.join(self.root, dest)
                    if dest.endswith('/'):
                        target = os.path.join(self.root, dest, name)
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    with open(target, 'wb') as handle:
                        handle.write(self.image.read(extent, length))
                    self.count += 1
                    self.total += length
                else:
                    with open(os.path.join(out, name), 'wb') as target:
                        target.write(self.image.read(extent, length))
                    self.count += 1
                    self.total += length

    def fs_tree(self, src, out, prefix='', top=True):
        """tree() over a directory (the disc with its patches, disc_tree.py)
        instead of the image."""
        for name in sorted(os.listdir(src)):
            path = os.path.join(src, name)
            if os.path.isdir(path):
                if top and name.upper() == self.audio_dir:
                    continue
                if matches(name, self.skip_dirs):
                    # what the installer put elsewhere is still wanted: place=
                    self.skipped += 1
                    self.fs_tree(path, None, prefix + name + '/', False)
                    continue
                if out:
                    os.mkdir(os.path.join(out, name))
                self.fs_tree(path, os.path.join(out, name) if out else None, prefix + name + '/', False)
            elif name == '.disc_tree' or (matches(name, self.skip_files) and self.place_dest(prefix + name) is None):
                self.skipped += 1
            else:
                rel = prefix + name
                dest = self.place_dest(rel)
                target = os.path.join(out, name) if out else None
                if target is None and dest is None:
                    continue
                if dest is not None:
                    self.placed.add(rel)
                    target = os.path.join(self.root, dest, name) if dest.endswith('/') else os.path.join(self.root, dest)
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copyfile(path, target)
                self.count += 1
                self.total += os.path.getsize(path)

    def write_placed(self, tree):
        """Files whose disc location was not visited (their directory was
        skipped) still need to be written to their place= destination."""
        for rel, (extent, length, directory) in tree.items():
            if directory or rel in self.placed:
                continue
            dest = self.place_dest(rel)
            if dest is None:
                continue
            self.placed.add(rel)
            target = os.path.join(self.root, dest)
            if dest.endswith('/'):
                target = os.path.join(self.root, dest, os.path.basename(rel))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, 'wb') as handle:
                handle.write(self.image.read(extent, length))
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

    if not os.path.isfile(cue_path):
        if not cfg.get('image'):
            fail('%s not found (and %s has no image= to make it from)' % (cue_path, args.cfg))
        make_cue(os.path.join(root, cfg['image']), cue_path)
    image_path, tracks = parse_cue(cue_path)
    data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
    if data is None:
        fail('no data track in %s' % cue_path)
    if os.path.exists(args.out):
        fail('%s exists; give a new directory' % args.out)
    os.makedirs(args.out)

    # 1. the data track, without the installer
    image = Image(data['path'], data)
    pvd = image.sector(16)
    if pvd[1:6] != b'CD001':
        fail('no ISO9660 volume in the data track')
    root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
    place_rules = read_place_rules(args.cfg)
    copy = Copy(image, cfg.get('skip_files', '').split(), cfg.get('skip_dirs', '').split(), audio_dir, place_rules, args.out)
    if any(line.split('#', 1)[0].strip().lower().startswith(('patch=', 'unpack=')) for line in open(args.cfg)):
        # the disc with its patch= archives over it and its unpack= archives
        # unpacked, not the bare image
        disc = os.path.join(root, 'build', 'disc')
        subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'disc_tree.py'),
                        disc, '--cfg', args.cfg, '--cue', cue_path], check=True)
        copy.fs_tree(disc, args.out)
    else:
        tree = enumerate_tree(image, root_lba, root_size)
        copy.tree(root_lba, root_size, args.out)
        copy.write_placed(tree)
    # the directories whose files all went elsewhere (place=) are not part of the install
    for rel in copy.placed:
        directory = os.path.dirname(rel)
        while directory and os.path.isdir(os.path.join(args.out, directory)) and not os.listdir(os.path.join(args.out, directory)):
            os.rmdir(os.path.join(args.out, directory))
            directory = os.path.dirname(directory)
    print('data track: %d files, %.1f MB (%d installer entries skipped, %d placed)'
          % (copy.count, copy.total / 1e6, copy.skipped, len(copy.placed)))

    # 2. what the installer would have written
    for path in extra:
        shutil.copy(path, args.out)
        print('extra: %s' % os.path.basename(path))

    # 3. the audio tracks and their cue sheet
    music = os.path.join(args.out, audio_dir)
    os.mkdir(music)
    audio = [(position, t) for position, t in enumerate(tracks) if t['mode'] == 'AUDIO']
    cue_lines = []
    for position, track in audio:
        source_path, start, end = track_span(tracks, position)
        name = 'track%02d.%s' % (track['number'], audio_format)
        target = os.path.join(music, name)
        with open(source_path, 'rb') as source:
            source.seek(start)
            pcm = source.read(end - start)
        codec = {'ogg': ['-c:a', 'libvorbis', '-q:a', quality],
                 'flac': ['-c:a', 'flac', '-compression_level', '8'], 'wav': ['-c:a', 'pcm_s16le']}[audio_format]
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '44100', '-ac', '2', '-i', '-']
                       + codec + [target], input=pcm, check=True)
        cue_lines += ['FILE "%s" %s' % (name, 'WAVE' if audio_format == 'wav' else audio_format.upper()),
                      '  TRACK %02d AUDIO' % track['number'],
                      '    INDEX 01 00:00:00']
        print('track %2d: %6.1f s -> %s/%s' % (track['number'], (end - start) / RAW / 75, audio_dir, name))
    with open(os.path.join(music, audio_cue), 'w') as cue:
        cue.write('\n'.join(cue_lines) + '\n')
    print('%s/%s: %d tracks' % (audio_dir, audio_cue, len(audio)))


if __name__ == '__main__':
    main()
