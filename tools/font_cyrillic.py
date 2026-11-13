#!/usr/bin/env python3
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

"""Makes runtime/core/Game_font_cyr.h: the Cyrillic letters (U+0410-U+044F, Ё, ё)
of the port's text font, the OCR-B bitmap of VKfont2.h (38x56, baseline under row
42, capitals 38 rows high, small letters 29).

OCR-B has no Cyrillic.  The letters that look like Latin ones (А В Е К М Н О Р С Т
Х а е о р с у х) take the OCR-B glyph (the table names the ASCII character); the
others are drawn from a monospaced font of a near stroke, Source Code Pro Regular
(SIL OFL 1.1, NOTICE.md), fitted to the cell: capitals to the height of H, small
letters to the height of x, at most as wide as OCR-B's widest letter, letters with
an accent above the cell lowered.

    tools/font_cyrillic.py SourceCodePro-Regular.otf runtime/core/Game_font_cyr.h

Needs Pillow.  The output is committed; run it again only to change the letters.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H, PITCH = 38, 56, 5
BASE = 43             # the row under the baseline
CAP, XH = 38, 29      # OCR-B's H (rows 5..42) and x (rows 15..42)
MAXW, CX = 27, 19     # OCR-B's widest letter (m) and the centre of its glyphs
SS = 8                # supersampling

LATIN = dict(zip('АВЕКМНОРСТХаеорсух', 'ABEKMHOPCTXaeopcyx'))
LETTERS = [chr(c) for c in range(0x410, 0x450)] + ['Ё', 'ё']


def font_size(path, ch, height):
    font = ImageFont.truetype(path, 1000)
    return height * 1000 / -font.getbbox(ch, anchor='ls')[1]


def glyph(path, ch):
    size = font_size(path, 'x' if ch.islower() else 'H', XH if ch.islower() else CAP)
    font = ImageFont.truetype(path, int(round(size * SS)))
    canvas = Image.new('L', (int(size * SS * 3), int(size * SS * 3)), 0)
    bx, by = canvas.width // 3, canvas.height * 2 // 3
    ImageDraw.Draw(canvas).text((bx, by), ch, font=font, fill=255, anchor='ls')
    box = canvas.getbbox()
    ink = canvas.crop(box)
    top, bottom = (box[1] - by) / SS, (box[3] - by) / SS
    w, h = ink.width / SS, ink.height / SS

    sx = min(1.0, MAXW / w)
    sy = 1.0
    if BASE + top < 0:
        sy = BASE / -top
    if BASE + bottom > H:
        sy = min(sy, (H - BASE) / bottom)
    tw, th = w * sx, h * sy

    cell = Image.new('L', (W * SS, H * SS), 0)
    cell.paste(ink.resize((max(1, int(round(tw * SS))), max(1, int(round(th * SS)))), Image.Resampling.LANCZOS),
               (int(round((CX - tw / 2) * SS)), int(round((BASE + top * sy) * SS))))
    cell = cell.resize((W, H), Image.Resampling.BOX)
    rows = []
    for y in range(H):
        row = bytearray(PITCH)
        for x in range(W):
            if cell.getpixel((x, y)) >= 128:
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    return rows


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: font_cyrillic.py FONT OUTPUT.h')
    path, output = sys.argv[1], sys.argv[2]

    latin, index, data = [], [], []
    for ch in LETTERS:
        latin.append(LATIN.get(ch))
        if ch in LATIN:
            index.append(-1)
            continue
        index.append(len(data))
        data.append((ch, glyph(path, ch)))

    out = []
    out.append('/* The Cyrillic letters of the port\'s text font (Game_ui.c): U+0410-U+044F,')
    out.append(' * then U+0401 and U+0451.  Made by tools/font_cyrillic.py from %s,' % os.path.basename(path))
    out.append(' * fitted to the 38x56 cell of VKfont2.h.')
    out.append(' *')
    out.append(' * The glyphs below are a work derived from Source Code Pro,')
    out.append(' * Copyright 2023 Adobe (http://www.adobe.com/), with Reserved Font Name')
    out.append(' * \'Source\'; they are licensed under the SIL Open Font License, Version 1.1,')
    out.append(' * whose text is in NOTICE.md. */')
    out.append('')
    out.append('#define FONT_CYR_COUNT %d' % len(LETTERS))
    out.append('')
    out.append('/* the ASCII character whose OCR-B glyph the letter takes, or 0 */')
    out.append('static const char font_cyr_latin[FONT_CYR_COUNT] = {')
    for i in range(0, len(LETTERS), 16):
        out.append('    ' + ' '.join("'%s'," % c if c else '0,' for c in latin[i:i + 16]))
    out.append('};')
    out.append('')
    out.append('/* the letter\'s glyph in font_cyr_data, or -1 */')
    out.append('static const int8_t font_cyr_index[FONT_CYR_COUNT] = {')
    for i in range(0, len(LETTERS), 16):
        out.append('    ' + ' '.join('%d,' % n for n in index[i:i + 16]))
    out.append('};')
    out.append('')
    out.append('static const uint8_t font_cyr_data[%d * %d * %d] = {' % (PITCH, H, len(data)))
    for ch, rows in data:
        out.append('    /* U+%04X */' % ord(ch))
        for row in rows:
            bits = ''.join('#' if row[x >> 3] & (0x80 >> (x & 7)) else ' ' for x in range(W))
            out.append('    ' + ' '.join('0x%02X,' % b for b in row) + ' /* %s */' % bits)
    out.append('};')
    with open(output, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
