#!/usr/bin/env python3
# Copyright (C) 2026 mxvisor
#
# Reconstruct PE base relocations for a stripped executable by
# disassembling code sections with Capstone (with SKIPDATA for
# recovery from decode failures), then scanning each instruction's
# bytes for ImageBase-relative pointers that match values appearing
# in the disassembled instruction string.
#
# This avoids false positives from cross-instruction 4-byte reads
# (e.g., a value that spans a displacement's MSB and an immediate's
# bytes which won't appear in the disassembly string).
#
# For undecodable regions (SKIPDATA), fall back to raw scanning.
#
# Initialized data sections (.data, .rdata, ...) are scanned for dwords: a
# 4-aligned dword that points into the image is taken as a pointer (tables of
# pointers, vtables, jump tables).  A constant that looks like an address is
# a false positive.  A pointer into code must hit the start of an instruction
# of the linear disassembly.  --no-data leaves the data sections out.
#
# Output format matches SRW's relocations.csv (see SRW_loader.c LoadRelocations):
#   0x<FixupAddress>,0x<TargetAddress>,[i]
#
# `jmp/call dword [IAT slot]` (FF 25 / FF 15) is also looked for in the raw bytes: a linear
# sweep that lost its way (data between the import thunks) misses them.
#
# --strict-code: an immediate in code (not a memory operand: tables of bytes and of
# addresses lie in code) whose value points into the middle of an instruction of the linear
# disassembly is not taken for an address (a pushed flag set that falls in the image),
# unless it is 4-aligned behind a padding byte (nop, int3): there a function begins that the
# sweep, run off course by data before the padding, went past.  Without it every in-image constant seen in the disassembly is a relocation.
#
# A jump table inside a code section (jmp [reg*4 + table]) is not instructions:
# its entries are added while they point at the start of an instruction, or near the
# table (TABLE_REACH: the sweep that read the table as instructions misses the code
# after it). A table whose first entry fails and second fits is indexed from 1.
#
# A table of records whose pointer field is not 4-aligned (a packed structure) is
# found by its stride: --unaligned-runs takes the unaligned in-image dwords that
# lie at least RUN_MIN times in a row at one distance (8..RUN_STRIDE_MAX bytes), none of
# them 4-aligned and in data (a float reads as an address in code), as pointers.  Without it only the 4-aligned dwords of a data section count.
#
# --code NAME: the data section NAME holds machine code as well (code_sections.sci of
# SRW): it is disassembled like a code section, not scanned for dwords. Repeatable.
#
# --sci DIR also writes what SRW needs besides the csv when the code holds tables, into DIR:
#   fixup_interpret_as_code.sci          targets in code of rows in code that SRW takes for
#       code only when told (SR_basic.c): an immediate (push offset Func), a jump table entry,
#       a pointer in undecoded bytes -- when it is the start of an instruction and what
#       follows reads as code (CODE_PROBE instructions up to a jump, none of them 00 00)
#   fixup_do_not_interpret_as_code.sci   data in code: tables read through a memory operand
#       (mov dl, [edi+tbl]; jmp [reg*4+tbl]) and immediates that fail the probe (a structure
#       of a static library, push offset c_dfDIKeyboard): SRW must not decode them
#   displaced_labels.sci                 the base of a table indexed from 1, which lies inside
#       the instruction before the table: SRW names it by the label at the end of that
#       instruction minus the distance (loc_X,D: loc_X is loc_<X+D>-D)
#
# Usage: pe_relocations.py [--no-data] [--strict-code] [--unaligned-runs] [--code NAME]... [--sci DIR] <pe.exe> [out.csv]

from __future__ import annotations

import os
import struct
import sys

import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OPT_SYNTAX_INTEL


def pe_sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    imgbase = struct.unpack_from("<I", data, opt + 28)[0]
    imagesize = struct.unpack_from("<I", data, opt + 56)[0]
    soff = pe + 24 + optsize
    secs = []
    for _ in range(nsec):
        name = data[soff : soff + 8].split(b"\0")[0].decode("latin1", "replace")
        vs, va, rsz, rp = struct.unpack_from("<IIII", data, soff + 8)
        chars = struct.unpack_from("<I", data, soff + 36)[0]
        secs.append((name, va, vs, rsz, rp, chars))
        soff += 40
    return imgbase, imagesize, secs


