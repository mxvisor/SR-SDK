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

"""The port's icon, made at build time from the game's own (icon= of game/game.cfg):

    game_icon.py [--root DIR] [--build DIR] SOURCE [--badge sr|recomp|none]
                 [--ico OUT.ico] [--android RES_DIR] [--header OUT.h] [--png OUT.png]

SOURCE is as input_exe= of game.cfg: disc:NAME is a file of the game's disc
(BUILD/disc, made by tools/disc_tree.py; the case of NAME does not matter),
another path is relative to ROOT (default: the current directory).  It is a PE file (.exe, .dll: its first group icon, or FILE#NAME for the
group icon of that name or number) or an image Pillow reads (.ico, .bmp, .png).
Of its pictures the largest, then the deepest, is taken, and a badge is drawn on
it in pixels of the picture's own grid: "SR" in the bottom right corner (the
default), or "recomp" along the bottom.  The result, scaled with whole pixels:

    --ico      a Windows icon of 16, 24, 32, 48, 64, 128 and 256 pixels (the resource
               of the Windows build, scons/game.py)
    --android  RES_DIR/mipmap-<density>/ic_launcher.png (tools/android_apk.py)
    --header   a C header with the picture of 128 pixels in RGBA, game_icon_rgba[] of
               GAME_ICON_SIZE (the window's icon, SDL_SetWindowIcon of
               runtime/core/Game_eventloop.c)
    --png      one picture of 256 pixels (to look at)

The game's icon is never in the repository: it is the game's, and taken from the
game's files on every build, as the original exe is."""

import argparse
import io
import os
import struct
import subprocess
import sys

from PIL import Image

# ------------------------------------------------------------ the PE's icon --

RT_ICON = 3
RT_GROUP_ICON = 14


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('not an MZ file')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not a PE file')
        sections, optional_size = struct.unpack_from('<H12xH', data, pe + 6)
        optional = pe + 24
        magic = struct.unpack_from('<H', data, optional)[0]
        directories = optional + (96 if magic == 0x10b else 112)
        self.resource_rva = struct.unpack_from('<I', data, directories + 2 * 8)[0]
        self.sections = []
        table = optional + optional_size
        for i in range(sections):
            vsize, rva, rawsize, raw = struct.unpack_from('<IIII', data, table + 40 * i + 8)
            self.sections.append((rva, max(vsize, rawsize), raw))
        if not self.resource_rva:
            raise ValueError('no resources')
        self.base = self.offset(self.resource_rva)

    def offset(self, rva):
        for start, size, raw in self.sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError('RVA %#x in no section' % rva)

    def name(self, offset):
        length = struct.unpack_from('<H', self.data, self.base + offset)[0]
        return self.data[self.base + offset + 2:self.base + offset + 2 + 2 * length].decode('utf-16-le')

    def entries(self, directory):
        """(id or name, offset, is a directory) of one resource directory"""
        named, numbered = struct.unpack_from('<HH', self.data, self.base + directory + 12)
        result = []
        for i in range(named + numbered):
            ident, target = struct.unpack_from('<II', self.data, self.base + directory + 16 + 8 * i)
            key = self.name(ident & 0x7fffffff) if ident & 0x80000000 else ident
            result.append((key, target & 0x7fffffff, bool(target & 0x80000000)))
        return result

    def first_leaf(self, directory):
        for _, target, is_dir in self.entries(directory):
            return self.first_leaf(target) if is_dir else self.leaf(target)
        raise ValueError('an empty resource directory')

    def leaf(self, entry):
        rva, size = struct.unpack_from('<II', self.data, self.base + entry)
        start = self.offset(rva)
        return self.data[start:start + size]

    def resource(self, kind, key=None):
        for type_key, type_dir, _ in self.entries(0):
            if type_key != kind:
                continue
            for name_key, name_dir, _ in self.entries(type_dir):
                if key is None or str(name_key).upper() == str(key).upper():
                    return self.first_leaf(name_dir)
        return None


