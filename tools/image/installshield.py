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

"""InstallShield 3 (.z) installers: PKWARE DCL "implode" data, one file after
another, and a directory table at the end.

The format is from stix by Veit Kannegieser and its Linux port by DeclanHoare.
The archive has a 255-byte header, at the magic 13 5D 65 8C, then the files'
compressed data in order, then the table at `namenanfang` (from the header):
per directory, a file count (2 bytes), a block length (2 bytes) and a name;
per file, 7 unknown bytes, the COMPRESSED size (4 bytes; not the unpacked
one), 4 unknown, the DOS date and time, and a Pascal string with the name.

The decompressor is blast.c by Mark Adler (zlib's contrib/blast), built the
first time it is needed with the system C compiler, into the user's cache.
"""
import ctypes
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

from cdimage import fail

HERE = os.path.dirname(os.path.abspath(__file__))
MAGIC = b'\x13\x5d\x65\x8c'
HDR_SIZE = 255  # sizeof(dateikopf_typ) in Pascal


def load_blast():
    """The blast shim as a ctypes library, compiled when it is not cached."""
    sources = [os.path.join(HERE, name) for name in ('blast.c', 'blast_shim.c', 'blast.h')]
    digest = hashlib.sha256()
    for path in sources:
        digest.update(open(path, 'rb').read())
    cache = os.path.join(os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache'), 'sr-sdk-image')
    library = os.path.join(cache, 'libblast-%s.so' % digest.hexdigest()[:12])
    if not os.path.exists(library):
        os.makedirs(cache, exist_ok=True)
        compiler = os.environ.get('CC', 'cc')
        with tempfile.TemporaryDirectory(dir=cache) as work:
            built = os.path.join(work, 'libblast.so')
            command = [compiler, '-O2', '-fPIC', '-shared', '-o', built, sources[0], sources[1]]
            try:
                subprocess.run(command, check=True, capture_output=True, text=True)
            except (OSError, subprocess.CalledProcessError) as error:
                fail('cannot build blast (%s): %s' % (' '.join(command), getattr(error, 'stderr', error)))
            os.replace(built, library)
    lib = ctypes.CDLL(library)
    lib.is3_blast.restype = ctypes.POINTER(ctypes.c_ubyte)
    lib.is3_blast.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    lib.is3_free.argtypes = [ctypes.c_void_p]
    return lib


def blast_decompress(lib, data):
    """Decompress PKWARE DCL implode data. Returns the bytes or None."""
    out_len = ctypes.c_size_t(0)
    result = lib.is3_blast(data, len(data), ctypes.byref(out_len))
    if not result:
        return None
    try:
        return bytes(result[:out_len.value]) if out_len.value > 0 else None
    finally:
        lib.is3_free(result)


def parse_header(data, offset):
    """Parse the 255-byte IS3 archive header."""
    hdr = data[offset:offset + HDR_SIZE]
    if hdr[:4] != MAGIC:
        raise ValueError(f"Invalid magic at offset {offset}")
    return {
        'datei_anzahl':  struct.unpack_from('<H', hdr, 0x0c)[0],  # file count
        'archiv_laenge': struct.unpack_from('<I', hdr, 0x12)[0],  # archive length
        'namenanfang':   struct.unpack_from('<I', hdr, 0x29)[0],  # dir table offset
        'verzeichnisse': struct.unpack_from('<H', hdr, 0x31)[0],  # dir count
    }


def parse_directory(data, base, namenanfang, verzeichnisse, datei_anzahl):
    """Parse directory and file records from the directory table."""
    o = namenanfang
    dirs = []

    # Parse directory name records
    for _ in range(verzeichnisse):
        pos = base + o
        anzahl, blocklaenge = struct.unpack_from('<HH', data, pos)
        name_raw = data[pos + 4:pos + blocklaenge]
        name = name_raw.split(b'\x00')[0].decode('cp437', errors='replace')
        name = name.replace('\\', '/')
        # Strip non-path characters (e.g. 0x04 control bytes in root dir records)
        name = ''.join(c for c in name if c.isprintable() and c not in '/\\:\0')
        dirs.append({'count': anzahl, 'name': name})
        o += blocklaenge

    # Build dir->file assignment
    file_to_dir = []
    for d in dirs:
        for _ in range(d['count']):
            file_to_dir.append(d['name'])
    # Pad if needed
    while len(file_to_dir) < datei_anzahl:
        file_to_dir.append('')

    # Parse file records
    # datei_kopf layout (offsets within record):
    #   0x00-0x06: u_00 (7 bytes, flags/unknown)
    #   0x07-0x0a: laenge (4 bytes) = COMPRESSED size of file data
    #   0x0b-0x0e: u_0b (4 bytes, checksum/unknown)
    #   0x0f-0x10: datum (2 bytes, DOS date)
    #   0x11-0x12: zeit  (2 bytes, DOS time)
    #   0x13-0x16: u_13 (4 bytes)
    #   0x17-0x18: blocklaenge (2 bytes) = total size of this directory record
    #   0x19-0x1c: u_19 (4 bytes)
    #   0x1d:      name_len (1 byte, Pascal string length)
    #   0x1e+:     filename characters
    files = []
    for i in range(datei_anzahl):
        pos = base + o
        blocklaenge = struct.unpack_from('<H', data, pos + 0x17)[0]
        rec = data[pos:pos + blocklaenge]

        comp_size = struct.unpack_from('<I', rec, 0x07)[0]
        datum     = struct.unpack_from('<H', rec, 0x0f)[0]
        zeit      = struct.unpack_from('<H', rec, 0x11)[0]
        name_len  = rec[0x1d]
        fname     = rec[0x1e:0x1e + name_len].decode('cp437', errors='replace')

        files.append({
            'name':      fname,
            'dir':       file_to_dir[i] if i < len(file_to_dir) else '',
            'comp_size': comp_size,
            'datum':     datum,
            'zeit':      zeit,
        })
        o += blocklaenge

    return dirs, files


def read_archive(z_path):
    data = open(z_path, 'rb').read()
    start = data.find(MAGIC)      # also finds it after an SFX stub
    if start == -1:
        fail('IS3 magic %s not found in %s' % (MAGIC.hex(), z_path))
    hdr = parse_header(data, start)
    dirs, files = parse_directory(data, start, hdr['namenanfang'], hdr['verzeichnisse'], hdr['datei_anzahl'])
    return data, start, hdr, dirs, files


def extract(z_path, out_dir, log=print):
    """Extract all files of an IS3 .z archive; returns (ok, failed)."""
    lib = load_blast()
    data, start, hdr, dirs, files = read_archive(z_path)
    log('%s: %d files, %d directories' % (z_path, hdr['datei_anzahl'], hdr['verzeichnisse']))
    os.makedirs(out_dir, exist_ok=True)
    pos, ok, bad = start + HDR_SIZE, 0, 0
    for entry in files:
        rel = '/'.join(filter(None, [entry['dir'], entry['name']]))
        target = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(target) or out_dir, exist_ok=True)
        size = entry['comp_size']
        if size == 0:
            open(target, 'wb').close()
        else:
            # a little data after the file's own lets blast see its end code
            unpacked = blast_decompress(lib, data[pos:pos + size + 64])
            if unpacked is None:
                print('FAIL %s (compressed size %d)' % (rel, size), file=sys.stderr)
                bad += 1
            else:
                with open(target, 'wb') as handle:
                    handle.write(unpacked)
                ok += 1
        pos += size
    if pos != start + hdr['namenanfang']:
        print('WARNING: data ends at %d, the table starts at %d' % (pos, start + hdr['namenanfang']), file=sys.stderr)
    return ok, bad


def list_contents(z_path, log=print):
    _, _, hdr, _, files = read_archive(z_path)
    for entry in files:
        log('%-25s %-15s %10d' % (entry['name'], entry['dir'], entry['comp_size']))
    log('%d files in %d directories' % (hdr['datei_anzahl'], hdr['verzeichnisse']))
