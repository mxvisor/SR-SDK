#!/usr/bin/env python3
"""Iteratively resolve SRW's llasm 'unknown flags on start of region' errors.

For each failure at address A with flag mask F:
  * find every way into A: the branches that target it and the instruction
    before it when that one falls through (not jmp/ret),
  * for each, emit  loc_<producer>,<F>,0   and   loc_<A>,0,<F>.
An entry loc_<A>,0,<F> makes A read F from eflags, so every way into A has to
leave F there: once SR runs clean, the entries are checked for that and the
missing loc_<producer>,<F>,0 are added (a call before A is left alone: the
callee's flags are its own business).

Instruction boundaries come from objdump's disassembly of the very input SR
reads (build/input/<module>/<module>.EXE), so no x86 companion track is
needed.  Also folds in SR's own back-to-back conditional-jump annotations
('iflags->end/begin conflict').

For the Win32 ports (SRW, llasm).  Run it from the game repository after
`scons stage=recomp`; it rewrites game/recomp/<module>/default/llasm/
instruction_flags.sci.

Usage:  python3 SR-SDK/tools/flagfix.py [--module NAME] [--max N] [--root DIR]
        (--root: the game repository, default the current directory)
"""
import glob
import os
import re
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.getcwd()

# ---- locate this module's build ----------------------------------------
args = sys.argv[1:]
module_arg = None
maxiter = 400
while args:
    a = args.pop(0)
    if a == '--module':
        module_arg = args.pop(0)
    elif a == '--root':
        ROOT = os.path.abspath(args.pop(0))
    elif a == '--max':
        maxiter = int(args.pop(0))
    else:
        sys.exit(__doc__)

if module_arg is None:
    cfg = open(os.path.join(ROOT, 'game', 'game.cfg'), encoding='utf-8').read()
    m = re.search(r'^\s*modules?\s*=\s*([^\s,]+)', cfg, re.M)
    if not m:
        sys.exit('flagfix: no modules= in game/game.cfg, pass --module')
    module_arg = m.group(1)
MODULE = module_arg


def one(pattern, what):
    hits = sorted(glob.glob(os.path.join(ROOT, pattern)))
    if not hits:
        sys.exit('flagfix: no %s (run the scons stage that produces it): %s'
                 % (what, pattern))
    if len(hits) > 1:
        sys.exit('flagfix: several %s, pass --module:\n  %s'
                 % (what, '\n  '.join(hits)))
    return hits[0]


SRW = one(os.path.join('build', 'host', '*', 'sr-llasm', 'SRW.exe'), 'SRW.exe')
INPUT_EXE = one(os.path.join('build', 'input', MODULE, '*.EXE'), 'input EXE')
SRDIR = os.path.join(ROOT, 'build', 'recomp', MODULE, 'llasm')
SCI = os.path.join(ROOT, 'game', 'recomp', MODULE, 'default', 'llasm',
                   'instruction_flags.sci')

# ---- disassembly of the input ------------------------------------------
# objdump -d walks each code section linearly, so every address it prints is a
# real instruction boundary -- the same boundaries SR derives from the same
# bytes.
INSTR = {}          # address -> instruction text
ADDRS = []          # sorted boundaries
TARGETS = {}        # branch target -> [(addr, text)]
BRANCHES = []       # (addr, text, target)

BRANCH = re.compile(r'^(j\w+|loop\w*)\s+(?:short\s+)?0x([0-9a-f]+)$')


def load_disassembly():
    out = subprocess.run(['objdump', '-d', '-M', 'intel', INPUT_EXE],
                         capture_output=True, text=True).stdout
    row = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$')
    for line in out.split('\n'):
        m = row.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        text = m.group(3).strip()
        INSTR[addr] = text
        ADDRS.append(addr)
        b = BRANCH.match(text)
        if b:
            target = int(b.group(2), 16)
            BRANCHES.append((addr, text, target))
            TARGETS.setdefault(target, []).append((addr, text))
    if not ADDRS:
        sys.exit('flagfix: objdump produced no instructions for %s' % INPUT_EXE)
    ADDRS.sort()


def section_bases():
    """SR's section index -> flat base, from the PE section headers."""
    out = subprocess.run(['objdump', '-h', INPUT_EXE],
                         capture_output=True, text=True).stdout
    bases = []
    for line in out.split('\n'):
        m = re.match(r'\s+(\d+)\s+\S+\s+[0-9a-f]+\s+([0-9a-f]+)', line)
        if m:
            bases.append(int(m.group(2), 16))
    return bases


SEC_BASES = section_bases()

