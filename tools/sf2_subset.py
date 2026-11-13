#!/usr/bin/env python3
#
#  Copyright (C) 2026 mxvisor
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
#  the Software, and to permit persons to whom the Software is furnished to do so,
#  subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
#  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
"""A SoundFont 2 with only some of its presets: the instruments and samples they
use are kept, the rest is dropped (a General MIDI bank of 140 MB for the dozen
programs a game's music uses).

    sf2_subset.py IN.sf2 OUT.sf2 PRESET...      PRESET: bank:program or program (bank 0)

Drum kits are bank 128 (128:0 is the Standard kit).  The 24-bit extension
(sm24) is dropped: the synthesizer of the runtime (TinySoundFont) reads 16 bits.
"""
import struct
import sys

PAD = 46  # zero points after each sample, as the specification asks


def chunks(data, start, end):
    while start + 8 <= end:
        cid, size = data[start:start + 4], struct.unpack_from('<I', data, start + 4)[0]
        yield cid, start + 8, size
        start += 8 + size + (size & 1)


def records(blob, size):
    return [blob[i:i + size] for i in range(0, len(blob), size)]


def riff(cid, body):
    return cid + struct.pack('<I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def subset(data, wanted):
    lists = {}
    for cid, off, size in chunks(data, 12, len(data)):
        if cid == b'LIST':
            lists[data[off:off + 4]] = (off + 4, off + size)
    info = data[lists[b'INFO'][0] - 4 - 8:lists[b'INFO'][1]]
    smpl = None
    for cid, off, size in chunks(data, *lists[b'sdta']):
        if cid == b'smpl':
            smpl = data[off:off + size]
    if smpl is None:
        raise ValueError('no sample data (smpl)')
    pd = {cid: data[off:off + size] for cid, off, size in chunks(data, *lists[b'pdta'])}
    phdr, pbag, pmod, pgen = records(pd[b'phdr'], 38), records(pd[b'pbag'], 4), records(pd[b'pmod'], 10), records(pd[b'pgen'], 4)
    inst, ibag, imod, igen = records(pd[b'inst'], 22), records(pd[b'ibag'], 4), records(pd[b'imod'], 10), records(pd[b'igen'], 4)
    shdr = records(pd[b'shdr'], 46)

    def bags(hdr, index, bag_offset):
        first = struct.unpack_from('<H', hdr[index], bag_offset)[0]
        return first, struct.unpack_from('<H', hdr[index + 1], bag_offset)[0]

    def bag_range(bag_list, index, which):
        return (struct.unpack_from('<H', bag_list[index], which * 2)[0],
                struct.unpack_from('<H', bag_list[index + 1], which * 2)[0])

    presets = [i for i in range(len(phdr) - 1) if struct.unpack_from('<HH', phdr[i], 20) in wanted]
    found = {struct.unpack_from('<HH', phdr[i], 20) for i in presets}
    for bank_program in sorted(wanted - found):
        print('not in the bank: %d:%d' % (bank_program[1], bank_program[0]), file=sys.stderr)

    # the instruments of the presets, the samples of the instruments
    instruments = set()
    for p in presets:
        for b in range(*bags(phdr, p, 24)):
            for g in range(*bag_range(pbag, b, 0)):
                oper, amount = struct.unpack('<HH', pgen[g])
                if oper == 41:
                    instruments.add(amount)
    samples = set()
    for i in instruments:
        for b in range(*bags(inst, i, 20)):
            for g in range(*bag_range(ibag, b, 0)):
                oper, amount = struct.unpack('<HH', igen[g])
                if oper == 53:
                    samples.add(amount)
    for s in list(samples):  # the other channel of a stereo sample
        link, kind = struct.unpack_from('<HH', shdr[s], 42)
        if kind & 0x0e and link < len(shdr) - 1:
            samples.add(link)

    # samples: their points, new offsets
    sample_map, out_smpl, out_shdr = {}, bytearray(), []
    for s in sorted(samples):
        sample_map[s] = len(out_shdr)
        name, start, end, loop_start, loop_end, rest = struct.unpack('<20sIIII10s', shdr[s])
        base = len(out_smpl) // 2
        out_smpl += smpl[start * 2:end * 2] + b'\0' * (PAD * 2)
        out_shdr.append([name, base, base + end - start, base + loop_start - start, base + loop_end - start, rest])
    for rec in out_shdr:
        rate, pitch, correction, link, kind = struct.unpack('<IBbHH', rec[5])
        rec[5] = struct.pack('<IBbHH', rate, pitch, correction, sample_map.get(link, 0), kind)
    shdr_out = b''.join(struct.pack('<20sIIII10s', *rec) for rec in out_shdr) + shdr[-1]

    # instruments with their zones; the sample IDs renumbered
    inst_map, inst_out, ibag_out, imod_out, igen_out = {}, [], [], [], []
    for i in sorted(instruments):
        inst_map[i] = len(inst_out)
        inst_out.append(inst[i][:20] + struct.pack('<H', len(ibag_out)))
        for b in range(*bags(inst, i, 20)):
            ibag_out.append(struct.pack('<HH', len(igen_out), len(imod_out)))
            for g in range(*bag_range(ibag, b, 0)):
                oper, amount = struct.unpack('<HH', igen[g])
                igen_out.append(struct.pack('<HH', oper, sample_map[amount] if oper == 53 else amount))
            imod_out += [imod[m] for m in range(*bag_range(ibag, b, 1))]
    inst_out.append(inst[-1][:20] + struct.pack('<H', len(ibag_out)))
    ibag_out.append(struct.pack('<HH', len(igen_out), len(imod_out)))

    phdr_out, pbag_out, pmod_out, pgen_out = [], [], [], []
    for p in presets:
        phdr_out.append(phdr[p][:24] + struct.pack('<H', len(pbag_out)) + phdr[p][26:])
        for b in range(*bags(phdr, p, 24)):
            pbag_out.append(struct.pack('<HH', len(pgen_out), len(pmod_out)))
            for g in range(*bag_range(pbag, b, 0)):
                oper, amount = struct.unpack('<HH', pgen[g])
                pgen_out.append(struct.pack('<HH', oper, inst_map[amount] if oper == 41 else amount))
            pmod_out += [pmod[m] for m in range(*bag_range(pbag, b, 1))]
    phdr_out.append(phdr[-1][:24] + struct.pack('<H', len(pbag_out)) + phdr[-1][26:])
    pbag_out.append(struct.pack('<HH', len(pgen_out), len(pmod_out)))
    for terminal, out in ((pmod, pmod_out), (pgen, pgen_out), (imod, imod_out), (igen, igen_out)):
        out.append(terminal[-1])

    pdta = b'pdta' + b''.join(riff(cid, b''.join(body)) for cid, body in (
        (b'phdr', phdr_out), (b'pbag', pbag_out), (b'pmod', pmod_out), (b'pgen', pgen_out),
        (b'inst', inst_out), (b'ibag', ibag_out), (b'imod', imod_out), (b'igen', igen_out),
        (b'shdr', [shdr_out])))
    body = b'sfbk' + info + riff(b'LIST', b'sdta' + riff(b'smpl', bytes(out_smpl))) + riff(b'LIST', pdta)
    return riff(b'RIFF', body), len(presets), len(instruments), len(samples)


def main(argv):
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    wanted = set()
    for spec in argv[3:]:
        bank, _, program = spec.rpartition(':')
        wanted.add((int(program), int(bank) if bank else 0))
    with open(argv[1], 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'sfbk':
        print('%s: not a SoundFont 2' % argv[1], file=sys.stderr)
        return 1
    out, presets, instruments, samples = subset(data, wanted)
    with open(argv[2], 'wb') as f:
        f.write(out)
    print('%s: %d presets, %d instruments, %d samples, %d bytes' % (argv[2], presets, instruments, samples, len(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
