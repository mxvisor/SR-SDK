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