WRITES_FLAGS = set("""cmp test add sub adc sbb and or xor neg dec inc
 shl shr sar rol ror rcl rcr mul imul div idiv bt bts btr btc bsf bsr
 cmpsb cmpsw cmpsd scasb scasw scasd sahf""".split())

FLAGNAME = [(0x01, "CARRY"), (0x02, "PARITY"), (0x04, "ADJUST"), (0x08, "ZERO"),
            (0x10, "SIGN"), (0x20, "OVERFLOW"), (0x100000, "INST_Z")]


def flagstr(f):
    return " | ".join("FL_" + n for b, n in FLAGNAME if f & b) or hex(f)


# ---- producer selection -------------------------------------------------
def producers_for(addr):
    """Branches that enter the region at addr, or its fall-through source."""
    if addr not in INSTR:
        return [], ("no instruction boundary lands on %X -- objdump and SR "
                    "disagree there (data in code?)" % addr)
    out = [(a, t) for a, t, tgt in BRANCHES if tgt == addr and a != addr]
    # and the instruction before it, when control falls through from there:
    # the flags it leaves are the ones A sees on that way in
    prev = fall_through_into(addr)
    if prev is not None and prev[1].split()[0] != 'call':
        out.append(prev)
    if out:
        return out, ""
    return [], "no branch into %X and no flag writer before it" % addr


def fall_through_into(addr):
    """(address, text) of the instruction before addr if it falls through."""
    i = ADDRS.index(addr)
    if i == 0:
        return None
    prev = ADDRS[i - 1]
    mn = INSTR[prev].split()[0]
    if mn == 'jmp' or mn.startswith('ret') or mn in ('hlt', 'int3'):
        return None
    return prev, INSTR[prev]


def missing_producers():
    """Entries that read flags from eflags (end) with a way in that does not
    leave them there (begin): [(producer, flags, text)]."""
    entries = {}
    if os.path.exists(SCI):
        for l in open(SCI, encoding='utf-8'):
            m = ENT.match(l.rstrip("\n"))
            if m:
                entries[int(m.group(1), 16)] = (int(m.group(2), 0), int(m.group(3), 0))
    missing = []
    for addr, (_begin, end) in sorted(entries.items()):
        if not end or addr not in INSTR:
            continue
        ways = [(a, t) for a, t, tgt in BRANCHES if tgt == addr and a != addr]
        prev = fall_through_into(addr)
        if prev is not None:
            ways.append(prev)
        for p, t in ways:
            if t.split()[0] == 'call':
                continue
            if entries.get(p, (0, 0))[0] & end != end:
                missing.append((p, end, t, addr))
    return missing


# ---- running SR ---------------------------------------------------------
def run_sr():
    """The command `scons stage=recomp` runs, with the .sci files in place."""
    if not os.path.isdir(SRDIR):
        sys.exit('flagfix: %s does not exist (run: scons stage=recomp first)'
                 % SRDIR)
    src = os.path.dirname(SCI)
    for name in ('SR.cfg', 'bssborder.csv', 'relocations.csv'):
        p = os.path.join(os.path.dirname(src), name)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(SRDIR, name))
    for sci in sorted(glob.glob(os.path.join(src, '*.sci'))):
        shutil.copy2(sci, os.path.join(SRDIR, os.path.basename(sci)))
    r = subprocess.run([SRW, INPUT_EXE, MODULE + '.asm'], cwd=SRDIR,
                       capture_output=True, text=True)
    return (r.stdout or "") + (r.stderr or "")


ERR = re.compile(r'unknown flags on start of region - (\d+) - \d+ '
                 r'\(0x([0-9a-f]+)\) - 0x([0-9a-f]+)')
FATAL = re.compile(r'Error: (mixing flags and instruction flags|unprocessed instruction flags)[^\n]*')
CONFLICT = re.compile(r'iflags->(end|begin) conflict - (\d+) - (\d+)')

ENT = re.compile(r"^loc_([0-9A-Fa-f]+)\s*,\s*([^,]+?)\s*,\s*([^;]+?)\s*(;.*)?$")


