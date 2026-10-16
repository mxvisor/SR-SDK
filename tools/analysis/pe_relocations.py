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
# disassembly is not taken for an address (a pushed flag set that falls in the image).  Without it every in-image constant seen in the disassembly is a relocation.
#
# A jump table inside a code section (jmp [reg*4 + table]) is not instructions:
# its entries are added while they point at the start of an instruction.
#
# Usage: pe_relocations.py [--no-data] [--strict-code] <pe.exe> [out.csv]

from __future__ import annotations

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


def find_relocations_in_sec(
    data: bytes, imgbase: int, imagesize: int,
    sec_va: int, sec_rsz: int, sec_rp: int,
    sec_ranges: list, secs: list, seen: set, rows: list, starts: set, tables: set,
    immediates: set,
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
        # a word of a string table ("REDE"): the words around it are text as well
        if texty(val) and any(
                0 <= o <= len(raw) - 4 and texty(struct.unpack_from("<I", raw, o)[0])
                for o in (off - 4, off + 4)):
            continue
        if any(lo <= val < hi for lo, hi in code_ranges) and val not in starts:
            continue
        add_relocation(val, base + off, imgbase, seen, rows)


def main() -> int:
    args = [a for a in sys.argv[1:] if a not in ("--no-data", "--strict-code")]
    scan_data = "--no-data" not in sys.argv[1:]
    strict = "--strict-code" in sys.argv[1:]
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
    code_ranges = []
    for name, va, vs, rsz, rp, chars in secs:
        if rsz == 0 or name in (".rsrc", ".reloc") or not (chars & 0x20000020):
            continue
        code_ranges.append((imgbase + va, imgbase + va + max(vs, rsz)))
        find_relocations_in_sec(
            data, imgbase, imagesize, va, rsz, rp,
            sec_ranges, secs, seen, rows, starts, tables, immediates,
        )
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
                addr = base
                while addr + 4 <= imgbase + sec_va + sec_rsz:
                    val = struct.unpack_from("<I", data, sec_rp + addr - imgbase - sec_va)[0]
                    if not (lo <= val < hi and val in starts):
                        break
                    add_relocation(val, addr, imgbase, seen, rows)
                    addr += 4
    if scan_data:
        for name, va, vs, rsz, rp, chars in secs:
            if rsz == 0 or name in (".rsrc", ".reloc", ".idata") or (chars & 0x20000020) or not (chars & 0x40):
                continue
            find_relocations_in_data(data, imgbase, imagesize, va, rsz, rp,
                                     sec_ranges, code_ranges, starts, seen, rows)

    rows.sort(key=lambda s: int(s.split(",", 1)[0], 16))
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(rows))
        if rows:
            f.write("\n")
    print(f"wrote {len(rows)} relocations to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
