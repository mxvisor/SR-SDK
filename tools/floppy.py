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

# A set of DOS floppy images and the HUF archives on them, as the game's
# installer leaves the game directory:
#
#     floppy.py OUT_DIR IMAGE...
#
# The images are FAT12 floppies.  Every *.HUF archive is unpacked in the
# alphabetical order of their names, whatever the disk and directory order is
# (the installer keeps a list of them and appends the type-5 entries to the file
# the preceding type-1 entry made).  The result is written to OUT_DIR: the files
# of the archives at their paths.  Nothing is mounted.  What else the installer
# writes (a batch file, empty directories) is the game's: extra_files= and
# empty_dirs= of its distrib.cfg (distrib_cd.py).
#
# HUF: a sequence of entries while the 16-bit flag is 1 or 5, each an unsigned
# 32-bit length and a NUL-terminated DOS path, then 00 00 and the data.  Every
# entry's data is an unsigned 32-bit original size followed by an LZHUF stream
# (N_CHAR 314, T 627, THRESHOLD 2, F 60, a 4096-byte ring) that unpacks to that
# size.  A type-1 entry starts a new file, a type-5 entry appends to the last
# type-1 file (say, a game's alternate graphics and palettes).
import argparse
import os
import struct
import sys

THRESHOLD = 2
F = 60
N_CHAR = 256 - THRESHOLD + F
T = N_CHAR * 2 - 1
R = T - 1
N = 4096
MAX_FREQ = 0x8000

D_CODE = (
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
    0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
    0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
)

D_LEN = (
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
)


class LZHUF:
    def __init__(self, data):
        self.data, self.pos = data, 0
        self.getbuf = self.getlen = 0
        self.freq = [0] * (T + 1)
        self.prnt = [0] * (T + N_CHAR)
        self.son = [0] * T
        for i in range(N_CHAR):
            self.freq[i] = 1
            self.son[i] = i + T
            self.prnt[i + T] = i
        i, j = 0, N_CHAR
        while j <= R:
            self.freq[j] = self.freq[i] + self.freq[i + 1]
            self.son[j] = i
            self.prnt[i] = self.prnt[i + 1] = j
            i, j = i + 2, j + 1
        self.freq[T] = 0xffff
        self.prnt[R] = 0

    def _refill(self):
        i = self.data[self.pos] if self.pos < len(self.data) else 0
        self.pos += 1
        self.getbuf = (self.getbuf | (i << (8 - self.getlen))) & 0xffff
        self.getlen += 8

    def bit(self):
        while self.getlen <= 8:
            self._refill()
        i = self.getbuf
        self.getbuf = (self.getbuf << 1) & 0xffff
        self.getlen -= 1
        return i >> 15

    def byte(self):
        while self.getlen <= 8:
            self._refill()
        i = self.getbuf
        self.getbuf = (self.getbuf << 8) & 0xffff
        self.getlen -= 8
        return i >> 8

    def reconst(self):
        j = 0
        for i in range(T):
            if self.son[i] >= T:
                self.freq[j] = (self.freq[i] + 1) // 2
                self.son[j] = self.son[i]
                j += 1
        i = 0
        j = N_CHAR
        while j < T:
            k = i + 1
            f = self.freq[i] + self.freq[k]
            self.freq[j] = f
            k = j - 1
            while f < self.freq[k]:
                k -= 1
            k += 1
            count = (j - k)
            self.freq[k + 1:k + 1 + count] = self.freq[k:k + count]
            self.freq[k] = f
            self.son[k + 1:k + 1 + count] = self.son[k:k + count]
            self.son[k] = i
            i, j = i + 2, j + 1
        for i in range(T):
            k = self.son[i]
            if k >= T:
                self.prnt[k] = i
            else:
                self.prnt[k] = self.prnt[k + 1] = i

    def update(self, c):
        if self.freq[R] == MAX_FREQ:
            self.reconst()
        c = self.prnt[c + T]
        while True:
            self.freq[c] += 1
            k = self.freq[c]
            l = c + 1
            if k > self.freq[l]:
                while k > self.freq[l]:
                    l += 1
                l -= 1
                self.freq[c] = self.freq[l]
                self.freq[l] = k
                i = self.son[c]
                self.prnt[i] = l
                if i < T:
                    self.prnt[i + 1] = l
                j = self.son[l]
                self.son[l] = i
                self.prnt[j] = c
                if j < T:
                    self.prnt[j + 1] = c
                self.son[c] = j
                c = l
            c = self.prnt[c]
            if c == 0:
                break

    def char(self):
        c = self.son[R]
        while c < T:
            c += self.bit()
            c = self.son[c]
        c -= T
        self.update(c)
        return c

    def position(self):
        i = self.byte()
        c = D_CODE[i] << 6
        j = D_LEN[i] - 2
        while j:
            i = ((i << 1) + self.bit()) & 0xffff
            j -= 1
        return c | (i & 0x3f)

    def decode(self, size):
        out = bytearray()
        window = bytearray(b' ' * (N - F)) + bytearray(F)
        r = N - F
        while len(out) < size:
            c = self.char()
            if c < 256:
                out.append(c)
                window[r] = c
                r = (r + 1) & (N - 1)
            else:
                i = (r - self.position() - 1) & (N - 1)
                length = c - 255 + THRESHOLD
                for _ in range(length):
                    if len(out) == size:
                        break
                    c = window[i]
                    i = (i + 1) & (N - 1)
                    out.append(c)
                    window[r] = c
                    r = (r + 1) & (N - 1)
        return bytes(out)