def build_section_ranges(imgbase, secs):
    ranges = []
    for name, va, vs, rsz, rp, chars in secs:
        if rsz == 0:
            continue
        if name in (".rsrc", ".reloc"):
            continue
        lo = imgbase + va
        hi = lo + max(vs, rsz)
        ranges.append((lo, hi))
    return ranges


def is_in_image(val, imgbase, imagesize, sec_ranges):
    if val == 0:
        return False
    if val < imgbase or val >= imgbase + imagesize:
        return False
    return any(lo <= val < hi for lo, hi in sec_ranges)


def add_relocation(val, src, imgbase, seen, rows):
    if src in seen:
        return
    seen.add(src)
    if val == imgbase:
        rows.append(f"0x{src:x},0x{val:x},i")
    else:
        rows.append(f"0x{src:x},0x{val:x},")


BITWISE = ("test", "and", "or", "xor")

# how far from a jump table in code its targets may lie without being seen as
# instructions by the linear sweep
TABLE_REACH = 0x400

# how far apart two neighbouring pointers of a table that reads as text may be
PTR_NEAR = 0x18000
# how many instructions after an immediate's target must read as code
CODE_PROBE = 8
# a run of unaligned pointers: at least RUN_MIN records, RUN_STRIDE_MAX bytes apart at most
RUN_MIN = 4
RUN_STRIDE_MAX = 0x100


def find_relocations_in_sec(
    data: bytes, imgbase: int, imagesize: int,
    sec_va: int, sec_rsz: int, sec_rp: int,
    sec_ranges: list, secs: list, seen: set, rows: list, starts: set, tables: set,
    immediates: set, memops: set, sizes: dict, raws: set,
) -> None:
    raw = data[sec_rp : sec_rp + sec_rsz]
    if len(raw) == 0:
        return

    code_base = imgbase + sec_va
    limit = min(len(raw), sec_rsz)

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.syntax = CS_OPT_SYNTAX_INTEL
    md.skipdata = True
    md.skipdata_setup = (".byte", 0, 0)

    covered = bytearray(limit)

    for insn in md.disasm(raw, code_base, limit):
        start = insn.address - code_base
        insn_len = insn.size
        end = start + insn_len
        for i in range(start, min(end, limit)):
            covered[i] = 1

        # Skip SKIPDATA instructions
        if insn.id == 0:
            continue
        starts.add(insn.address)
        sizes[insn.address] = insn.size
        for m in re.finditer(r"\*4 \+ (0x[0-9a-f]+)\]", f"{insn.mnemonic} {insn.op_str}"):
            tables.add(int(m.group(1), 16))

        disasm_str = f"{insn.mnemonic} {insn.op_str}"

        # Scan byte-by-byte within this instruction for 4-byte values
        # that are valid image addresses AND appear in the disassembly string
        for off in range(start, end - 3):
            val = struct.unpack_from("<I", raw, off)[0]
            if val == 0:
                continue
            if not is_in_image(val, imgbase, imagesize, sec_ranges):
                continue

            val_str = f"0x{val:x}"
            if val_str not in disasm_str:
                continue
            # the immediate of a bit test or a mask is a set of flags, not an address
            if insn.mnemonic in BITWISE and insn.op_str.endswith(", " + val_str):
                continue

            src = code_base + off
            if not re.search(r"\[[^\]]*" + val_str + r"\b", disasm_str):
                immediates.add(src)
            else:
                memops.add(src)
            add_relocation(val, src, imgbase, seen, rows)

    # indirect jumps/calls through the import table that the sweep went past
    idata = [(imgbase + va2, imgbase + va2 + max(vs2, rsz2))
             for name2, va2, vs2, rsz2, _rp2, _c2 in secs if name2 == ".idata"]
    for off in range(0, limit - 5):
        if raw[off] == 0xFF and raw[off + 1] in (0x25, 0x15):
            val = struct.unpack_from("<I", raw, off + 2)[0]
            if any(lo <= val < hi for lo, hi in idata):
                add_relocation(val, code_base + off + 2, imgbase, seen, rows)

    # Scan undecoded regions (SKIPDATA / gaps)
    off = 0
    while off < limit - 3:
        if covered[off]:
            off += 1
            continue

        val = struct.unpack_from("<I", raw, off)[0]
        if val == 0:
            off += 1
            continue
        if not is_in_image(val, imgbase, imagesize, sec_ranges):
            off += 1
            continue

        # Consistency check: verify that the 4-byte value at src
        # equals the value we found
        src = code_base + off
        if src in seen:
            off += 1
            continue

        # Verify: look up the target address and check consistency
        tgt_rva = val - imgbase
        tgt_found = False
        for name2, va2, vs2, rsz2, rp2, chars2 in secs:
            if rp2 == 0 or name2 in (".rsrc", ".reloc"):
                continue
            if va2 <= tgt_rva < va2 + max(vs2, rsz2):
                tgt_file_offset = rp2 + (tgt_rva - va2)
                if tgt_file_offset + 4 <= len(data):
                    tgt_val = struct.unpack_from("<I", data, tgt_file_offset)[0]
                    if tgt_val == val:
                        tgt_found = True
                        break

        if tgt_found:
            raws.add(src)
            add_relocation(val, src, imgbase, seen, rows)

        off += 1


