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
#    replaces the disc's GENERAL/BOOM.TFX instead of making a second file;
# 3. every InstallShield 3 .z archive the cfg's unpack= names, unpacked as
#    the installer would (the program's own files: exe, data, config);
# 4. every SZDD file the cfg's expand= names, unpacked (COMPRESS.EXE's
#    LZSS: SETUP.EX_ is SETUP.EXE) over the name it unpacks to.
#
# game/portable.cfg (read as portable_cd.py reads it, same keys):
#     image=            the disc, as in portable_cd.py (its cue sheet: build/cd/)
#     patch=<archive>[ <dir>]
#                       .zip, .rar, .7z, .tar...: the archive of a patch; <dir>
#                       is the directory inside it that holds the files, when
#                       they are not at the top. Repeatable; paths relative to
#                       the game repository's root.
#     unpack=<disc file>[ <dir>]
#                       an InstallShield 3 .z archive of the disc, unpacked
#                       into <dir> of the tree (default: its top). Repeatable.
#     expand=<name>[=<out>]
#                       an SZDD file of the disc to unpack, as the disc has
#                       it (NAME, NAME=the name it unpacks to, or a * pattern
#                       of names). Repeatable, one per word.
#     audio_dir=        left out of the data track
#
# OUT_DIR is rebuilt when the cfg, the image or a patch changed (a stamp file
# in it says), otherwise left as it is. Needs unrar, bsdtar or 7z for non-zip
# archives. Nothing is written outside OUT_DIR and build/cd/.

import argparse
import fnmatch
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from portable_cd import Copy, ImageTree, disc_cue, fail, read_cfg  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image'))
from cdimage import Image, parse_cue  # noqa: E402
import szdd  # noqa: E402

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


def read_unpacks(path):
    """The unpack= lines of a cfg: [(archive on the disc, directory)]."""
    unpacks = []
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line.lower().startswith('unpack='):
                words = line.split('=', 1)[1].split()
                if not words:
                    fail('unpack= needs an archive of the disc')
                unpacks.append((words[0], ' '.join(words[1:])))
    return unpacks


def find_path_ci(root, name):
    """root/name, each part of name matched without regard to case."""
    path = root
    for part in name.split('/'):
        entry = find_ci(path, part)
        if entry is None:
            fail('no %s in the disc (unpack=)' % name)
        path = os.path.join(path, entry)
    return path


def unpack_tree(out, unpacks):
    """Unpacks the InstallShield .z or Wise setup files unpack= names, as the
    installer would."""
    installer = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image', 'image.py')
    for name, directory in unpacks:
        target = os.path.join(out, directory) if directory else out
        os.makedirs(target, exist_ok=True)
        if subprocess.run([sys.executable, '-I', installer, 'installer', find_path_ci(out, name), target],
                          stdout=subprocess.DEVNULL).returncode:
            fail('cannot unpack %s' % name)
        print('unpack: %s' % name)


def read_expands(path):
    """The expand= words of a cfg: SZDD files to unpack, each as NAME,
    NAME=out (the name to unpack it to) or a * pattern of names."""
    words = []
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line.lower().startswith('expand='):
                words.extend(line.split('=', 1)[1].split())
    for word in words:
        pattern, equal, name = word.partition('=')
        if not pattern:
            fail('expand= needs a file name: %s' % word)
        if equal and not name:
            fail('expand= needs a name after =: %s' % word)
        if equal and any(c in pattern for c in '*?['):
            fail('expand= =NAME needs a single file, not a pattern: %s' % word)
    return words


def expand_tree(out, words):
    """Unpacks the SZDD files words name in out, as the cfg's expand= says."""
    if not words:
        return
    found = []
    for base, _, names in os.walk(out):
        for name in names:
            found.append(os.path.relpath(os.path.join(base, name), out))
    for word in words:
        pattern, equal, name = word.partition('=')
        if any(c in pattern for c in '*?['):
            lower = pattern.lower()
            matches = sorted(r for r in found
                             if fnmatch.fnmatchcase(r.lower(), lower)
                             or ('/' not in pattern and
                                 fnmatch.fnmatchcase(os.path.basename(r).lower(), lower)))
            if not matches:
                fail('expand= matches nothing on the disc: %s' % pattern)
            targets = [None] * len(matches)
        else:
            matches = [r for r in found if r.lower() == pattern.lower()]
            if not matches:
                fail('expand= has no such file on the disc: %s' % pattern)
            targets = [name if equal else None]
        for match, target in zip(matches, targets):
            source = os.path.join(out, match)
            written = os.path.join(out, target) if target else None
            if written is None:
                with open(source, 'rb') as stream:
                    parsed = szdd.header(stream.read(14))
                if parsed is None:
                    fail('expand= is not an SZDD file: %s' % match)
                written = os.path.join(os.path.dirname(source),
                                        szdd.original_name(os.path.basename(match), parsed[3]))
            szdd.expand_file(source, written)
            print('expand: %s -> %s' % (match, os.path.relpath(written, out)))


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
    parser.add_argument('--cue', help="the disc's cue sheet (default: the one of image= of the cfg, build/cd/)")
    args = parser.parse_args()

    root = os.getcwd()
    cfg = read_cfg(args.cfg)
    cue_path = os.path.abspath(args.cue) if args.cue else disc_cue(root, cfg, args.cfg)
    patches = [(os.path.join(root, archive), inner) for archive, inner in read_patches(args.cfg)]
    for archive, _ in patches:
        if not os.path.isfile(archive):
            fail('no such patch: %s' % archive)
    unpacks = read_unpacks(args.cfg)
    expands = read_expands(args.cfg)
    image_file = os.path.join(root, cfg['image']) if cfg.get('image') else cue_path
    stamp = stamp_of([image_file, cfg.get('audio_dir', 'AUDIO')] + [a + '|' + i for a, i in patches]
                     + [a for a, _ in patches] + ['unpack|' + a + '|' + d for a, d in unpacks] + ['expand|' + w for w in expands])
    stamp_file = os.path.join(args.out, STAMP)
    if os.path.isfile(stamp_file) and open(stamp_file).read().strip() == stamp:
        print('disc: %s is up to date' % args.out)
        return

    image_path, tracks = parse_cue(cue_path)
    data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
    if data is None:
        fail('no data track in %s' % cue_path)
    image = Image(data['path'], data)
    pvd = image.sector(16)
    if pvd[1:6] != b'CD001':
        fail('no ISO9660 volume in the data track')
    root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]

    if os.path.exists(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    copy = Copy([], [], cfg.get('audio_dir', 'AUDIO'), [], args.out)
    copy.tree(ImageTree(image, (root_lba, root_size)), (root_lba, root_size))
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
    unpack_tree(args.out, unpacks)
    expand_tree(args.out, expands)
    with open(stamp_file, 'w') as handle:
        handle.write(stamp + '\n')


if __name__ == '__main__':
    main()
