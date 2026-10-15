# llasm.py

A Python port of `SR/llasm/llasm.d`, the translator from SR's llasm IR to
LLVM IR. It is what `scons/game.py` runs by default, with the Python that runs
SCons, so building a game needs no D compiler. `scons llasm=<path>` takes an
llasm executable instead.

The port follows `llasm.d` line by line, down to the order of operations and
the numbering of temporaries and labels: for the same input and options it
writes the same output, the same messages and the same exit status. The one
exception is which error is named when several `daddr` of different data
segments point nowhere: `llasm.d` checks them in the order of a D associative
array, which depends on the D runtime; the exit status is the same.

## Keeping it in step with llasm.d

Every change to `SR/llasm/llasm.d` has to be carried over here. The three
scripts beside it run the D build and the port on the same input and compare
stdout, stderr and the exit status:

```
gdc -O2 ../SR/llasm/llasm.d -o llasm      # or ldc2/dmd; or set $LLASM_D
python3 llasm_edge.py                     # hand-written cases: errors, includes, macros, options
python3 llasm_fuzz.py -n 2000 -j 8        # random programs over all instruction forms
python3 llasm_compare.py -j 8             # every .llasm of the repository, several option sets
python3 llasm_compare.py -I <dir> FILE... # the .llasm of a game, with its include directories
```

`tests/run.py` runs all of it (plus unit tests of the pure functions, which
need no D build): quick by default, `--full` before a commit.

## Layout

`llasm.py` is only the entry point; the port is the package `llasmlib/`, in
the order of dependencies (a module imports only from those above it):

| module | what |
|---|---|
| `tables` | register and instruction tables, constants, `_llref` |
| `strutil` | `strip`/`lstrip`/`rstrip`/`str_split_strip` with the semantics of D |
| `state` | data classes and the global state `G` (`reset_state`) |
| `expr` | numbers, constant expressions, `define` replacement |
| `source` | reading lines, includes, macros, instruction tables (`initialize`) |
| `emit` | output lines, temporaries, register cache, operand read/write |
| `x87` | FPU stack helpers |
| `proc` | one procedure: `_analyze`, `_backward_pass`, `_declare_registers`, `_emit`; an instruction is a handler `_analyze_*(c)` / `_emit_*(c)` found by the tables `_ANALYZE_TABLE` / `_EMIT_TABLE` |
| `reader` | the first pass over the input: keywords `define`, `extern`, `proc`, data segments… (`_read_*(cl)`) |
| `generate` | the second pass: writing the module |
| `cli` | options and `main` |

What the `llasm.d` function you are changing is called here: the `if`/`elif`
chains of `processProcBody` are the handler tables of `proc`, the branches of
`main`'s read loop are `_read_*` of `reader`, the output part of `main` is
`generate`. A handler gets the pass state in `c` (`c.word`, `c.paramvals`,
`c.last_reg_write` …); a reader handler returns None or an exit status.
The order of operations and the numbering of temporaries and labels must not
change: the tests above compare the whole output.