def texty(word: int) -> bool:
    """A dword of text: every byte is printable or NUL, and at least two are printable."""
    bs = struct.pack("<I", word)
    return all(b == 0 or 0x20 <= b < 0x7f for b in bs) and sum(1 for b in bs if b) >= 2


def find_relocations_in_data(
    data: bytes, imgbase: int, imagesize: int,
    sec_va: int, sec_rsz: int, sec_rp: int,
    sec_ranges: list, code_ranges: list, starts: set, seen: set, rows: list,
) -> None:
    raw = data[sec_rp : sec_rp + sec_rsz]
    base = imgbase + sec_va
    for off in range(0, len(raw) - 3, 4):
        if (base + off) & 3:
            continue
        val = struct.unpack_from("<I", raw, off)[0]
        if not is_in_image(val, imgbase, imagesize, sec_ranges):
            continue
        # a word of a string table ("REDE", "OSK\0"): the words around it are
        # text as well. A table of pointers 0x0047xxxx reads as text too
        # ("p0G\0"): its words are taken for pointers while they are plausible
        # ones -- into code at an instruction, into data 4-aligned -- and near
        # each other (PTR_NEAR; "PQL\0PEZ\0" are 0xdf400 apart).
        def plausible(word):
            if not is_in_image(word, imgbase, imagesize, sec_ranges):
                return False
            if any(lo <= word < hi for lo, hi in code_ranges):
                return word in starts
            return word & 3 == 0
        def text_around(o):
            if not 0 <= o <= len(raw) - 4:
                return False
            word = struct.unpack_from("<I", raw, o)[0]
            return texty(word) and not (plausible(word) and plausible(val)
                                        and abs(word - val) < PTR_NEAR)
        if texty(val) and any(text_around(o) for o in (off - 4, off + 4)):
            continue
        if any(lo <= val < hi for lo, hi in code_ranges) and val not in starts:
            continue
        add_relocation(val, base + off, imgbase, seen, rows)


