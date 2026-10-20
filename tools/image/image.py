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

# Disc images and installers of a game, as the port wants them:
#
#     image.py disc IMAGE [-o OUT_DIR] [--name NAME] [--no-audio]
#                         [--iso] [--extract DIR]
#     image.py installer FILE.z OUT_DIR       (-l: list only)
#     image.py expand FILE... [-o OUT_FILE]
#
# disc: IMAGE is .nrg (Nero), .mds (Alcohol 120%; its .mdf is taken from next
# to it), .bin or .iso (with or without a .cue next to it). It writes a cue
# sheet, NAME.cue, to OUT_DIR (default: next to the image; NAME defaults to
# the image's name), which is what the rest of the SDK takes as the disc:
#
#   .nrg   NAME.cue + NAME.bin: the data track re-wrapped into 2352-byte
#          sectors and the audio tracks, byte for byte (--no-audio: data only)
#   .mds   NAME.cue pointing at the .mdf; nothing is copied
#   .bin   the cue sheet beside it, else NAME.cue with one data track
#   .iso   NAME.cue with one MODE1/2048 data track
#
# --iso     also write NAME.iso, the data track alone, 2048 bytes per sector
# --extract DIR   also copy the files of the data track to DIR
#
# Nothing is ever deleted and an existing file is overwritten only if it is
# one of these outputs. Needs no external program.
#
# installer: unpack an InstallShield 3 .z archive (see installshield.py).
#
# expand: unpack SZDD files (COMPRESS.EXE's LZSS, `*_` installer files such
# as SETUP.EX_ for SETUP.EXE), each to its original name beside it; with a
# single FILE, -o says the output file. The name comes from the header; a
# header that says nothing (installers often leave it 0) falls back to the
# usual DOS completions (DL_ -> DLL), and when that does not say either,
# give the name: expand SETUP.IN_ -o SETUP.INF.
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cdimage          # noqa: E402
import installshield    # noqa: E402
import mds              # noqa: E402
import nrg              # noqa: E402
import rawbin           # noqa: E402
import szdd             # noqa: E402


def cmd_disc(args):
    image = args.image
    if not os.path.isfile(image):
        cdimage.fail('%s: not found' % image)
    out_dir = args.out or os.path.dirname(os.path.abspath(image))
    os.makedirs(out_dir, exist_ok=True)
    name = args.name or os.path.splitext(os.path.basename(image))[0]
    ext = os.path.splitext(image)[1].lower()
    if ext == '.nrg':
        cue = nrg.to_cue(image, out_dir, name, audio=not args.no_audio)
    elif ext in ('.mds', '.mdf'):
        cue = mds.mds_to_cue(os.path.splitext(image)[0] + '.mds', out_dir, name)
    elif ext in ('.bin', '.iso', '.img'):
        cue = rawbin.raw_to_cue(image, out_dir, name)
    else:
        cdimage.fail('%s: unknown image type (.nrg, .mds, .bin, .iso)' % image)
    print('cue: %s' % cue)

    if args.iso or args.extract:
        data, _, _ = cdimage.open_data_track(cue)
        pvd = data.sector(16)
        if args.iso:
            iso = os.path.join(out_dir, name + '.iso')
            sectors = struct.unpack_from('<I', pvd, 80)[0]
            cdimage.write_data_iso(data, iso, sectors)
            print('iso: %s: %d sectors' % (iso, sectors))
        if args.extract:
            files, total = cdimage.extract_tree(data, args.extract)
            print('extract: %s: %d files, %.1f MB' % (args.extract, files, total / 1e6))
    return 0


def cmd_installer(args):
    if args.list or not args.out:
        installshield.list_contents(args.z_file)
        return 0
    ok, bad = installshield.extract(args.z_file, args.out)
    print('extracted %d files (%d failed) to %s' % (ok, bad, args.out))
    return 1 if bad else 0


def cmd_expand(args):
    if args.out and len(args.files) != 1:
        cdimage.fail('expand -o needs exactly one FILE')
    for path in args.files:
        target = args.out if args.out else None
        source, written = szdd.expand_file(path, target)
        print('%s -> %s' % (os.path.basename(source), os.path.relpath(written, os.path.dirname(os.path.abspath(source)))))
    return 0


def main():
    parser = argparse.ArgumentParser(description='disc images and installers of a game')
    sub = parser.add_subparsers(dest='command', required=True)
    disc = sub.add_parser('disc', help='cue sheet (and more) for a disc image')
    disc.add_argument('image')
    disc.add_argument('-o', '--out', help='output directory (default: next to the image)')
    disc.add_argument('--name', help='base name of the outputs (default: the image name)')
    disc.add_argument('--no-audio', action='store_true', help='nrg: data track only')
    disc.add_argument('--iso', action='store_true', help='also write the data track as NAME.iso')
    disc.add_argument('--extract', metavar='DIR', help='also copy the data track files to DIR')
    disc.set_defaults(func=cmd_disc)
    inst = sub.add_parser('installer', help='unpack an InstallShield 3 .z archive')
    inst.add_argument('z_file')
    inst.add_argument('out', nargs='?', help='output directory (omit to list)')
    inst.add_argument('-l', '--list', action='store_true')
    inst.set_defaults(func=cmd_installer)
    exp = sub.add_parser('expand', help='unpack SZDD (COMPRESS.EXE) files')
    exp.add_argument('files', nargs='+', metavar='FILE')
    exp.add_argument('-o', '--out', help='the output file (a single FILE only; default: the original name beside it)')
    exp.set_defaults(func=cmd_expand)
    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
