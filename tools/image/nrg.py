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

"""Nero Burning ROM (.nrg) images.

A mixed-mode disc is laid out as

    150 sectors of lead-in (zeros)
    the data track, 2048 bytes per sector
    the audio tracks, 2352 bytes per sector: raw 16-bit stereo PCM at
        44.1 kHz, with no sync pattern and no header
    the Nero footer

7-Zip reads the ISO9660 at the front of such a file and calls the rest "data
after the end of the archive". A footer chunk is a 4-byte tag and a 4-byte
big-endian length, and the last 16 bytes of the file are 4 zero bytes, NER5,
4 more zero bytes and the footer's offset. Its chunks are CUEX (the track
table), DAOX (file offsets, meaningless in this layout), SINF, MTYP and END!.

The track table is CUEX: 8-byte entries of [marker, track, index, 0, lba], with
marker 0x41 for the data track, 0x21 for the audio ones and 0x01 for the
lead-out, index 0 a track's lead-in and index 1 its start. The audio has no
2352-byte sector headers, and the rip left out the blank sectors that ended the
data track, so the audio's place in the file is worked out from the lead-out:
the lead-out is the last sector of the last audio track, and the file's audio
runs right up to the footer.

The bin written is 2352 bytes per sector throughout, so LBA n sits at n * 2352:
the data track's ISO9660 is re-wrapped into Mode 1 sectors, the blank sectors
the rip dropped are put back so that the audio's LBAs hold, and the audio is
copied over byte for byte.
"""
import os
import struct

from cdimage import DATA, RAW, fail, msf, write_cue

SYNC = b'\x00' + b'\xff' * 10 + b'\x00'
DATA_TRACK, AUDIO_TRACK, LEAD_OUT = 0x41, 0x21, 0x01


def detect(path):
    with open(path, 'rb') as image:
        image.seek(-12, 2)
        return image.read(4) == b'NER5' or path.lower().endswith('.nrg')


def read_footer(image, size):
    """(first byte of the footer, {tag: body}) from the offset in its tail."""
    image.seek(size - 16)
    tail = image.read(16)
    if b'NER5' not in tail:
        fail('no NER5 footer at the end of the image')
    at = struct.unpack('>I', tail[-4:])[0]
    image.seek(at)
    footer = image.read(size - at)
    chunks = {}
    for tag in (b'CUEX', b'DAOX', b'SINF', b'MTYP'):
        head = footer.find(tag)
        if head >= 0 and head + 8 <= len(footer):
            length = struct.unpack('>I', footer[head + 4:head + 8])[0]
            chunks[tag] = footer[head + 8:head + 8 + length]
    return at, chunks


