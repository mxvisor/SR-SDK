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

# A distrib copy of a game's disc, run from the game repository's root:
#
#     distrib_cd.py OUT_DIR [--cfg game/distrib.cfg] [--cue CUE]
#                    [--audio flac|wav|ogg] [--quality Q]
#
# The game directory of a distrib copy, OUT_DIR, which must not exist yet: the
# directory the game's own installer makes in its fullest install, with the
# files that install still leaves on the disc copied in too, so the port needs
# no disc (a game directory with the disc's files is taken as the disc). It is
# made of:
#
# 1. the files of the disc's data track (ISO9660), at their disc paths, except
#    those game/distrib.cfg names (what the installer does not copy, and
#    what it copies elsewhere: place=) and except a directory called like
#    audio_dir: on a disc it holds entries whose extents are the audio tracks
#    themselves, not files. What the install leaves on the disc and the game
#    still reads there stays at its disc path, so the game directory is the
#    disc as well;
# 2. extra_files, the files the installer would have written (files of the
#    repository, game/install/ by custom), as they are, and soundfont=, the
#    General MIDI bank of the port's synthesizer, cut from the system's
#    FluidR3_GM (sf2_subset.py);
# 3. place=<glob> <dest>..., the files the installer copies to another place
#    than their disc path, relocated from the disc tree to OUT_DIR: <glob> is
#    either a disc-path glob (case-insensitive fnmatch against the path from
#    the disc's root) with <dest> a directory (ending in "/") or a file name,
#    or a disc directory (ending in "/") with <dest> the directory its tree
#    goes to ("./" is OUT_DIR itself). A file every rule of which it matches
#    is written to each of their destinations and not at its disc path (a
#    rule whose <dest> is the disc path keeps it there too);
# 4. audio_dir/: the audio tracks, one file each, and a cue sheet listing
#    them. flac decodes to the disc's samples bit for bit; wav is the same
#    uncompressed; ogg is lossy, only smaller.
#
# game/distrib.cfg (paths relative to the game repository's root):
#     image=<the disc image>           .nrg, .mds, .bin, .img or .iso (a .bin or
#                                      .img with its cue sheet beside it), the
#                                      one path outside the repository
#                                      (required, unless floppy=). Its cue sheet is made in
#                                      build/cd/ with image/image.py, once.
#                                      `scons image=<path>` builds from another
#                                      (build/disc-image says which)
#     floppy=<images>                  a game on floppies instead: the FAT12 images
#                                      in order, their HUF archives unpacked as the
#                                      installer does (floppy.py) into build/disc,
#                                      the disc of the copy; no audio tracks
#     patch=<archive>[ <dir>]          archives over the disc, in order: the
#                                      copy is taken from disc_tree.py's
#                                      build/disc, not the bare image
#     unpack=<disc file>[ <dir>]       an InstallShield .z or a Wise setup
#                                      program of the disc, unpacked into the copy
#                                      (so is build/disc)
#     expand=<name>[=<out>]            an SZDD file of the disc, unpacked (disc_tree.py)
#     skip_files=<patterns>            files not to copy, at any depth
#     skip_dirs=<patterns>             directories not to copy, at any depth
#     extra_files=<paths>              copied into OUT_DIR as they are
#     soundfont=<presets>              OUT_DIR/soundfont.sf2 (+ .LICENSE): FluidR3_GM
#                                      of the system cut to these bank:program
#     empty_dirs=<paths>               created in OUT_DIR, empty (a directory the
#                                      game expects but the disc does not have)
#     place=<glob> <dest>              disc-glob (or disc directory/) +
#                                      destination, repeatable
#     audio_dir=AUDIO                  where the tracks go
#     audio_cue=<name>.cue             the tracks' cue sheet (required)
#     audio=ogg                        flac, wav or ogg (default ogg)
#     quality=6                        ogg only: -1..10
# Patterns are fnmatch patterns, separated by spaces, matched in upper case
# against the name, or, when they hold a "/", against the path from the
# disc's root, which begins with "/" (/SMALL.LFT is the root's, not
# FONTS/SMALL.LFT). A file that a place= rule matches is copied even when a
# skip pattern names it or its directory.
#
# Needs ffmpeg. Nothing is written outside OUT_DIR and build/ of the repository.

import argparse
import fnmatch
import os
import shutil
import struct
import subprocess
import sys
from typing import NoReturn

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cdimage import RAW, Image, parse_cue, records, track_span  # noqa: E402
import sf2_subset  # noqa: E402


