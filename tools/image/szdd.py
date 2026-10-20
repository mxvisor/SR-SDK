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

"""SZDD: the single-file LZSS format of the MS-DOS COMPRESS.EXE and
EXPAND.EXE, which is how Windows installers ship their compressed files
(SETUP.EX_ for SETUP.EXE, WING32.DL_ for WING32.DLL). The format is
documented at cabextract.org.uk/libmspack/doc/szdd_kwaj_format.html and
implemented here from that: a 14-byte header, then a 4096-byte window full
of spaces, literals and 3..18-byte matches, flag bits least significant
first.

Names: COMPRESS.EXE replaces the last character of the name with an
underscore and stores it in the header. Installers of that era often leave
the header byte at 0 ("unknown") and keep the real name elsewhere, so a
name ending in '_' whose completion is a well-known DOS extension is
completed from a table (the DISC_EXTENSIONS map), and anything else keeps
its name until the caller says what it is (a `name=out` pair, see
image.py expand)."""
import os
import struct

from cdimage import fail

SIGNATURE = b'SZDD\x88\xf0\x27\x33'
QBASIC_SIGNATURE = b'SZ\x20\x88\xf0\x27\x33\xd1'

# The name ends in '_'; what the '_' stands for, by the extension before it.
# Only unambiguous completions: IN_ could be INI or INF, so it is not here.
DISC_EXTENSIONS = {
    '38': '6',   # 386    386
    'ba': 't',   # BAT    BAT
    'co': 'm',   # COM    COM
    'cu': 'e',   # CUE    CUE
    'dl': 'l',   # DL_    DLL
    'dr': 'v',   # DR_    DRV
    'ex': 'e',   # EX_    EXE
    'fo': 'n',   # FON    FON
    'gi': 'f',   # GIF    GIF
    'gr': 'p',   # GRP    GRP
    'hl': 'p',   # HL_    HLP
    'ln': 'k',   # LNK    LNK
    'nl': 's',   # NL_    NLS
    'sy': 's',   # SYS    SYS
    'tx': 't',   # TX_    TXT
    'wn': 'd',   # WN_    WND
}


def is_szdd(path):
    """Whether the file at path starts with an SZDD signature."""
    with open(path, 'rb') as stream:
        return stream.read(8) in (SIGNATURE, QBASIC_SIGNATURE)


def header(data):
    """The header of an SZDD file: (unpacked length, data offset, window
    start, missing_char), or None when data is not SZDD."""
    if data[:8] == SIGNATURE:
        if len(data) < 14:
            fail('SZDD: truncated header')
        if data[8] != 0x41:
            fail('SZDD: unknown compression mode 0x%02X (only "A")' % data[8])
        return struct.unpack_from('<I', data, 10)[0], 14, 4096 - 16, data[9]
    if data[:8] == QBASIC_SIGNATURE:
        if len(data) < 12:
            fail('SZDD: truncated header')
        return struct.unpack_from('<I', data, 8)[0], 12, 4096 - 18, 0
    return None


def decompress(data):
    """The unpacked bytes of an SZDD file."""
    parsed = header(data)
    if parsed is None:
        fail('SZDD: not an SZDD file')
    length, offset, pos, _ = parsed
    window = bytearray(b' ' * 4096)
    out = bytearray()
    size = len(data)
    while len(out) < length:
        if offset >= size:
            fail('SZDD: the data ends at %d of %d bytes' % (len(out), length))
        control = data[offset]
        offset += 1
        for bit in (1, 2, 4, 8, 16, 32, 64, 128):
            if len(out) >= length:
                break
            if control & bit:
                if offset >= size:
                    fail('SZDD: the data ends at %d of %d bytes' % (len(out), length))
                value = data[offset]
                offset += 1
                window[pos] = value
                out.append(value)
                pos = (pos + 1) & 4095
            else:
                if offset + 1 >= size:
                    fail('SZDD: the data ends at %d of %d bytes' % (len(out), length))
                low, high = data[offset], data[offset + 1]
                offset += 2
                match = low | ((high & 0xF0) << 4)
                for _ in range((high & 0x0F) + 3):
                    value = window[match]
                    match = (match + 1) & 4095
                    window[pos] = value
                    out.append(value)
                    pos = (pos + 1) & 4095
                    if len(out) >= length:
                        break
    return bytes(out)


def original_name(name, missing_char):
    """The name name (of an SZDD file) had before it was compressed."""
    if not name.endswith('_'):
        return name
    if missing_char:
        return name[:-1] + bytes([missing_char]).decode('cp437', errors='replace')
    base, dot, extension = name[:-1].rpartition('.')
    if dot and extension.lower() in DISC_EXTENSIONS:
        added = DISC_EXTENSIONS[extension.lower()]
        if extension == extension.upper():
            added = added.upper()
        return base + dot + extension + added
    return name


def expand_file(source, target=None):
    """Unpack the SZDD file at source; target defaults to the original name
    beside it. Returns (source, the name written)."""
    with open(source, 'rb') as stream:
        data = stream.read()
    parsed = header(data)
    if parsed is None:
        fail('%s: not an SZDD file' % source)
    if target is None:
        target = os.path.join(os.path.dirname(source),
                              original_name(os.path.basename(source), parsed[3]))
    if os.path.abspath(target) == os.path.abspath(source):
        fail('%s: the original name is unknown; say it as %s=<the name>'
             % (source, os.path.basename(source)))
    directory = os.path.dirname(target)
    if directory:
        os.makedirs(directory, exist_ok=True)
    unpacked = decompress(data)
    if len(unpacked) != parsed[0]:
        fail('%s: unpacked to %d bytes, the header says %d' % (source, len(unpacked), parsed[0]))
    with open(target, 'wb') as stream:
        stream.write(unpacked)
    return source, target