def parse_cuex(body):
    """(data track, [audio tracks], lead-out) from a CUEX or CUES body."""
    for first in range(8):
        entries = [body[first + 8 * n:first + 8 * n + 8] for n in range((len(body) - first) // 8)]
        if entries and all(e[0] in (DATA_TRACK, AUDIO_TRACK, LEAD_OUT) for e in entries):
            break
    else:
        fail('the cue table has no entry list')
    marks, numbers, lead_out = {}, set(), None
    for entry in entries:
        marker, track, index = entry[0], entry[1], entry[2]
        at = struct.unpack('>i', entry[4:8])[0]
        if marker == LEAD_OUT:
            lead_out = at
        else:
            if marker == AUDIO_TRACK:
                numbers.add(track)
            marks.setdefault(track, {})[index] = at
    if not numbers or lead_out is None:
        fail('the cue table has no audio tracks or no lead-out')
    return marks, sorted(numbers), lead_out


def find_pvd(image, size):
    """Offset of the ISO9660 image, offset of its PVD, and the volume id."""
    image.seek(0)
    window, read = b'', 0
    while read < min(size, 1 << 27):
        window += image.read(1 << 22)
        at = window.find(b'\x01CD001\x01\x00')
        while at >= 0:
            if at >= 16 * DATA and at % DATA == 0 and window[at - 1] == 0:
                image.seek(at)
                pvd = image.read(DATA)
                if pvd[0] == 1:
                    return at - 16 * DATA, at, pvd[40:72].decode('latin1').strip()
            at = window.find(b'\x01CD001\x01\x00', at + 1)
        window = window[-DATA:]
        read += 1 << 22
    fail('no ISO9660 primary volume descriptor in the image')


def iso_extent(image, iso_at, lba, size, seen, sectors=[0], files=[0], folders=[0]):
    """The last sector the directory tree at (lba, size) uses, and its count."""
    if (lba, size) in seen:
        return
    seen.add((lba, size))
    image.seek(iso_at + lba * DATA)
    data = image.read(size)
    at = 0
    while at + 34 <= len(data):
        length = data[at]
        if length == 0:
            at = (at // DATA + 1) * DATA
            continue
        record = data[at:at + length]
        name, extent = record[33:33 + record[32]], struct.unpack_from('<I', record, 2)[0]
        used = struct.unpack_from('<I', record, 10)[0]
        at += length
        if name in (b'\x00', b'\x01'):
            continue
        sectors[0] = max(sectors[0], extent + (used + DATA - 1) // DATA)
        if record[25] & 2:
            folders[0] += 1
            iso_extent(image, iso_at, extent, used, seen, sectors, files, folders)
        else:
            files[0] += 1
    return sectors[0]


def mode1(data, lba):
    """One data sector for the bin: sync, header, data, and no ECC."""
    minutes, rest = divmod(lba, 75 * 60)
    seconds, frame = divmod(rest, 75)
    return SYNC + bytes((minutes, seconds, frame, 1)) + data + b'\x00' * 288


def copy(source, target, offset, length, block=1 << 22):
    source.seek(offset)
    while length > 0:
        chunk = source.read(min(block, length))
        if not chunk:
            fail('%s ends %d bytes early' % (source.name, length))
        target.write(chunk)
        length -= len(chunk)


def to_cue(path, out_dir, name, audio=True, log=print):
    """Write out_dir/name.cue and out_dir/name.bin. Returns the cue's path."""
    size = os.path.getsize(path)
    with open(path, 'rb') as image:
        footer_at, chunks = read_footer(image, size)
        log('footer at %d: %s' % (footer_at, ' '.join(tag.decode('latin1') for tag in sorted(chunks))))
        table = next((chunks[tag] for tag in (b'CUEX', b'CUES') if tag in chunks), None)
        if table is None:
            fail('the footer has no cue table')
        marks, numbers, lead_out = parse_cuex(table)
        iso_at, pvd_at, volume = find_pvd(image, size)
        image.seek(pvd_at)
        pvd = image.read(DATA)
        iso_sectors = struct.unpack_from('<I', pvd, 80)[0]
        used, files, folders = [0], [0], [0]
        iso_extent(image, iso_at, struct.unpack_from('<I', pvd, 158)[0],
                   struct.unpack_from('<I', pvd, 166)[0], set(), used, files, folders)
        log('ISO9660 %r: %d sectors of 2048 bytes, %d files, %d folders, last file at sector %d'
            % (volume, iso_sectors, files[0], folders[0], used[0] - 1))
        if not used[0] <= iso_sectors:
            fail('a file ends at sector %d, past the volume space size %d' % (used[0], iso_sectors))

    audio_at = iso_at + iso_sectors * DATA
    audio_sectors = (footer_at - audio_at) // RAW
    audio_lba = lead_out - audio_sectors
    log('audio: %d sectors of 2352 bytes at %d, LBA %d..%d' % (audio_sectors, audio_at, audio_lba, lead_out - 1))
    if not iso_sectors <= audio_lba <= marks[numbers[0]].get(0, marks[numbers[0]][1]):
        fail('the audio starts at LBA %d, outside the data track and track %d' % (audio_lba, numbers[0]))
    log('data track: %d sectors, of which %d blank ones the rip dropped' % (audio_lba, audio_lba - iso_sectors))
    for position, track in enumerate(numbers):
        first = marks[track][1]
        end = (marks[numbers[position + 1]].get(0, marks[numbers[position + 1]][1])
               if position + 1 < len(numbers) else lead_out)
        log('  %2d audio  %s  %6.1f s' % (track, msf(first), (end - first) / 75.0))
    log('  lead-out  %s' % msf(lead_out))

    bin_name = name + '.bin'
    tracks = [{'file': bin_name, 'number': 1, 'mode': 'MODE1/2352', 'index1': 0}]
    if audio:
        for track in numbers:
            tracks.append({'file': bin_name, 'number': track, 'mode': 'AUDIO',
                           'index0': marks[track].get(0, marks[track][1]), 'index1': marks[track][1]})
    cue_path = os.path.join(out_dir, name + '.cue')
    write_cue(cue_path, tracks)

    bin_path = os.path.join(out_dir, bin_name)
    with open(path, 'rb') as image, open(bin_path, 'wb') as disk:
        for lba in range(0, iso_sectors, 64):
            image.seek(iso_at + lba * DATA)
            for sector in range(min(64, iso_sectors - lba)):
                disk.write(mode1(image.read(DATA), lba + sector))
        if audio_lba > iso_sectors:
            disk.write(b'\x00' * ((audio_lba - iso_sectors) * RAW))
        if audio:
            copy(image, disk, audio_at, footer_at - audio_at)
    written = os.path.getsize(bin_path)
    log('%s: %d sectors, %.1f MB' % (bin_name, written // RAW, written / 1e6))
    return cue_path
