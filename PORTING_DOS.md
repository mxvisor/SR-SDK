# Porting a DOS game

A walk through a new port of a DOS/4GW (LE) game, from the original's files to a program that plays:
what to run, what SR stops at and what to do about it, and the traps met so far. The README's
[Starting a new port](README.md#starting-a-new-port) and [What a game provides](README.md#what-a-game-provides)
are the reference; the tools are in [`tools/analysis/`](tools/analysis/README.md).

Keep a journal while you port: every step with its command, not only its result, in the game's
`research/` (never in the `-recomp` repository). What you did by hand twice is a candidate for the SDK.

Addresses below are `loc_XXXXX`, SR's names: the linear address in the original, as its object
table places it (the first object of a Watcom LE usually at 10000h).

## 1. The original's files

- The game comes on a CD image (`image=` of `game/distrib.cfg`, unpacked by `tools/image/image.py`)
  or on floppies (`floppy=`: FAT12 images unpacked by `tools/floppy.py`, which knows the HUF
  archives and the F Install archive cut into `DISK1`, `DISK2`, ...). An installer the SDK does not
  know gets an unpacker in `tools/image/` or `floppy.py`, not a script of the game: the next game of
  the same publisher has it too.
- Several releases of a game differ in the executable (a patch, another language, a copy-protection
  variant). Choose one and keep its sha256 (`game/recomp/<module>/<variant>/sha256`); the port is of
  that executable only. A disc may carry a DOS and a Windows 95 program of the same game, and a patch
  may exist for one of them only: write down which one the port is of and why. The other one's files
  (its setup, DirectX) go into `skip_files=` and `skip_dirs=`.
- The game's own settings file (the sound card, the controls) is what its setup program wrote. A
  default one comes with the distrib copy (`extra_files=` or the installer's own); the port's
  settings live in `<program>.cfg`, not in it.

## 2. The skeleton

    SR-SDK/tools/new_game.sh <dir> <program> dos --image <disc image> --exe disc:<GAME>.EXE --env <GAME>

It writes `audio_*` into `game/distrib.cfg` only for a disc with audio tracks. In `.gitmodules` it
names the SDK's public origin, and the clone's own origin stays the local SDK. Then:

- `game/distrib.cfg`: `skip_files=` for the installer's files;
- `game/game.cfg`: `devices=` (add `arm64-android` and `android_id=` when the game is ready for a
  phone);
- `scons stage=input` checks the sha256; the first `scons` builds SR itself (about a minute).

## 3. The executable

- `tools/analysis/leparse.py <exe>`: the objects, the entry point, the stack. A Watcom program bound
  to DOS/4G is the extender's MZ and BW blocks followed by the program's own MZ and LE; SR and the
  tools find the LE themselves (`exe_image.le_offset`).
- `tools/analysis/exe_dis.py <exe> <address>`: the instructions around an address (an error of SR, a
  crash, a `.sci` line).
- `tools/analysis/exe_match.py`: a function of a port already done found in the new executable (the
  C library, the extender's code, a sound library), so a known replacement can be reused.
- Banners in the code name the libraries the game was linked with (`strings`): a sound library, a
  timer library, a version. Each has its own traps; look at the SDK's runtime for one already done
  (the Miles AIL 2 and 3, DSMI's MCP).

## 4. SR's first run: `scons stage=recomp`

SR writes `build/recomp/<module>/llasm/sr.err`. An error names the object and the offset in it in
decimal (`Error: ... - 0 - 2775`): the address is the object's base plus the offset (`0x10000 + 2775`);
most errors also print it in hex. Group the errors by instruction before fixing anything.

**An instruction form SR does not translate.** SR knows most mnemonics but leaves some operand
combinations as empty branches of `SR/SR/SR_full_llasm_instr.c` (the high byte registers `ah`..`bh`,
an 8-bit or 16-bit memory operand, an immediate). Such a form is fixed in SR, by the pattern of the
neighbouring branch (`not r8h`, `test r8h, r8l`, `rcl` by 1 of a byte, word or memory operand,
`adc`/`sbb r16, mem16|imm`, `idiv r8|mem8`, `imul mem8` are done so): every
later game gains. Check the flags: OF is bit 11, CF bit 0, so a byte's bit 7 is shifted left to OF,
not right.

**What SR cannot know** goes into the game's `instruction_replacements.sci`
(`loc_X,<length>,;<the original, as a comment>|<llasm>|<llasm>...`):

- 16-bit addressing (`[bx]`, `[si+2]`) in 32-bit code: SR has no 16-bit addresses. A read of the BIOS
  data area through a real-mode segment (the video mode at 0040:0049, the columns, the rows) becomes
  the constant the port's world has (text mode 3, 80, 24); a field of a structure addressed by `si`
  becomes `[esi+N]`. Write down the assumption and check it when the code runs.
- `call bx` (a 16-bit near pointer): `and tmp1, ebx, 0xffff|ACTION_CALL|PUSH loc_<next>|tcall tmp1|endp|proc loc_<next>`.
  The return point becomes a procedure of its own, so declare it with `proc` at the end.
- `lsl eax, eax` (the limit of DS): `load eax, Game_DosDsLimit, 4`.
- WATCOM's library also brings system instructions: the setup of its FPU emulator and the FPU check
  of the start-up (`smsw [m]`, `sidt [esp]`, `mov r32, cr0`, `mov cr0, r32`). SR stops at them as
  `unknown flags` or `llasm instruction error`. Replace every one, a read of cr0 too:
  - `smsw`: `mov tmp1, 0x0013|store16 tmp1, loc_X, 2`;
  - `sidt`: `mov tmp1, 0x07ff|store16 tmp1, esp, 4|load tmp2, Game_DosIdtBase, 4|add tmpadr, esp, 2|store tmp2, tmpadr, 2`;
  - `mov ecx, cr0`: `load ecx, Game_DosCr0, 4`;
  - `mov cr0, ecx`: `store ecx, Game_DosCr0, 4`.

  The address of `smsw` is the one SR's error prints (`[loc_7D0E6]`), not the raw bytes.
- `iretw` of a real-mode handler the port never runs: the body of `iretd`.
- A `cmp` or `test` right before a `jcc` is fused by SR into one conditional jump on `tmpcnd`: a
  replacement of the `cmp` writes `tmpcnd` (`sub tmpcnd, tmp1, 0xffff`), not `eflags`, and the
  `jcc` still needs its `instruction_flags.sci` line.
- llasm has no `movzx`: `and`, `load16z`, `ext8s`, `ins16` and the like do it.
- A replacement that calls the runtime (`call Game_Something args`) needs the function declared
  (`func` in `runtime/dos/llasm/dos.llinc` for the SDK's, in `game/llasm/` for the game's).

**`unknown flags on start of region`.** A region (code after a call, a jump target) reads flags
that SR does not see being set there: most often a callee returns a result in CF or ZF, and the
caller's `jae`/`jnz` reads it after the call. `tools/analysis/sr_llcode.py flags` repeats SR and
writes `loc_<return point>,0x00,<mask>` lines into `instruction_flags.sci` until SR passes or a
region it cannot explain. The format is `loc_X,<flags this instruction must compute>,<flags taken as
known before it>`, the masks of SR's `FL_*` (CF 01h, ZF 08h, all six 3Fh).

**`unknown weak flags on start of region`** are warnings: look at each once (a `pushf`/`popf`
pair around an `adc`, flags kept for a later instruction) and write down why it is right.

**WATCOM's `int386x()`** calls a stub of a table of 256 `int N / ret` with a `ret`
(`lea eax, [esi*3+table]`, `push eax`, `ret`), and after the call stores CF with `sbb eax, eax`.
SR needs `int_stub_table=yes` in `SR.cfg` (it finds the table and gives that `lea` a stub of its
own). SR still sees no flags through that return, and `sr_llcode.py flags` cannot explain the region
(`cannot explain the flags 0x1`, a run of `push`es and a `call`). The line is written by hand, at
the start of `int386x` (the region SR names): `loc_X,0x00,0x01 ; sbb eax,eax after int386x's int`.
The segment-register variant (`pushfd` after its call) gives a weak-flags warning, which is right.

**Hundreds of `reading inside of instruction`, from a few routines**: self-modifying code. A setup
routine writes the immediates and displacements of an inner loop (`mov [loc_582E3], esi`), and then
the loop runs with them (`imul eax, eax, 0`, `add esi, 0`, `mov ah, [eax+0]`, the zeros being
placeholders). SR has no code bytes in memory, so the writes go nowhere. Put
`self_modifying_code=yes` into `SR.cfg`:

- each instruction written into gets a copy in the data segment `smc` (`smc_<address>`);
- the writes, and the pointers in tables to such an operand (`daddr`), go to the copy;
- the instruction reads its operand from the copy: a written immediate becomes a memory operand,
  and a written displacement is loaded and added to the address.

SR lists them on stderr (`Self-modifying code: loc_X, bytes ... written`). It handles `mov`, `add`,
`sub`, `adc`, `sbb`, `and`, `or`, `xor`, `cmp`, `test` and `push` with an immediate of the operand's
size, `imul r32, r32, imm32`, and a disp32 of any memory operand. Any other written byte stops SR
(`Error: self-modifying code: SR cannot read byte N of loc_X`) and needs a replacement.

A table of patch sites can point into routines SR never decoded. Its entries then show up as
`daddr` candidates in the middle of instructions (6.); they are operands, not code entries. If no
reference to such a routine's entry exists, the routine and its setup are dead code (another
resolution, another mode) and can stay data.

## 5. llasm: `scons stage=bin`

SR passed, `llasm.py` and LLVM now read its output. An error of `llasm.py` (`Wrong instruction
parameter`, `Unknown word`, `Wrong number of instruction parameters`) names the line of
`build/gen/<module>/llasm/*.llinc`: a register name that is not llasm's (`bx` of a 16-bit address), a
replacement with a word llasm lacks, or data that SR took for code (`add al, [eax]` is two zero bytes).

## 6. The first run

`make distrib-data`, then `./run-sdl.sh --headless=N` (screenshots in `build/shots/sdl/`). An
`unimplemented interrupt` line names a service the DOS runtime lacks (the runtime answers the cursor
of INT 10h AH=01h-03h and the joystick of INT 15h AH=84h; it did not always).

**A crash that jumps into data or into the heap** is, most of the time, code SR never decoded: a
routine whose address the program only stores (`mov ecx, loc_X`, a field of a structure, a table of
handlers) and calls later through a pointer. SR left it as data (`dlabel loc_X`), so the pointer
leads to bytes that are not the port's code.

- `sr_llcode.py candidates`: the labels of code that SR wrote as data and whose address the program
  takes (`mov reg, loc_X`, `PUSH loc_X`, `daddr loc_X`).
- `sr_llcode.py add FILE` adds them one at a time (answering the flags, dropping what SR or
  `llasm.py` rejects), `sr_llcode.py grow` repeats it until nothing new; `bisect` finds the lines SR
  fails on. A routine of a driver called after `clc`/`stc` reads CF on entry: `add` writes its
  `loc_X,0x00,0x01` line.
- These are candidates. SR decodes data without a word when the list says it is code: a jump table
  inside a routine, a table of offsets, a table of 16-bit values read with `mov ax, [ebx+loc_X]`.
  Look at the bytes of every candidate (`exe_dis.py`) before `add`, and do not `grow` blindly. A
  `mov reg, loc_X` also takes the address of a string (`????0`), a table of floating-point constants,
  or a variable right before a handler (the old vector of a chained interrupt, `dd 0` followed by
  `jmp far [old]`). A `daddr` also points at an entry of a jump table or at a written operand
  (self-modifying code, 4.).
  The port's copy of those bytes then holds code, and the game goes wrong with no error (wrong notes,
  wrong positions). SR warns (`Warning: loc_X is in fixup_interpret_as_code.sci, but loc_Y reads or
  writes it as memory`, also on the build's stderr) and `tools/analysis/sr_codecheck.py` checks the
  list; never add candidates in bulk without them, and keep the rejected ones in `research/`.

Ways to find the place of a crash:

- `LLASM_SYMBOLS=1` (delete `build/obj/ir/*.bc` first): every procedure and every label of the data
  keeps its `loc_` name for `nm` and `gdb`. The crash report prints the binary and the offset of the
  address the program jumped to (`<program>-x64+0x1ccbb8`). The symbol of `nm -n` at that offset
  names it: a `D` label is a routine SR left as data, and the label is the candidate to add. Without
  the symbols, the offset in a data segment has to be counted from the segment's start, and the
  compacted output has `db N dup K` lines.
- `strace -f -e openat,read`: the last file the game read before the crash.
- gdb: a breakpoint on `loc_X` with `ignore N`, then a Python loop of `nexti` that keeps the last
  procedure names (`frame.name()`) until the signal: the chain of calls into the crash. `stepi`
  walks into SDL and libc; `record full` fails on AVX-512 code of libc.
- The guest's registers inside a procedure: the CPU structure at `$rdi`, eax at +0, ecx +4, edx +8,
  ebx +0Ch, esp +10h, ebp +14h, esi +18h, edi +1Ch. A guest address plus `pointer_offset` is the
  host's. `$bp` in gdb is a register, not a variable of the game.
- Without gdb: the process's memory through `/proc/<pid>/mem` (the base from `/proc/<pid>/maps`)
  at intervals, for an object's fields over time.
- A watchpoint on a field that goes wrong names the instruction that writes it; its `loc_` is the
  next lead.

**A service the runtime lacks.** `unimplemented interrupt`, `unimplemented DOS function` and
`$<GAME>_TRACE_INT=1` show what the game asks for. Add a missing service to the runtime, not to the
game: INT 21h AH=2Ah (the date) was added so.

**SVGA.** A game that draws in VESA's modes (`Can't set VESA mode`, INT 10h AX=4F01h/4F02h through
DPMI 0300h with a DOS block at ES:DI) says `GAME_DOS_VESA` in `Game_hooks.h`. The runtime then has a
VBE 1.2 BIOS: the 256-colour modes 100h, 101h, 103h and 105h, a 2 MB frame seen through the 64 KB
window at A0000h, banks (4F05h), the line length (4F06h) and the display start (4F07h). It is off by
default because a game that only probes VESA would change its modes: one of the ports sets 101h
for its language screen when VBE answers.

**Mode X.** A low resolution is often mode 13h unchained: the game clears Chain 4 (sequencer
index 4 = 06h), draws on four planes through the Map Mask, flips pages through the CRTC's start
address. Without it the picture is four squeezed copies side by side. The runtime follows the
Memory Mode register by itself; what it cannot follow is a copy through the VGA's latches (write
mode 1, or a Bit Mask of 0, then `rep movsb` from video memory to video memory): the translated
code reads and writes one plane. Find the copies (a call that sets the Bit Mask to 0 before a
`rep movsb` between page addresses; the runtime says `a VGA latch copy without Game_DosVgaCopy` when
one ran) and replace each `rep movsb` with a call:

    loc_X,2,;rep movsb: a copy between pages through the latches|call Game_DosVgaCopy edi, esi, ecx|add edi, edi, ecx|add esi, esi, ecx|mov ecx, 0

**The joystick.** The game port (201h) and INT 15h AH=84h read the host's first gamepad
(`Game_dosjoy.c`): an axis is 16 reads of the port at one end, 2015 at the other, 1015 at the
centre, a scale of the port's (the counts of a real stick depend on the machine). A game that
calibrates when the player picks the joystick takes any scale; one that reads a calibration from a
file of its disc (X and Y at the ends and the centre) puts the stick off the centre, often against
an edge, until the player calibrates it from the game's menu. Give that game a file made for the
port's scale (`extra_files=` in `game/distrib.cfg`, like any file the install writes). A game finds
the joystick once, at its start: a pad plugged in later is not seen, as on DOS.

**The mouse after a mode change.** The driver sets its range back to the mode's virtual screen when
INT 10h sets a mode (640 wide for a 320-pixel mode), as real drivers and DOSBox do. A game may set
the range first and the mode after: with the old range kept, its pointer stops halfway across.

## 7. Flags across a `ret`

SR does not carry flags from a `ret` to its caller unless told. A callee that returns its answer in
ZF or CF (`xor dx, dx` / `dec dx` before the `ret`, a `stc`) and a caller that reads it after the
call (`jnz`) go wrong in silence: the caller sees the flags of some instruction before the call. The
game runs, but a decision in it is wrong (an object that never lands, a collision that never
happens).

    python3 SR-SDK/tools/analysis/retflags.py build/gen/<module>/llasm/seg01_code.llinc \
        build/input/<module>/<module>.EXE game/recomp/<module>/<variant>/llasm/instruction_flags.sci

prints `loc_<ret>,0x3F,0x00` lines (PE and LE). A `ret` right after `stc` or `clc` needs no line under
SR. Its stderr lists the indirect calls whose return point reads the flags: their targets need lines
it cannot find (`mov [reg+N], loc_X`, the fixups, a trace).

## 8. A manual-lookup check

A check that asks a word of the manual keeps its table of answers in the executable (the page, the
line and the word, often in several languages), and picks the question at random. A headless run
cannot answer it with fixed keys. Find the table and the compare (`strings`, the code around the
prompt's text) and type the answer; while the port is being made, a replacement at the compare may
take the table's own word for the typed one. Say in the line's comment what it does; whether it
stays in the port is a decision of its own.

## 9. Sound

- `GAME_DOS_SOUNDBLASTER`: a Sound Blaster 2.0 for a game that drives the card itself
  (`BLASTER=A220 I5 D1 T1`). The game's settings file chooses the card; a game set to no sound plays
  none, so check what its default file says.
- A sound driver the game reads from a file (a `.BIN` of its own) is 16-bit code that the
  game loads into a DOS block, points a real-mode vector at (DPMI 0201h) and calls with INT n from
  protected mode. SR cannot translate it (it is not in the executable, and the game patches it), so
  `Game_dosreal.c` (`GAME_DOS_REALMODE`) runs it on an interpreter of the 8086: the call, its
  INT 21h AH=25h on the card's IRQ, and the IRQ itself, which reaches the driver's real-mode handler when the game's protected-mode handler of that
  IRQ passes it on (`jmp far` to the old vector: the null ISR of `Game-llasm.llasm` marks it,
  `Game_DosHardwareInterrupt`). The driver sees the card through the same ports as the translated
  code. Its settings file must name the card the runtime has (port, IRQ, DMA): make it as the game's
  installer would and give it with `extra_files=` in `distrib.cfg`.
- A driver with a small double buffer (two blocks of 20 ms) fills the block the card has just played,
  at the IRQ. The card has to take its samples when the time comes and raise the IRQ at the end of the
  block (`Game_dossb.c` does it on the game's thread), not when the host's output asks: the output
  asks in pieces of its own (23 ms on SDL_mixer), bigger than a block, and would read blocks the driver
  has not filled yet (crackle, broken effects). A driver may also change the rate after it starts the
  card (D0h, 40h, D4h).
- Such a driver usually plays its music on the card's FM chip (`out` to base+8/9 or 388h/389h):
  `GAME_DOS_SB_OPL` gives `GAME_DOS_SOUNDBLASTER` that chip. Without it the effects play and the
  music is silent. `$<GAME>_TRACE_PORTS` shows which ports the driver writes.
- A sound library linked into the game (Miles AIL 2/3, DSMI) is often better run on the host than
  emulated. Typical reasons: its mixer reads tables through a selector with a base of its own
  (`mov es, [...]`, `mov ax, [es:ebx+ebx]`), and SR takes every segment as flat, so it mixes silence;
  its clock is the card's DMA position, which the emulation hardly moves. Replace the entries of the
  library's driver table that mix and that tell the time, and keep the rest (the module player, the
  game's effects) as the game's code. `GAME_DOS_DSMI` (`runtime/dos/Game_dosdsmi.c`) is one such
  replacement; its head says how to find the entries and the variables in a new game.
- A stream that the host plays must never run dry: let the library's clock be the stream's queue
  (how much it lacks to a target latency plus the device's buffers), not the wall clock, and never
  drop what was mixed.
- A sample loop: the loop end is past the last sample of the loop, and the interpolation at the seam
  takes the loop's first sample. A sample lost at each turn of a short loop is a rasp, not a click.
- `$<GAME>_TRACE_SOUND`, `$<GAME>_TRACE_PORTS`, `$<GAME>_TRACE_INT` trace the runtime; with
  `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file>` the output is raw S16 to look at (gaps, clipping,
  jumps) with a script. On a desktop the card's stream has a device of its own, and the two devices
  write the same file: record with `GAME_AUDIO_ON_MIXER 1` in `Game_hooks.h` for the time of it.
  Hearing it stays the user's check.

## 10. Headless runs and keys

- `--headless=N` takes N frames, not seconds; a screenshot comes every few seconds of real time.
- `<GAME>_AUTO_KEYS` / `--keys=`: `delay_s:Key[~hold_ms],...`, with SDL's key names (`Left Ctrl`,
  `Return`, `space`). Text typed too fast loses letters: a press every 0.45 s held 90 ms is safe.
- Which key starts the game from its menu (Enter may start the demo, the fire key the game) matters
  for every later check: a "level" reached through the demo is not the game.
- A cheat of the game (a word typed on the title screen) is the quickest way to a later level for a
  check; a temporary replacement that forces it is fine while debugging and goes before a commit.
- A key the game misses when tapped quickly: find what its INT 9 handler writes (a table of keys
  down, a mask, the last key pressed) and how often the code that reads it runs. A game that acts on
  a new press (up in one frame, down in the next) or on the last key pressed misses a press and a
  release that come in one frame. `Game_doskbd.c` holds each key apart from its own press and
  release (`GAME_DOS_KBD_MIN_HOLD_MS`, 60 ms); the times between different keys stay the fingers'.
  A hold counted from any key's press would hand over the releases of two keys together (an arrow
  held, then fire: the game never sees fire alone). A game that reads the keys in its own loop often
  enough takes 0, as DOSBox: the least delay. On-screen buttons give taps of 1 ms, a keyboard 16 ms
  and more; `$<GAME>_TRACE_KEYS` shows when each byte reaches the handler.

## 11. Android

`devices=...,arm64-android`, `android_id=`, `android_name=`, `make apk` (with `ANDROID_HOME`). The
on-screen buttons are `game/Game_touchlayout.h` (`touchlayout_default.h` otherwise): the controls the
manual lists, a `TOUCHGAMEPAD_KEYBOARD` button for typed words and cheats. The user tries it on a
device.

- A game that "crashes at once" may not crash: `adb shell dumpsys activity exit-info <package>`
  gives the reason (ANR, a signal), `adb logcat -b crash` the native aborts, `dumpsys dropbox --print
  data_app_anr` the stacks of an ANR, `Android/data/<package>/files/startup.txt` the steps before
  the native side starts. Hundreds of MB of data unpacked on the activity's thread were an ANR at
  the first touch; `UnpackActivity` does it on a thread of its own.
- What a thread of the port opens through its Android looper (the gyroscope's sensor, on the thread
  that pumps the events) is closed by that thread before it ends: `SDL_Quit` later reaches a looper
  that is gone, and the process aborts on the way out.

## 12. Changes to SR and the SDK

Make them in the root `SR-SDK/` (and its `SR/`), not in the game's copy; while you only try them, a
diff of the copy (`git -C SR-SDK diff`, `git -C SR-SDK/SR diff`) kept in `research/` is the record.
Before a commit: `SR-SDK/tools/regress/run_all.sh` (SR's output of its reference games must stay the
same, every port must still build and start), then every game's SDK copy is moved to the commit.

## Traps, in short

| Symptom | Cause |
|---|---|
| SR `Error` on a form of a known instruction | an empty branch of `SR_full_llasm_instr.c`: fix SR |
| `fixup operand mismatch` | a 16-bit address with a fixup: a replacement |
| a crash into data or the heap | a routine SR left as data: `sr_llcode.py`, `fixup_interpret_as_code.sci` |
| `Wrong number of instruction parameters` from `llasm.py` | data in `fixup_interpret_as_code.sci` |
| wrong values in a table, no error | a table in `fixup_interpret_as_code.sci`: `sr_codecheck.py`, SR's warning |
| a decision of the game goes wrong, no crash | flags returned through `ret`: `retflags.py` |
| silence from a sound library | a based selector or the card's DMA clock: mix on the host |
| hundreds of `reading inside of instruction`, from a few routines | self-modifying code: `self_modifying_code=yes` in `SR.cfg` |
| `unknown flags on start of region` after `push`es and a `call`, a `sbb eax, eax` after it | WATCOM's `int386x()`: `int_stub_table=yes` and a `loc_X,0x00,0x01` line by hand |
| `llasm instruction error - mov cr0, ...`, `smsw`, `sidt` | WATCOM's FPU emulator and FPU check: replacements on `Game_DosCr0`, `Game_DosIdtBase` |
| `Can't set VESA mode`, `unimplemented interrupt 10 (AX=4f01)` | `GAME_DOS_VESA` |
| four squeezed copies of the picture side by side | mode X (Chain 4 cleared): the runtime has it; latch copies go to `Game_DosVgaCopy` |
| a crash writing into `.text` at a handler the game patches (`mov [loc_X], ds`) | a variable (the old vector) listed in `fixup_interpret_as_code.sci` before the handler: list only the handler |
| a game's sound driver in a file plays effects, no music | its music is FM: `GAME_DOS_SB_OPL` |
| a quick tap, or a tap with another key held, is lost | how the game's INT 9 handler keeps the keys; `GAME_DOS_KBD_MIN_HOLD_MS` (§10) |
| the joystick stays against an edge, the game's calibration helps | a calibration file of the disc, for the hardware of its time: one for the port's scale in `extra_files=` (§6) |
| data among the `mov`/`daddr` candidates | strings, constants, a chained handler's old vector, jump-table entries: look at the bytes |