def fail(message) -> NoReturn:
    sys.exit('distrib_cd: ' + message)


# the disc image `scons image=` chose instead of image= of the cfg (scons/game.py),
# relative to the repository's root, the current directory
DISC_IMAGE_CHOICE = os.path.join('build', 'disc-image')


def read_cfg(path):
    # as scons/game.py reads it; image= is the one scons/game.py was told to use
    cfg = {}
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line:
                key, value = line.split('=', 1)
                cfg[key.strip()] = value.strip()
    if 'image' in cfg and os.path.isfile(DISC_IMAGE_CHOICE):
        with open(DISC_IMAGE_CHOICE, 'rt') as stream:
            chosen = stream.read().strip()
        if chosen:
            cfg['image'] = chosen
    return cfg


def read_floppies(path):
    """The floppy= images of a cfg: a FAT12 set the game's installer reads."""
    images = []
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line.lower().startswith('floppy='):
                images.extend(line.split('=', 1)[1].split())
    return images


def disc_cue(root, cfg, cfg_name):
    """The cue sheet of the cfg's image=, in build/cd/ of the repository. The
    image is the one file outside the repository the SDK reads; its cue sheet,
    and what that needs beside it (a link to the image, or the .bin of an
    .nrg), are made there by image/image.py, once."""
    if not cfg.get('image'):
        fail('%s has no image=' % cfg_name)
    image = os.path.join(root, cfg['image'])
    if not os.path.isfile(image):
        fail('no such disc image: %s' % image)
    out_dir = os.path.join(root, 'build', 'cd')
    name = os.path.splitext(os.path.basename(image))[0]
    cue = os.path.join(out_dir, name + '.cue')
    if os.path.isfile(cue):
        return cue
    command = [sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'image', 'image.py'),
               'disc', image, '-o', out_dir, '--name', name]
    print('disc image: %s -> %s' % (image, cue))
    if subprocess.run(command).returncode or not os.path.isfile(cue):
        fail('cannot make %s from %s' % (cue, image))
    return cue


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


def matches(name, rel, patterns):
    """name (or, for a pattern with a "/", /rel) matches one of patterns."""
    upper, path = name.upper(), '/' + rel.upper()
    return any(fnmatch.fnmatchcase(path if '/' in pattern else upper, pattern.upper()) for pattern in patterns)


class ImageTree:
    """The data track's ISO9660 tree: a node is a directory's (extent, length)."""
    def __init__(self, image, root):
        self.image, self.root = image, root

    def entries(self, node):
        for name, extent, length, directory in records(self.image, *node):
            yield name, directory, (extent, length), length

    def read(self, node):
        return self.image.read(*node)


class DirTree:
    """A directory with the disc's files (disc_tree.py's build/disc): a node is a path."""
    def __init__(self, root):
        self.root = root

    def entries(self, node):
        for name in sorted(os.listdir(node)):
            path = os.path.join(node, name)
            if node != self.root or name != '.disc_tree':
                yield name, os.path.isdir(path), path, os.path.getsize(path)

    def read(self, node):
        with open(node, 'rb') as handle:
            return handle.read()


class Copy:
    def __init__(self, skip_files, skip_dirs, audio_dir, place_rules, out):
        self.skip_files, self.skip_dirs = skip_files, skip_dirs
        self.audio_dir = audio_dir.upper()
        self.place_rules = place_rules
        self.out = out
        self.placed = set()
        self.count = self.total = self.skipped = 0

    def destinations(self, rel):
        """Where place= puts the disc file rel (paths in OUT_DIR); [] when no
        rule matches it."""
        result = []
        for glob, dest in self.place_rules:
            if glob.endswith('/'):
                if rel.upper().startswith(glob.upper()):
                    result.append(os.path.normpath(os.path.join(dest, rel[len(glob):])))
            elif fnmatch.fnmatchcase(rel.upper(), glob.upper()):
                result.append(os.path.join(dest, os.path.basename(rel)) if dest.endswith('/') else dest)
        return result

    def tree(self, source, node, prefix='', skipped=False):
        """The files under node, prefix their disc path; skipped: node is a
        directory not to copy, whose place= files are still copied."""
        for name, directory, child, length in source.entries(node):
            rel = prefix + name
            if directory:
                if not prefix and name.upper() == self.audio_dir:
                    continue
                skip = skipped or matches(name, rel, self.skip_dirs)
                if skip and not skipped:
                    self.skipped += 1
                if not skip:
                    os.makedirs(os.path.join(self.out, rel), exist_ok=True)
                self.tree(source, child, rel + '/', skip)
                continue
            targets = self.destinations(rel)
            if targets:
                self.placed.add(rel)
            elif skipped or matches(name, rel, self.skip_files):
                self.skipped += not skipped
                continue
            else:
                targets = [rel]
            data = source.read(child)
            for target in targets:
                target = os.path.join(self.out, target)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'wb') as handle:
                    handle.write(data)
                self.count += 1
                self.total += length