def pe_icon(data, key=None):
    """The group icon of the PE as the bytes of an .ico file."""
    pe = PE(data)
    group = pe.resource(RT_GROUP_ICON, key)
    if group is None:
        raise ValueError('no group icon' + ('' if key is None else ' ' + key))
    count = struct.unpack_from('<H', group, 4)[0]
    images = []
    for i in range(count):
        entry = group[6 + 14 * i:6 + 14 * i + 14]
        ident = struct.unpack_from('<H', entry, 12)[0]
        image = pe.resource(RT_ICON, ident)
        if image is not None:
            images.append((entry[:12], image))
    out = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for head, image in images:
        out += head[:8] + struct.pack('<II', len(image), offset)
        offset += len(image)
    return out + b''.join(image for _, image in images)


def source_path(spec, root, build):
    """The file of SPEC (disc:NAME or a path), with its #NAME kept"""
    path, hash_, key = spec.partition('#')
    if path.startswith('disc:'):
        disc = os.path.join(build, 'disc')
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'disc_tree.py')
        if subprocess.call([sys.executable, '-I', tool, disc], cwd=root) != 0:
            raise ValueError('cannot make %s from game/distrib.cfg' % disc)
        found = disc
        for part in path[len('disc:'):].split('/'):
            entry = next((e for e in os.listdir(found) if e.lower() == part.lower()), None)
            if entry is None:
                raise ValueError('no %s in %s' % (path[len('disc:'):], disc))
            found = os.path.join(found, entry)
        path = found
    return os.path.join(root, path) + hash_ + key


def load(source):
    """The picture of SOURCE (see the module's text) in RGBA."""
    path, _, key = source.partition('#')
    with open(path, 'rb') as stream:
        data = stream.read()
    if data[:2] == b'MZ':
        data = pe_icon(data, key or None)
    image = Image.open(io.BytesIO(data))
    ico = getattr(image, 'ico', None)
    if ico is not None:
        # the largest picture, then the one of the most colours
        best = max(ico.entry, key=lambda e: (e.width * e.height, e.bpp))
        image = ico.getimage((best.width, best.height), best.bpp)
    return image.convert('RGBA')


# ------------------------------------------------------------------ badge --

# a font of 3 pixels by 5 (lower case: 4 above the line and 1 below), "#" is ink
GLYPHS = {
    'r': ['...', '#.#', '##.', '#..', '#..', '...'],
    'e': ['...', '.#.', '###', '#..', '.##', '...'],
    'c': ['...', '.##', '#..', '#..', '.##', '...'],
    'o': ['...', '.#.', '#.#', '#.#', '.#.', '...'],
    'm': ['.....', '##.#.', '#.#.#', '#.#.#', '#.#.#', '.....'],
    'p': ['...', '##.', '#.#', '#.#', '##.', '#..'],
    'S': ['.##', '#..', '.#.', '..#', '##.', '...'],
    'R': ['##.', '#.#', '##.', '#.#', '#.#', '...'],
}
INK = (255, 255, 255, 255)
EDGE = (0, 0, 0, 255)


def text_mask(text):
    """(width, height, the set of ink pixels) of TEXT, one pixel between letters"""
    ink = set()
    x = 0
    height = 0
    for letter in text:
        rows = GLYPHS[letter]
        for y, row in enumerate(rows):
            for dx, cell in enumerate(row):
                if cell == '#':
                    ink.add((x + dx, y))
                    height = max(height, y + 1)
        x += len(rows[0]) + 1
    top = min(y for _, y in ink)
    return x - 1, height - top, {(px, py - top) for px, py in ink}


