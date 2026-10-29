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

"""Wise setup programs (Wise 7 and later), without running them.

A Wise installer is a PE file with the setup data in its overlay: a header, the
installer's own files (a .dib, WiseScript.bin, DLLs), then the files the
installer copies, each a raw DEFLATE stream followed by the CRC32 of the
inflated data.  WiseScript.bin has the file headers: the DEFLATE offsets (from
the start of the files), the inflated size, the CRC32 and the destination,
which begins with an install variable such as %MAINDIR%.

Only %MAINDIR% (the game directory) is extracted, at the path under it,
including the empty files the installer makes (the game may open them); the
installer's own files and the ones it puts in %TEMP% or at %UNINSTALL_PATH% are
not part of the game directory.  The header layout is the one of Wise 7-9 as
documented by REWise; tested with a Wise Game Installer by LiS.
"""
import os
import re
import struct
import zlib


def _inflate(data, size=None):
    out = zlib.decompressobj(-15).decompress(data)
    if size is not None and len(out) != size:
        raise ValueError('inflated %d bytes, expected %d' % (len(out), size))
    return out


def is_wise(path):
    """True when PATH is a PE file (a Wise setup program is a PE file; .z
    archives of InstallShield are not)."""
    with open(path, 'rb') as stream:
        return stream.read(2) in (b'MZ', b'ZM')


def read(path):
    """The bytes of the overlay header, the internal components and the
    WiseScript, for a Wise setup program."""
    with open(path, 'rb') as stream:
        data = stream.read()
    if data[:2] not in (b'MZ', b'ZM'):
        raise ValueError('%s: not a PE file' % path)
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    table = pe + 24 + optional
    overlay = max(struct.unpack_from('<I', data, table + i * 40 + 20)[0] +
                  struct.unpack_from('<I', data, table + i * 40 + 16)[0]
                  for i in range(sections))

    header = _overlay_header(data, overlay)
    if header['eof'] != len(data) or not 0 < header['inflated_script'] < len(data) * 20:
        raise ValueError('%s: no Wise overlay' % path)

    start = header['end']
    script_offset = start + header['deflated_dib']
    script = _inflate(data[script_offset:script_offset + header['deflated_script'] - 4],
                      header['inflated_script'])
    return data, header, start, script


def _overlay_header(data, offset):
    def u8():
        nonlocal offset
        value = data[offset]
        offset += 1
        return value

    def u16():
        nonlocal offset
        value = struct.unpack_from('<H', data, offset)[0]
        offset += 2
        return value

    def u32():
        nonlocal offset
        value = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        return value

    length = u8()
    if length:
        offset += length + 4
    flags = u32()
    offset += 20
    inflated_script = u32()
    deflated_script = u32()
    deflated_wisedll = u32()
    ctl3d = u16(); u16()
    wisedll2 = u16(); u16()
    ocxreg = u16(); u16()
    progress = u32()
    shttp = u16(); u16()
    ctl3dnt = u16(); u16()
    msrstrt = u16(); u16()
    rebootnt = u16(); u16()
    deflated_install = u32()
    u32()
    eof = u32()
    deflated_dib = u32()
    u32()
    offset += 2
    texts = u8()
    offset += texts
    return dict(flags=flags, inflated_script=inflated_script,
                deflated_script=deflated_script, deflated_wisedll=deflated_wisedll,
                ctl3d=ctl3d, wisedll2=wisedll2, ocxreg=ocxreg, progress=progress,
                shttp=shttp, ctl3dnt=ctl3dnt, msrstrt=msrstrt, rebootnt=rebootnt,
                deflated_install=deflated_install, eof=eof,
                deflated_dib=deflated_dib, end=offset)


def _files_start(start, header):
    """Where the installer's copied files begin: after its own files, whose
    stored lengths (each with the CRC32 after it) the header gives."""
    total = (header['deflated_dib'] + header['deflated_script'] +
             header['deflated_wisedll'] + header['ctl3d'] + header['progress'] +
             header['shttp'] + header['ctl3dnt'] + header['msrstrt'] +
             header['rebootnt'] + header['deflated_install'])
    if header['flags'] & 0x8000:     # WISE0002.DLL
        total += header['wisedll2']
    if header['flags'] & 0x20000:    # OCXREG32.EXE
        total += header['ocxreg']
    return start + total


def files(path):
    """(name, offset_of_the_deflated_data, offset_of_its_crc, inflated_size,
    crc32) for every %MAINDIR% file of the setup program."""
    data, header, start, script = read(path)
    base = _files_start(start, header)
    for offset in range(len(script) - 46):
        if struct.unpack_from('<H', script, offset)[0] not in (0x80, 0x81, 0, 0x98, 0xa1):
            continue
        deflate, end = struct.unpack_from('<II', script, offset + 2)
        if not 0 < deflate < end or end - deflate < 5:
            continue
        size, = struct.unpack_from('<I', script, offset + 14)
        crc, = struct.unpack_from('<I', script, offset + 38)
        if not 0 < size < 1 << 28 or not crc:
            continue
        name_end = script.find(b'\0', offset + 42, offset + 42 + 260)
        if name_end < 0:
            continue
        try:
            name = script[offset + 42:name_end].decode('cp1252')
        except UnicodeDecodeError:
            continue
        if not re.match(r'^%[A-Za-z0-9_]+%\\', name) or any(ord(c) < 32 for c in name):
            continue
        if not name.upper().startswith('%MAINDIR%\\'):
            continue
        yield name, base + deflate, base + end - 4, size, crc


def paths(script):
    """The %MAINDIR% paths of the script that have no data header of their own:
    the installer makes them empty (the game may still open them)."""
    for match in re.finditer(rb'%MAINDIR%\\', script):
        offset = match.start()
        end = script.find(b'\0', offset, offset + 260)
        if end < 0 or script[end + 1:end + 4] != b'\0' * 3:
            continue
        try:
            name = script[offset:end].decode('cp1252')
        except UnicodeDecodeError:
            continue
        if any(ord(c) < 32 for c in name) or ' ' in name:
            continue
        yield name


def extract(path, out):
    """Writes the %MAINDIR% files of a Wise setup program to OUT, at their
    paths under it. Returns (written, failed)."""
    data = open(path, 'rb').read()
    written = failed = 0
    written_paths = set()
    for name, deflate, crc_offset, size, crc in files(path):
        if crc_offset + 4 > len(data):
            continue
        try:
            content = _inflate(data[deflate:crc_offset], size)
        except Exception:
            continue
        if (struct.unpack_from('<I', data, crc_offset)[0] != crc or
                zlib.crc32(content) & 0xffffffff != crc):
            failed += 1
            continue
        rel = name.split('\\', 1)[1].replace('\\', '/')
        target = os.path.join(out, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as stream:
            stream.write(content)
        written_paths.add(os.path.normcase(rel))
        written += 1
    script = read(path)[3]
    for name in paths(script):
        rel = name.split('\\', 1)[1].replace('\\', '/')
        if os.path.normcase(rel) in written_paths:
            continue
        target = os.path.join(out, rel)
        if not os.path.exists(target):
            os.makedirs(os.path.dirname(target), exist_ok=True)
            open(target, 'wb').close()
            written += 1
    return written, failed


def list_contents(path):
    for name, _, _, size, _ in files(path):
        print('%-12d %s' % (size, name))