def huf_entries(data):
    """The (flag, name, unpacked_data) entries of a HUF archive, in file order."""
    off = 0
    entries = []
    while off + 6 <= len(data):
        flag, length = struct.unpack_from('<HI', data, off)
        if flag not in (1, 5):
            break
        end = data.find(b'\0', off + 6)
        if end < 0:
            break
        name = data[off + 6:end]
        if not name or len(name) > 127:
            break
        entries.append((flag, name.decode('latin1'), length))
        off = end + 1
    if data[off:off + 2] != b'\0\0':
        raise ValueError('no end of HUF header')
    off += 2
    payload = data[off:]
    if len(payload) != sum(length for _, _, length in entries):
        raise ValueError('HUF data length does not match its entries')
    out = []
    pos = 0
    for flag, name, length in entries:
        if length < 4:
            raise ValueError('short HUF entry %s' % name)
        size = struct.unpack_from('<I', payload, pos)[0]
        pos += 4
        unpacked = LZHUF(payload[pos:pos + length - 4]).decode(size)
        pos += length - 4
        out.append((flag, name, unpacked))
    return out


def fat_files(image):
    """The files of a FAT12 image: [(name, data)]."""
    with open(image, 'rb') as stream:
        disk = stream.read()
    if disk[510:512] != b'\x55\xaa':
        raise ValueError('%s: no FAT boot sector' % image)
    bps = struct.unpack_from('<H', disk, 11)[0]
    spc = disk[13]
    reserved = struct.unpack_from('<H', disk, 14)[0]
    fats = disk[16]
    root_entries = struct.unpack_from('<H', disk, 17)[0]
    total = struct.unpack_from('<H', disk, 19)[0] or struct.unpack_from('<I', disk, 32)[0]
    fat_size = struct.unpack_from('<H', disk, 22)[0] or struct.unpack_from('<I', disk, 36)[0]
    root_sectors = (root_entries * 32 + bps - 1) // bps
    root_off = (reserved + fats * fat_size) * bps
    data_off = root_off + root_sectors * bps
    clusters = (total - (reserved + fats * fat_size + root_sectors)) // spc
    if clusters >= 4085:
        raise ValueError('%s: not a FAT12 image' % image)
    fat_off = reserved * bps
    fat = disk[fat_off:fat_off + fat_size * bps]

    def entry(n):
        i = n + n // 2
        value = fat[i] | (fat[i + 1] << 8)
        return value >> 4 if n & 1 else value & 0xfff

    def chain(start):
        n = start
        seen = set()
        while 2 <= n < 0xff8:
            if n in seen:
                raise ValueError('%s: cluster loop' % image)
            seen.add(n)
            yield n
            n = entry(n)

    def file_data(start, size):
        return b''.join(disk[data_off + (n - 2) * spc * bps:
                              data_off + (n - 1) * spc * bps] for n in chain(start))[:size]

    files = []

    def directory(off, size, prefix):
        for pos in range(off, off + size, 32):
            item = disk[pos:pos + 32]
            if len(item) < 32 or item[0] == 0:
                break
            if item[0] == 0xe5 or item[11] & 0x0f == 0x0f or item[11] & 0x08:
                continue
            name = item[:8].decode('latin1').rstrip()
            ext = item[8:11].decode('latin1').rstrip()
            name = name + ('.' + ext if ext else '')
            if name in ('.', '..'):
                continue
            cluster = struct.unpack_from('<H', item, 26)[0]
            length = struct.unpack_from('<I', item, 28)[0]
            if item[11] & 0x10:
                directory(data_off + (cluster - 2) * spc * bps, spc * bps, prefix + name + '/')
            else:
                files.append((prefix + name, file_data(cluster, length)))

    directory(root_off, root_entries * 32, '')
    return files


def unpack(images, out):
    archives = []
    for image in images:
        for name, data in fat_files(image):
            if name.upper().endswith('.HUF'):
                archives.append((name, data))
    archives.sort(key=lambda item: item[0].upper())
    files = {}
    current = None
    for name, data in archives:
        for flag, path, unpacked in huf_entries(data):
            path = path.replace('\\', '/').upper()
            if path.startswith('/') or '..' in path.split('/'):
                raise ValueError('%s: bad path %s' % (name, path))
            if flag == 1:
                current = path
                files[current] = bytearray(unpacked)
            else:
                if current is None:
                    raise ValueError('%s: append without a file' % name)
                files[current].extend(unpacked)
        print('floppy: %s' % name)
    for path, data in files.items():
        target = os.path.join(out, *path.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as stream:
            stream.write(data)
    print('floppy: %d files, %.1f MB' % (len(files), sum(len(data) for data in files.values()) / 1e6))


def main():
    parser = argparse.ArgumentParser(description="Unpacks a game's floppy images and HUF archives.")
    parser.add_argument('out', help='the directory to make')
    parser.add_argument('images', nargs='+', help='the FAT12 floppy images')
    args = parser.parse_args()
    for image in args.images:
        if not os.path.isfile(image):
            sys.exit('floppy: no such image: %s' % image)
    if os.path.exists(args.out):
        import shutil
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    try:
        unpack(args.images, args.out)
    except Exception as error:
        sys.exit('floppy: %s' % error)


if __name__ == '__main__':
    main()