def draw_text(image, text, right, bottom, center=False):
    """TEXT in INK with an EDGE of one pixel around it (also on the diagonals),
    its box's bottom right corner at (right, bottom), or centred on RIGHT"""
    width, height, ink = text_mask(text)
    left = right - width // 2 if center else right - width
    top = bottom - height
    pixels = image.load()
    w, h = image.size
    for x, y in ink:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                px, py = left + x + dx, top + y + dy
                if 0 <= px < w and 0 <= py < h and (x + dx, y + dy) not in ink:
                    pixels[px, py] = EDGE
    for x, y in ink:
        if 0 <= left + x < w and 0 <= top + y < h:
            pixels[left + x, top + y] = INK


def badge(image, kind):
    image = image.copy()
    w, h = image.size
    if w < 24 or h < 16:
        # a 16 pixel icon has no room: the badge on a picture of twice the size
        image = image.resize((w * 2, h * 2), Image.Resampling.NEAREST)
        w, h = image.size
    if kind == 'recomp':
        draw_text(image, 'recomp', w // 2, h - 2, center=True)
    elif kind == 'sr':
        draw_text(image, 'SR', w - 2, h - 2)
    elif kind != 'none':
        raise ValueError('badge: sr, recomp or none, not ' + kind)
    return image


# ---------------------------------------------------------------- the sizes --

def scaled(image, size):
    """IMAGE at SIZE pixels: whole pixels up (nearest), then a box filter down to
    a size that is no multiple of the picture's"""
    w = image.size[0]
    factor = -(-size // w)
    big = image.resize((w * factor, w * factor), Image.Resampling.NEAREST) if factor > 1 else image
    return big if big.size[0] == size else big.resize((size, size), Image.Resampling.BOX)


ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
ANDROID_SIZES = (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192))


def write_ico(image, path):
    big = scaled(image, 256)
    images = [scaled(image, s) for s in ICO_SIZES]
    # Pillow writes each size from the first picture: give it the ones made here
    big.save(path, format='ICO', sizes=[(s, s) for s in ICO_SIZES], append_images=images)


def write_android(image, res):
    for density, size in ANDROID_SIZES:
        directory = os.path.join(res, 'mipmap-' + density)
        os.makedirs(directory, exist_ok=True)
        scaled(image, size).save(os.path.join(directory, 'ic_launcher.png'), optimize=True)


HEADER_SIZE = 128


def write_header(image, path):
    data = scaled(image, HEADER_SIZE).tobytes()
    lines = ['/* the window icon of the port: made by SR-SDK/tools/game_icon.py from icon= of game.cfg */',
             '#define GAME_ICON_SIZE %d' % HEADER_SIZE,
             'static const unsigned char game_icon_rgba[%d] = {' % len(data)]
    for i in range(0, len(data), 24):
        lines.append('    ' + ','.join('%d' % b for b in data[i:i + 24]) + ',')
    lines.append('};')
    with open(path, 'wt') as stream:
        stream.write('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description="The port's icon from the game's own.")
    parser.add_argument('--root', default='.', help="the game repository (default: here)")
    parser.add_argument('--build', help='its build directory (default ROOT/build)')
    parser.add_argument('source', help='an exe or dll (FILE#NAME for one group icon), or an .ico/.bmp/.png')
    parser.add_argument('--badge', default='sr', choices=('sr', 'recomp', 'none'))
    parser.add_argument('--ico', help='write a Windows .ico')
    parser.add_argument('--android', metavar='RES', help='write RES/mipmap-*/ic_launcher.png')
    parser.add_argument('--header', help='write a C header of the window icon')
    parser.add_argument('--png', help='write a 256 pixel .png')
    args = parser.parse_args()
    try:
        root = os.path.abspath(args.root)
        source = source_path(args.source, root, os.path.abspath(args.build or os.path.join(root, 'build')))
        image = badge(load(source), args.badge)
    except (OSError, ValueError) as error:
        sys.exit('game_icon: %s: %s' % (args.source, error))
    if args.ico:
        write_ico(image, args.ico)
    if args.android:
        write_android(image, args.android)
    if args.header:
        write_header(image, args.header)
    if args.png:
        scaled(image, 256).save(args.png)


if __name__ == '__main__':
    main()
