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

"""Raw BINARY images: a .bin of 2352-byte sectors or an .iso of 2048-byte ones,
with or without the cue sheet that came with it."""
import os
import shutil

from cdimage import DATA, RAW, fail, link_next_to, write_cue


def sniff_mode(path):
    """MODE1/2352, MODE2/2352 or MODE1/2048 for a raw data image: where the
    ISO9660 volume descriptor at sector 16 is."""
    with open(path, 'rb') as stream:
        for mode, sector, header in (('MODE1/2048', DATA, 0), ('MODE1/2352', RAW, 16), ('MODE2/2352', RAW, 24)):
            stream.seek(16 * sector + header)
            if stream.read(6)[1:6] == b'CD001':
                return mode
    fail('%s: no ISO9660 volume descriptor in sector 16 (2048- or 2352-byte)' % path)


def raw_to_cue(path, out_dir, name, log=print):
    """A cue sheet with one data track for a .bin or .iso without one; the
    cue that comes with the image, if there is one, is used as it is."""
    given = os.path.splitext(path)[0] + '.cue'
    if os.path.isfile(given) and os.path.getsize(given) > 0:
        text = open(given, encoding='latin1').read()
        if 'INDEX' in text.upper() and os.path.basename(path).lower() in text.lower():
            if os.path.realpath(os.path.dirname(given)) == os.path.realpath(out_dir):
                log('using %s' % given)
                return given
            # the cue names the image by its base name: both go to out_dir
            link_next_to(path, out_dir)
            cue = os.path.join(out_dir, name + '.cue')
            shutil.copyfile(given, cue)
            log('%s: the cue sheet of %s' % (cue, path))
            return cue
    mode = sniff_mode(path)
    image_name = link_next_to(path, out_dir)
    cue = os.path.join(out_dir, name + '.cue')
    write_cue(cue, [{'file': image_name, 'number': 1, 'mode': mode, 'index1': 0}])
    log('%s: one %s data track in %s' % (cue, mode, image_name))
    return cue