def main():
    parser = argparse.ArgumentParser(description="Makes a distrib copy of a game's disc.")
    parser.add_argument('out', help='the new game directory; it must not exist yet')
    parser.add_argument('--cfg', default=os.path.join('game', 'distrib.cfg'))
    parser.add_argument('--cue', help="the disc's cue sheet (default: the one of image= of the cfg, build/cd/)")
    parser.add_argument('--audio', choices=('flac', 'wav', 'ogg'))
    parser.add_argument('--quality', help='ogg only: quality -1..10')
    args = parser.parse_args()

    root = os.getcwd()
    cfg = read_cfg(args.cfg)
    floppies = read_floppies(args.cfg)
    if floppies:
        cue_path = None
        audio_cue = cfg.get('audio_cue')
    else:
        cue_path = os.path.abspath(args.cue) if args.cue else disc_cue(root, cfg, args.cfg)
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
    if cfg.get('soundfont') and sf2_subset.fluid_source() is None:
        fail(sf2_subset.FLUID_MISSING)

    if floppies:
        tracks = []
    else:
        image_path, tracks = parse_cue(cue_path)
        data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
        if data is None:
            fail('no data track in %s' % cue_path)
    if os.path.exists(args.out):
        fail('%s exists; give a new directory' % args.out)
    os.makedirs(args.out)

    # 1. the data track, without the installer
    copy = Copy(cfg.get('skip_files', '').split(), cfg.get('skip_dirs', '').split(), audio_dir,
                read_place_rules(args.cfg), args.out)
    made_disc = any(line.split('#', 1)[0].strip().lower().startswith(('patch=', 'unpack=', 'expand=')) for line in open(args.cfg))
    if floppies or made_disc:
        # the disc with its patch= archives over it and its unpack= archives
        # and expand= files unpacked, not the bare image
        disc = os.path.join(root, 'build', 'disc')
        command = [sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'disc_tree.py'),
                   disc, '--cfg', args.cfg]
        if cue_path:
            command += ['--cue', cue_path]
        subprocess.run(command, check=True)
        copy.tree(DirTree(disc), disc)
    else:
        image = Image(data['path'], data)
        pvd = image.sector(16)
        if pvd[1:6] != b'CD001':
            fail('no ISO9660 volume in the data track')
        root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
        copy.tree(ImageTree(image, (root_lba, root_size)), (root_lba, root_size))
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
    if cfg.get('soundfont'):
        sf2_subset.fluid_bank(args.out, cfg['soundfont'].split())
    for path in cfg.get('empty_dirs', '').split():
        os.makedirs(os.path.join(args.out, path), exist_ok=True)

    # 3. the audio tracks and their cue sheet
    if not tracks:
        return
    music = os.path.join(args.out, audio_dir)
    os.mkdir(music)
    audio = [(position, t) for position, t in enumerate(tracks) if t['mode'] == 'AUDIO']
    cue_lines = []
    # where each track is on the disc, as a real disc's table of contents has it: the
    # game finds a track by that address (a track file's own INDEX 01 is 0)
    file_base, total = {}, 0
    for track in tracks:
        if track['path'] not in file_base:
            file_base[track['path']] = total
            total += os.path.getsize(track['path']) // RAW

    def msf(frames):
        return '%02d:%02d:%02d' % (frames // 4500, frames // 75 % 60, frames % 75)

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
                      '    INDEX 01 00:00:00',
                      '    REM DISC %s' % msf(file_base[track['path']] + track['index1'])]
        print('track %2d: %6.1f s -> %s/%s' % (track['number'], (end - start) / RAW / 75, audio_dir, name))
    cue_lines.append('REM LEADOUT %s' % msf(total))
    with open(os.path.join(music, audio_cue), 'w') as cue:
        cue.write('\n'.join(cue_lines) + '\n')
    print('%s/%s: %d tracks' % (audio_dir, audio_cue, len(audio)))


if __name__ == '__main__':
    main()
