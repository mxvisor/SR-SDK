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
# Output format matches SRW's relocations.csv (see SRW_loader.c LoadRelocations):
#   0x<FixupAddress>,0x<TargetAddress>,[i]
#
# Usage: pe_relocations.py <pe.exe> [out.csv]

from __future__ import annotations

import struct
import sys

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


def find_relocations_in_sec(
    data: bytes, imgbase: int, imagesize: int,
    sec_va: int, sec_rsz: int, sec_rp: int,
    sec_ranges: list, secs: list, seen: set, rows: list,
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

            src = code_base + off
            add_relocation(val, src, imgbase, seen, rows)

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


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "relocations.csv"
    with open(path, "rb") as f:
        data = f.read()

    imgbase, imagesize, secs = pe_sections(data)
    sec_ranges = build_section_ranges(imgbase, secs)

    rows = []
    seen = set()

    for name, va, vs, rsz, rp, chars in secs:
        if rsz == 0:
            continue
        if name in (".rsrc", ".reloc"):
            continue
        if not (chars & 0x20000020):
            continue
        find_relocations_in_sec(
            data, imgbase, imagesize, va, rsz, rp,
            sec_ranges, secs, seen, rows,
        )

    rows.sort(key=lambda s: int(s.split(",", 1)[0], 16))
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(rows))
        if rows:
            f.write("\n")
    print(f"wrote {len(rows)} relocations to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
