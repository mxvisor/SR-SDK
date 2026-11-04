# tools/analysis

Tools for the work on a game's SR input (not part of the build). They take the
paths of the game's files as arguments.

| File | What it does |
|---|---|
| `leparse.py` | LE (DOS4GW) header, object table, address map |
| `lefixups.py` | the LE fixup records, by target range |
| `sr_cov.py`, `sr_iterate.py` | code that SR still emits as data: candidates for `fixup_interpret_as_code.sci`, iterated until stable (`--exe NAME` names the game's executable) |
| `pe_relocations.py` | PE base relocations rebuilt with Capstone for an executable that has none (`relocations.csv` of SRW); `--sci DIR` also writes the code, no-code and displaced-label lists SRW needs for tables in code |
| `sr_cover.py` | the same for SRW (PE): grows `fixup_interpret_as_code.sci` from the code addresses the executable's relocations point at (`--repo`, `--module`, `--variant`; run in a `-recomp` directory) |
| `retflags.py` | rets whose flags a caller reads after the call, from SR's `seg01_code.llinc`: `instruction_flags.sci` lines (direct calls only; the scan after a call stops at the next call) |
| `pe_imports.py`, `pe_exports.py`, `pe_callargs.py` | PE32 imports (dll, name, IAT slot, argc), DLL exports with the `ret N` of each, and the arg count of the import calls from the call sites |
| `udump.c` | decodes a raw section with the udis86 build SR uses (build line in the header) |

`tools/gen_import_shim.py` (the import shim) is in `tools/`. SRW passes the flags between regions itself; a game's `instruction_flags.sci` keeps only what it cannot derive (jump-table targets, flags a callee leaves).
Game-specific scripts (object tables of one executable and so on) stay in the game's `research/`.

Traps met so far:
- `pe_relocations.py` without `--strict-code` takes every in-image constant for an address: fixed-point
  numbers (16.16: `0x5a0000` is 90.0) then get relocated and the data goes wrong. Look through a
  new `relocations.csv` for such constants.
- The CRT's helpers can hand their caller a result in the flags: the argument loaders of the math functions
  (ZF for an infinity or a NaN) and the double compare of Watcom's CRT. SRW
  does not keep flags across a `ret`; `retflags.py` finds these rets for a new game. It knows only entries on
  the `ret` itself: one put on the instruction that makes the flag is listed again.
- Runs under Xvfb need `SDL_VIDEODRIVER=x11` (the launchers and `tools/regress` set it): without it SDL may
  open the window on the desktop's Wayland display.
