# tools/regress

One command, `run_all.sh`, that answers "did I break anything?" after a change to
SR, to `llasm.py` or to the SDK. It compares six things with a saved reference run:

| Check | What | Tool |
|---|---|---|
| `sr` | SR's and SRW's output (hashes) for SR's reference games (`SR/SR-games/`: backends llasm/x86/arm/x64; `SR/SRW-games/`: llasm/x86/x64) | `sr-regression.sh` |
| `build` | `make linux` and `make windows` of every game | `build_all.sh` |
| `d3d` | `smoke` again for a game's renderer variants (column 9 of `games.tsv`, e.g. a DirectDraw and a software Direct3D renderer; the default is `smoke`); the screenshots are in `smoke/` | `smoke.sh` (`SMOKE_EXTRA`, `SMOKE_LABEL`) |
| `smoke` | the port starts under Xvfb, runs, closes by `WM_DELETE_WINDOW`, exits by itself and leaves no process; Linux and Wine | `smoke.sh`, `xclose.py` |
| `image` | the disc images of `images.tsv` through `tools/image/image.py`: the hash of the cue sheet, the bin, the ISO and of every file of the data track must not change | `image_check.py` |
| `aslr` | three runs in a row at different load addresses: each has to stay alive; under Linux the image base must change between the runs | `aslr.sh` |
| `prefixes` | no tracked file of a game (name or text) carries another game's name or prefix (`markers.tsv`); the same for the SDK's `runtime/`, `scons/`, `android/` | `check_prefixes.py` |
| `llasm-gen`, `llasm-llvm` | hashes of `build/gen/**` and of the LLVM IR that `llasm.py` makes of it | `llasm_hashes.py` |

```sh
tools/regress/run_all.sh                    # everything, all games
tools/regress/run_all.sh --only sr          # SR's output for the reference games only
tools/regress/run_all.sh --only build,smoke <id> <id>
tools/regress/run_all.sh --jobs 2           # smoke and aslr run 4 at a time by default
tools/regress/run_all.sh --sr @worktree     # SR with its uncommitted changes
tools/regress/run_all.sh --save-ref         # the result becomes the reference
```

Each row of the comparison is `same`, `REGRESSION` (ok → fail), `improved`
(fail → ok), `CHANGED` (an SR output hash, or a llasm hash, differs) or `new`.
The exit status is 1 if there is a `REGRESSION` or a `CHANGED`.

A `CHANGED` is not always a defect: a fix in SR changes its output on purpose.
It means that the change must be looked at, and then the reference saved again.

## Files

- `games.tsv`: the games (repository, program, env prefix, data, disc image, Wine prefix).
  The paths are relative to the workspace root, the parent of `SR-SDK/`
  (`$DOS_RECOMP_ROOT` overrides it). An optional eighth column is the number of seconds the
  game is left running when it needs more than the default 15 to show something.
- `ref/`: the reference run: `results.tsv` (build, smoke, sr), `llasm-<game>.txt`
  (hashes of the llasm output of each game), `sr-hashes.txt` (SR's output per input,
  with the commit it was taken at).
- `lib.sh`: what the scripts share.

## sr-regression.sh

```sh
tools/regress/sr-regression.sh WORKDIR [COMMIT...]     # default HEAD; @worktree
```

It needs the images of the reference games and the tools to unpack them; the header
of the script says where they are looked for. Every SR build and output stays in WORKDIR;
SR's own messages are kept in `sr.out` and `sr.err` of each output.
`SR_ONLY='<grep -E pattern over kind:dir>'` limits the inputs.

## smoke.sh

```sh
tools/regress/smoke.sh GAME linux|wine [RUN_SECS] [CLOSE_WAIT]
```

The exit status of a closed game is only reported (`rc=`): whether non-zero is
acceptable is not decided here. A game that is not built, or that does not start
under Wine, is `fail`.

## aslr.sh

```sh
tools/regress/aslr.sh GAME linux|wine [RUNS=4] [RUN_SECS=6]
```

Starts the port RUNS times, reads from `/proc/<pid>/maps` where its image was
loaded and counts the distinct base addresses. `ok`: every run is alive after
RUN_SECS, and under Linux (PIE, kernel ASLR on) there are at least two bases.
Under Wine a fixed base is not an error, only a run that dies is.

## Clearing a reference after an intended change

1. Run `run_all.sh`; look at every `CHANGED` and `REGRESSION`.
2. When all of them are explained, `run_all.sh --save-ref` and commit `ref/`.

`smoke` and `aslr` run `--jobs` games at a time; games share one Wine prefix (dos-recomp-wine), so the Wine runs go one after another.
A run waits for the game only until it exits, and under Wine `aslr` starts the game once (the image base is fixed there). A full run takes a few minutes, most of it the slowest game's start.
A close by the user has to end with exit status 0 (`rc=0`).
