#!/usr/bin/env python3
"""Minimal LE (Linear eXecutable) reader for DOS4GW/WATCOM DOS executables.

Prints the LE header, object table, and (with --map) a file<->virtual
address mapping for every byte of every object.

Usage: leparse.py EXE [--map]
"""

import struct
import sys


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


LE_FIELDS = [
    # (name, size, is_u16)
    ("Signature", 2, True),
    ("ByteOrdering", 1, True),
    ("WordOrdering", 1, True),
    ("FormatLevel", 4, False),
    ("CPUType", 2, True),
    ("OSType", 2, True),
    ("ModuleVersion", 4, False),
    ("ModuleFlags", 4, False),
    ("NumPages", 4, False),
    ("EIPObjectNum", 4, False),
    ("EIP", 4, False),
    ("ESPObjectNum", 4, False),
    ("ESP", 4, False),
    ("PageSize", 4, False),
    ("BytesOnLastPage", 4, False),
    ("FixupSectionSize", 4, False),
    ("FixupSectionChecksum", 4, False),
    ("LoaderSectionSize", 4, False),
    ("LoaderSectionChecksum", 4, False),
    ("ObjectTableOffset", 4, False),
    ("NumObjects", 4, False),
    ("ObjectPageMapTableOffset", 4, False),
    ("ObjectIteratedPagesOffset", 4, False),
    ("ResourceTableOffset", 4, False),
    ("NumResourceTableEntries", 4, False),
    ("ResidentNameTableOffset", 4, False),
    ("EntryTableOffset", 4, False),
    ("ModuleDirectivesOffset", 4, False),
    ("NumModuleDirectives", 4, False),
    ("FixupPageTableOffset", 4, False),
    ("FixupRecordTableOffset", 4, False),
    ("ImportModuleTableOffset", 4, False),
    ("NumImportModuleEntries", 4, False),
    ("ImportProcedureTableOffset", 4, False),
    ("PerPageChecksumOffset", 4, False),
    ("DataPagesOffset", 4, False),
    ("NumPreloadPages", 4, False),
    ("NonResidentNameTableOffset", 4, False),
    ("NonResidentNameTableLength", 4, False),
    ("NonResidentNameTableChecksum", 4, False),
    ("AutoDSObjectNum", 4, False),
    ("DebugInfoOffset", 4, False),
    ("DebugInfoLength", 4, False),
    ("NumInstancePreload", 4, False),
    ("NumInstanceDemand", 4, False),
    ("HeapSize", 4, False),
]


def read_header(data, lf):
    off = 0
    vals = {}
    for name, size, is16 in LE_FIELDS:
        if is16:
            vals[name] = u16(data, lf + off)
        else:
            vals[name] = u32(data, lf + off)
        off += size
    return vals


def read_objects(data, lf, hdr):
    objs = []
    ot = hdr["ObjectTableOffset"]
    for i in range(hdr["NumObjects"]):
        e = struct.unpack_from("<IIIIII", data, lf + ot + i * 24)
        objs.append(dict(virtual_size=e[0], relbase=e[1], flags=e[2],
                         ptindex=e[3], npages=e[4], reserved=e[5]))
    return objs


def addr_to_file(data, lf, hdr, objs, addr):
    """Return (file offset, length) for virtual address addr."""
    for o in objs:
        size = o["virtual_size"]
        start = o["relbase"]
        if start <= addr < start + size:
            rel = addr - start
            page = rel // hdr["PageSize"]
            # pages are contiguous in file per object
            foff = hdr["DataPagesOffset"] + padto(hdr, objs, o) + page * hdr["PageSize"] + rel % hdr["PageSize"]
            return foff
    return None


def padto(hdr, objs, target):
    """file offset within data pages where target object's data begins."""
    pos = 0
    for o in objs:
        if o is target:
            return pos
        pos += o["npages"] * hdr["PageSize"]
    return pos


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    data = open(path, "rb").read()
    lf = u32(data, 0x3C)
    if data[lf:lf + 2] != b"LE":
        print("not an LE executable")
        return 1
    hdr = read_header(data, lf)
    objs = read_objects(data, lf, hdr)
    print("LE header facts:")
    for name in ("PageSize", "DataPagesOffset"):
        print("  %-20s 0x%X" % (name, hdr[name]))
    eip_obj = hdr["EIPObjectNum"] - 1
    esp_obj = hdr["ESPObjectNum"] - 1
    code = []
    data_objs = []
    for i, o in enumerate(objs):
        kind = "code" if (o["flags"] & 4) else ("data" if (o["flags"] & 1) else "?")
        if kind == "code":
            code.append(i)
        else:
            data_objs.append(i)
        print("  obj%d: relbase=0x%08X vsize=0x%06X npages=%d flags=0x%X %s%s%s"
              % (i, o["relbase"], o["virtual_size"], o["npages"], o["flags"], kind,
                 "  <- EIP obj" if i == eip_obj else "",
                 "  <- ESP obj" if i == esp_obj else ""))
    print("  entry      = 0x%X" % (objs[eip_obj]["relbase"] + hdr["EIP"]))
    print("  stack top  = 0x%X" % (objs[esp_obj]["relbase"] + hdr["ESP"]))
    print("  code objs  : %s" % code)
    print("  data objs  : %s" % data_objs)

    if "--map" in sys.argv:
        print("\nfile->addr ranges:")
        pos = hdr["DataPagesOffset"]
        for i, o in enumerate(objs):
            size = o["npages"] * hdr["PageSize"]
            print("  obj%d: file [0x%X,0x%X) -> vaddr [0x%X,0x%X)"
                  % (i, pos, pos + size, o["relbase"],
                     o["relbase"] + o["virtual_size"]))
            pos += size
    return 0


if __name__ == "__main__":
    sys.exit(main())