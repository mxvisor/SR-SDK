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
#

# The game's disc, unpacked and patched, run from the game repository's root:
#
#     disc_tree.py OUT_DIR [--cfg game/portable.cfg] [--cue CUE]
#
# OUT_DIR is the disc as the build and the port's launchers see it, made from
# the files the game's repository names, so nobody edits it by hand:
#
# 1. the files of the disc image's data track, as they are (the installer
#    too; only a directory called like audio_dir is left out, on a disc its
#    entries are the audio tracks themselves);
# 2. every patch= archive over them, in the order of the cfg, later ones win.
#    Names are matched without regard to case: a patch's general/Boom.tfx
#    replaces the disc's GENERAL/BOOM.TFX instead of making a second file.
#
# game/portable.cfg (read as portable_cd.py reads it, same keys):
#     cue=, image=      the disc, as in portable_cd.py
#     patch=<archive>[ <dir>]
#                       .zip, .rar, .7z, .tar...: the archive of a patch; <dir>
#                       is the directory inside it that holds the files, when
#                       they are not at the top. Repeatable; paths relative to
#                       the game repository's root.
#     audio_dir=        left out of the data track
#
# OUT_DIR is rebuilt when the cfg, the image or a patch changed (a stamp file
# in it says), otherwise left as it is. Needs unrar, bsdtar or 7z for non-zip
# archives. Nothing is written outside OUT_DIR.

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from portable_cd import Copy, fail, make_cue, read_cfg  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image'))
from cdimage import Image, parse_cue  # noqa: E402

STAMP = '.disc_tree'


def read_patches(path):
    """The patch= lines of a cfg: [(archive, directory inside it)]."""
    patches = []
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line.lower().startswith('patch='):
                words = line.split('=', 1)[1].split()
                if not words:
                    fail('patch= needs an archive')
                patches.append((words[0], ' '.join(words[1:])))
    return patches


def stamp_of(parts):
    digest = hashlib.sha256()
    for part in parts:
        if os.path.isfile(part):
            stat = os.stat(part)
            part = '%s:%d:%d' % (part, stat.st_size, stat.st_mtime_ns)
        digest.update(part.encode() + b'\0')
    return digest.hexdigest()


def extract(archive, out):
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(out)
        return
    commands = (('unrar', ['unrar', 'x', '-y', '-idq', archive, out + os.sep]),
                ('bsdtar', ['bsdtar', '-xf', archive, '-C', out]),
                ('7z', ['7z', 'x', '-y', '-bd', '-o' + out, archive]))
    for tool, command in commands:
        if shutil.which(tool) and subprocess.run(command, stdout=subprocess.DEVNULL).returncode == 0:
            return
    fail('cannot unpack %s (needs unrar, bsdtar or 7z)' % archive)


def find_ci(directory, name):
    """The entry of directory that is called name, ignoring case."""
    lower = name.lower()
    try:
        for entry in os.listdir(directory):
            if entry.lower() == lower:
                return entry
    except FileNotFoundError:
        pass
    return None


def overlay(source, target):
    """Copies the tree source over target, matching names without regard to case."""
    for name in sorted(os.listdir(source)):
        path = os.path.join(source, name)
        existing = find_ci(target, name) or name
        destination = os.path.join(target, existing)
        if os.path.isdir(path):
            os.makedirs(destination, exist_ok=True)
            overlay(path, destination)
        else:
            shutil.copyfile(path, destination)


def main():
    parser = argparse.ArgumentParser(description="Unpacks a game's disc image and puts its patches over it.")
    parser.add_argument('out', help='the directory to make; rebuilt when its inputs changed')
    parser.add_argument('--cfg', default=os.path.join('game', 'portable.cfg'))
    parser.add_argument('--cue', help="the disc's cue sheet (default: cue= of the cfg)")
    args = parser.parse_args()

    root = os.getcwd()
    cfg = read_cfg(args.cfg)
    cue_path = args.cue or (os.path.join(root, cfg['cue']) if cfg.get('cue') else fail(args.cfg + ' has no cue'))
    patches = [(os.path.join(root, archive), inner) for archive, inner in read_patches(args.cfg)]
    for archive, _ in patches:
        if not os.path.isfile(archive):
            fail('no such patch: %s' % archive)
    image_file = os.path.join(root, cfg['image']) if cfg.get('image') else cue_path
    stamp = stamp_of([image_file, cfg.get('audio_dir', 'AUDIO')] + [a + '|' + i for a, i in patches]
                     + [a for a, _ in patches])
    stamp_file = os.path.join(args.out, STAMP)
    if os.path.isfile(stamp_file) and open(stamp_file).read().strip() == stamp:
        print('disc: %s is up to date' % args.out)
        return

    if not os.path.isfile(cue_path):
        if not cfg.get('image'):
            fail('%s not found (and %s has no image= to make it from)' % (cue_path, args.cfg))
        make_cue(os.path.join(root, cfg['image']), cue_path)
    image_path, tracks = parse_cue(cue_path)
    data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
    if data is None:
        fail('no data track in %s' % cue_path)
    image = Image(image_path, data)
    pvd = image.sector(16)
    if pvd[1:6] != b'CD001':
        fail('no ISO9660 volume in the data track')
    root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]

    if os.path.exists(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    copy = Copy(image, [], [], cfg.get('audio_dir', 'AUDIO'))
    copy.tree(root_lba, root_size, args.out)
    print('disc: %d files, %.1f MB from %s' % (copy.count, copy.total / 1e6, os.path.basename(image_file)))

    for archive, inner in patches:
        with tempfile.TemporaryDirectory(prefix='disc_tree_') as temp:
            extract(archive, temp)
            top = temp
            if inner:
                top = os.path.join(temp, inner)
                if not os.path.isdir(top):
                    fail('%s has no directory %s' % (archive, inner))
            overlay(top, args.out)
        print('patch: %s' % os.path.basename(archive))
    with open(stamp_file, 'w') as handle:
        handle.write(stamp + '\n')


if __name__ == '__main__':
    main()