def add_entries(addr, flags, want):
    """Add/merge flag entries.  SR keeps one entry per address, so a second
    entry for an address already listed silently replaces the first -- the
    masks have to be OR-ed into the existing line instead of appended."""
    lines = open(SCI, encoding='utf-8').read().split("\n") \
        if os.path.exists(SCI) else []
    index = {}
    for i, l in enumerate(lines):
        m = ENT.match(l)
        if m:
            index[int(m.group(1), 16)] = (i, int(m.group(2), 0),
                                          int(m.group(3), 0),
                                          (m.group(4) or "").strip())
    fresh = []
    for a, s_, c_, txt in want:
        if a in index:
            i, os_, oc_, cm = index[a]
            lines[i] = "loc_%X,0x%02X,0x%02X %s" % (a, os_ | s_, oc_ | c_, cm)
        else:
            fresh.append("loc_%X,0x%02X,0x%02X ; %s" % (a, s_, c_, txt))
    if fresh:
        lines += ["", "; 0x%X needs %s across a region boundary"
                  % (addr, flagstr(flags))] + fresh
    os.makedirs(os.path.dirname(SCI), exist_ok=True)
    open(SCI, "w", encoding="utf-8").write("\n".join(lines).rstrip("\n") + "\n")


# ---- SR's automatic handling of back-to-back conditional jumps ----------
# SR_disassemble_change_cjump_iflags() pairs a jcc that directly follows another
# jcc: it wants `end` on the second to include the second's flags_needed, and
# `begin` on the first to include them too.  It refuses to OR those bits into an
# entry this file already wrote, so our entries have to carry them already.
JCC_FLAGS = {
    "jo": 0x20, "jno": 0x20, "js": 0x10, "jns": 0x10, "jp": 0x02, "jpe": 0x02,
    "jnp": 0x02, "jpo": 0x02, "je": 0x08, "jz": 0x08, "jne": 0x08, "jnz": 0x08,
    "jb": 0x01, "jc": 0x01, "jnae": 0x01, "jae": 0x01, "jnb": 0x01, "jnc": 0x01,
    "ja": 0x09, "jnbe": 0x09, "jbe": 0x09, "jna": 0x09,
    "jl": 0x30, "jnge": 0x30, "jge": 0x30, "jnl": 0x30,
    "jg": 0x38, "jnle": 0x38, "jle": 0x38, "jng": 0x38,
}


def instr_at_and_prev(addr):
    if addr not in INSTR:
        return None, None
    i = ADDRS.index(addr)
    prev = (ADDRS[i - 1], INSTR[ADDRS[i - 1]]) if i > 0 else None
    return INSTR[addr], prev


def fix_conflict(kind, entry, offset):
    flat = SEC_BASES[entry] + offset
    txt, prev = instr_at_and_prev(flat)
    if txt is None:
        return None
    mn = txt.split()[0]
    flags = JCC_FLAGS.get(mn)
    if flags is None:
        return None
    if kind == "end":
        target, field = flat, "end"
    else:
        if prev is None:
            return None
        target, field = prev[0], "begin"
    want = [(target, flags if field == "begin" else 0,
             flags if field == "end" else 0, txt)]
    add_entries(target, flags, want)
    return "0x%X %s += %s (%s %s)" % (target, field, flagstr(flags), mn, kind)


def main():
    load_disassembly()
    seen = set()
    for n in range(maxiter):
        out = run_sr()
        mc = CONFLICT.search(out)
        if mc:
            info = fix_conflict(mc.group(1), int(mc.group(2)),
                                int(mc.group(3)))
            if info is None:
                print("UNRESOLVED conflict %s (section %s ofs %s)"
                      % (mc.group(1), mc.group(2), mc.group(3)))
                print(out[-800:])
                return 4
            print("[%3d] conflict fix: %s" % (n + 1, info))
            continue
        bad = FATAL.search(out)
        if bad:
            print("SR refuses an entry: %s" % bad.group(0))
            return 5
        m = ERR.search(out)
        if not m:
            missing = missing_producers()
            if missing:
                for p, f, t, a in missing:
                    add_entries(a, f, [(p, f, 0, t)])
                    print("[%3d] 0x%-7X %-28s <- 0x%X %s (way in without them)"
                          % (n + 1, a, flagstr(f), p, t))
                continue
            print("\n=== no flag error ===")
            print(out[-1500:])
            return 0
        addr = int(m.group(2), 16)
        flags = int(m.group(3), 16)
        if (addr, flags) in seen:
            print("STUCK: %X / %s did not clear" % (addr, flagstr(flags)))
            return 1
        seen.add((addr, flags))
        prods, note = producers_for(addr)
        if not prods:
            print("UNRESOLVED %X flags=%s  %s" % (addr, flagstr(flags), note))
            return 2
        want = [(p_, flags, 0, t_) for p_, t_ in prods]
        want.append((addr, 0, flags, INSTR.get(addr, "consumer")))
        add_entries(addr, flags, want)
        print("[%3d] 0x%-7X %-28s <- %s"
              % (n + 1, addr, flagstr(flags),
                 ", ".join("0x%X %s" % (p, t) for p, t in prods)))
    print("hit iteration cap")
    return 3


sys.exit(main())