def find_unaligned_runs(
    data: bytes, imgbase: int, imagesize: int,
    sec_va: int, sec_rsz: int, sec_rp: int,
    sec_ranges: list, code_ranges: list, starts: set, seen: set, rows: list,
) -> None:
    """Pointers that are not 4-aligned, in a table of records of one size."""
    raw = data[sec_rp : sec_rp + sec_rsz]
    base = imgbase + sec_va
    taken = set()
    for row in rows:
        src = int(row.split(",")[0], 16)
        if base - 3 <= src < base + sec_rsz:
            taken.update(range(src, src + 4))
    cand = {}
    for off in range(0, len(raw) - 3):
        addr = base + off
        if addr & 3 == 0:
            continue
        val = struct.unpack_from("<I", raw, off)[0]
        if not is_in_image(val, imgbase, imagesize, sec_ranges):
            continue
        # a pointer into code would be a float (0x46xxxx00 is 1e4) as often as not
        if val & 3 or any(lo <= val < hi for lo, hi in code_ranges):
            continue
        if any(a in taken for a in range(addr, addr + 4)):
            continue
        cand[addr] = val
    used = set()
    for addr in sorted(cand):
        if addr in used:
            continue
        for stride in range(8, RUN_STRIDE_MAX + 1):
            n = 0
            while addr + n * stride in cand:
                n += 1
            if n >= RUN_MIN:
                for k in range(n):
                    used.add(addr + k * stride)
                    add_relocation(cand[addr + k * stride], addr + k * stride, imgbase, seen, rows)
                break


def write_sci(sci_dir, rows, code_ranges, starts, sizes, immediates, memops, raws,
              entries, from1, read):
    def in_code(v):
        return any(lo <= v < hi for lo, hi in code_ranges)
    # the instruction of the linear sweep that holds an address (not at its start)
    order = sorted(sizes)
    import bisect
    def inside(v):
        i = bisect.bisect_right(order, v) - 1
        if i >= 0 and order[i] < v < order[i] + sizes[order[i]]:
            return order[i]
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    def reads_as_code(v):
        n = 0
        for insn in md.disasm(read(v, 16 * CODE_PROBE), v):
            if insn.bytes[:2] == b"\0\0":    # add [eax], al: zeroes
                return False
            n += 1
            if insn.mnemonic.startswith(("j", "ret", "call")) or n == CODE_PROBE:
                return True
        return False
    code, nocode, displaced = set(), set(), {}
    for row in rows:
        src, val = (int(x, 16) for x in row.split(",")[:2])
        if not (in_code(src) and in_code(val)):
            continue
        # a table entry first: the sweep may have read the table as instructions
        if src in entries:
            code.add(val)
        elif src in memops:
            insn = inside(val)
            if val in from1 and insn is not None:
                displaced[val] = insn + sizes[insn] - val
            else:
                nocode.add(val)
        elif src in immediates or src in raws:
            if val in starts and reads_as_code(val):
                code.add(val)
            elif src in immediates:
                nocode.add(val)
    # a table read with a negative index ends where code begins (memcpy of MSVC)
    nocode -= code
    os.makedirs(sci_dir, exist_ok=True)
    def write(name, lines):
        with open(os.path.join(sci_dir, name), "w", encoding="ascii", newline="\n") as f:
            f.writelines(line + "\n" for line in lines)
        print(f"wrote {len(lines)} lines to {os.path.join(sci_dir, name)}", file=sys.stderr)
    write("fixup_interpret_as_code.sci", [f"loc_{v:X}" for v in sorted(code)])
    write("fixup_do_not_interpret_as_code.sci", [f"loc_{v:X}" for v in sorted(nocode)])
    write("displaced_labels.sci", [f"loc_{v:X},{d}" for v, d in sorted(displaced.items())])


