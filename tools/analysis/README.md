# tools/analysis

Tools for the work on a game's SR input (not part of the build). They take the
paths of the game's files as arguments.

| File | What it does |
|---|---|
| `leparse.py` | LE (DOS4GW) header, object table, address map |
| `lefixups.py` | the LE fixup records, by target range |
| `sr_cov.py`, `sr_iterate.py` | code that SR still emits as data: candidates for `fixup_interpret_as_code.sci`, iterated until stable (`--exe NAME` names the game's executable) |
| `pe_relocations.py` | PE base relocations rebuilt with Capstone for an executable that has none (`relocations.csv` of SRW); `--sci DIR` also writes the code, no-code and displaced-label lists SRW needs for tables in code |
| `udump.c` | decodes a raw section with the udis86 build SR uses (build line in the header) |

`tools/flagfix.py` (the Win32 ports, SRW) and `tools/gen_import_shim.py` (the import shim) are in `tools/`.
Game-specific scripts (object tables of one executable and so on) stay in the game's `research/`.
