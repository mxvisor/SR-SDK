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
| `pe_codeptrs.py` | for a PE with base relocations: which addresses in code stored by `mov dword [mem], imm32` are code and which data (SRW takes two adjacent relocations for a table of code addresses, and misses a routine stored into a structure or onto the stack); `--sci DIR` adds them to the two fixup lists, `--data-var` names a variable that holds data addresses too, `--near ADDR` lists the relocation targets below the address of an SRW error |
| `pe_callflags.py` | `instruction_flags.sci` from the executable alone: every call site whose flags are read after the call and every ret of its callees (also the calls in tail position), for code that returns its results in the flags throughout; `retflags.py` sees only what SR has decoded |
| `pe_imports.py`, `pe_exports.py`, `pe_callargs.py` | PE32 imports (dll, name, IAT slot, argc), DLL exports with the `ret N` of each, and the arg count of the import calls from the call sites |
| `pe_symbols.py` | the COFF symbol table a PE kept (names of functions and variables) |
| `exe_dis.py` | the instructions around an address of a PE or LE (the `loc_` of an error, a `.sci` line, a crash) |
| `exe_match.py` | the functions of one executable found in another by their shape (masked addresses): the C library, the Miles AIL or the extender's code of a game already ported, in a new one |
| `exe_image.py` | the PE/LE loader of the tools above (module) |
| `sr_gdb.sh` | SR (SRW) that crashes on a game's input: an `-O0 -g` build of the game's copy under gdb on the game's inputs, with the backtrace |
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
- Machine-translated code of another CPU (its registers are variables in memory, its data lies in the
  code section between the routines, its routines return the condition codes): an emulated address
  register holds code and data addresses alike (`pe_codeptrs.py --data-var`), and a table of 16-bit
  pairs can pass the weak probe, which is why fields and the stack get the strict one.
- Runs under Xvfb need `SDL_VIDEODRIVER=x11` (the launchers and `tools/regress` set it): without it SDL may
  open the window on the desktop's Wayland display.