def main() -> int:
    argv = sys.argv[1:]
    code_names = set()
    while "--code" in argv:
        i = argv.index("--code")
        code_names.add(argv[i + 1])
        del argv[i:i + 2]
    sci_dir = None
    if "--sci" in argv:
        i = argv.index("--sci")
        sci_dir = argv[i + 1]
        del argv[i:i + 2]
    args = [a for a in argv if a not in ("--no-data", "--strict-code", "--unaligned-runs")]
    unaligned = "--unaligned-runs" in argv
    scan_data = "--no-data" not in argv
    strict = "--strict-code" in argv

    def is_code(name, chars):
        return bool(chars & 0x20000020) or name in code_names
    if not args:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = args[0]
    out = args[1] if len(args) > 1 else "relocations.csv"
    with open(path, "rb") as f:
        data = f.read()

    imgbase, imagesize, secs = pe_sections(data)
    sec_ranges = build_section_ranges(imgbase, secs)

    rows = []
    seen = set()

    starts = set()
    tables = set()
    immediates = set()
    memops = set()
    sizes = {}
    raws = set()
    entries = set()     # sources of jump table entries
    from1 = set()       # bases of tables indexed from 1
    code_ranges = []
    for name, va, vs, rsz, rp, chars in secs:
        if rsz == 0 or name in (".rsrc", ".reloc") or not is_code(name, chars):
            continue
        code_ranges.append((imgbase + va, imgbase + va + max(vs, rsz)))
        find_relocations_in_sec(
            data, imgbase, imagesize, va, rsz, rp,
            sec_ranges, secs, seen, rows, starts, tables, immediates, memops, sizes, raws,
        )
    # a function behind padding that the linear sweep went past
    def file_byte(addr):
        for _n, va, _vs, rsz, rp, _c in secs:
            if imgbase + va <= addr < imgbase + va + rsz:
                return data[rp + addr - imgbase - va]
        return None
    for row in rows:
        src, val = (int(x, 16) for x in row.split(",")[:2])
        if (src in immediates and val not in starts and val & 3 == 0
                and any(lo <= val < hi for lo, hi in code_ranges)
                and file_byte(val - 1) in (0x90, 0xCC)):
            starts.add(val)
    # a constant that points into the middle of an instruction is not an address
    def keep(row):
        src, val = (int(x, 16) for x in row.split(",")[:2])
        return (src not in immediates or val in starts
                or not any(lo <= val < hi for lo, hi in code_ranges))
    if strict:
        rows[:] = [r for r in rows if keep(r)]
    for base in sorted(tables):
        for lo, hi in code_ranges:
            if not lo <= base < hi:
                continue
            for sec_va, sec_rsz, sec_rp in ((va, rsz, rp) for _n, va, _vs, rsz, rp, _c in secs):
                if not imgbase + sec_va <= base < imgbase + sec_va + sec_rsz:
                    continue
                def entry(addr):
                    val = struct.unpack_from("<I", data, sec_rp + addr - imgbase - sec_va)[0]
                    # the sweep goes astray right after a table: a target near
                    # the table counts too
                    if lo <= val < hi and (val in starts or abs(val - base) < TABLE_REACH):
                        return val
                    return None
                addr = base
                # a table indexed from 1 begins one entry after its label
                if (addr + 8 <= imgbase + sec_va + sec_rsz and entry(addr) is None
                        and entry(addr + 4) is not None):
                    from1.add(base)
                    addr += 4
                while addr + 4 <= imgbase + sec_va + sec_rsz:
                    val = entry(addr)
                    if val is None:
                        break
                    add_relocation(val, addr, imgbase, seen, rows)
                    entries.add(addr)
                    addr += 4
    if scan_data:
        for name, va, vs, rsz, rp, chars in secs:
            if rsz == 0 or name in (".rsrc", ".reloc", ".idata") or is_code(name, chars) or not (chars & 0x40):
                continue
            find_relocations_in_data(data, imgbase, imagesize, va, rsz, rp,
                                     sec_ranges, code_ranges, starts, seen, rows)
            if unaligned:
                find_unaligned_runs(data, imgbase, imagesize, va, rsz, rp,
                                    sec_ranges, code_ranges, starts, seen, rows)

    rows.sort(key=lambda s: int(s.split(",", 1)[0], 16))
    if sci_dir is not None:
        def read(addr, n):
            for _n, va, _vs, rsz, rp, _c in secs:
                if imgbase + va <= addr < imgbase + va + rsz:
                    off = rp + addr - imgbase - va
                    return data[off:min(off + n, rp + rsz)]
            return b""
        write_sci(sci_dir, rows, code_ranges, starts, sizes, immediates, memops, raws,
                  entries, from1, read)
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(rows))
        if rows:
            f.write("\n")
    print(f"wrote {len(rows)} relocations to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
