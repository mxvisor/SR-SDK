# runtime/dos: the runtime of a DOS/4GW game

`Game_DosRuntimeInit`/`Deinit` (`Game_dosrt.c`) start and end it, and it owns the
procedures the translated code calls -- `X86_InterruptProcedure` (`Game_dosint.c`: the vectors,
the game's handlers run as hardware interrupts, INT 08h, 16h, 1Ah, 21h, 2Fh and 31h),
`X86_InPortProcedure`/`X86_OutPortProcedure` and `X86_ReadMemProcedure`/`X86_WriteMemProcedure`
(`Game_dosrt.c`: the PIT, the keyboard, the VGA DAC and retrace, the segment-override accesses).
The game's own services (INT, a memory dump) and ports come through hooks
(`GAME_DOS_INTERRUPT`, `GAME_DUMP_GUEST_MEMORY`, `GAME_DOS_PORT_IN`/`GAME_DOS_PORT_OUT`
in `Game_hooks.h`). It brings its sound, its llasm glue (with the handler every vector starts
with) and whatever else of the files below it replaces.

`Game_dosmain.c` is the `main` of a DOS game's port, `Game_dostimer.c` its
`SR_CheckTimer`; a game with its own version of one lists it in `runtime_skip` and brings the
functions the file's head names.

[`PORTING_DOS.md`](../../PORTING_DOS.md) walks through a DOS port.

## What a DOS game provides

Besides the hooks of every port ([`../README.md`](../README.md#what-a-game-provides)),
`game/Game_hooks.h` of a DOS game takes, all optional:

| Macro | Meaning |
|---|---|
| `GAME_ENTRY` | the llasm symbol the game starts at (`Game-asm.c`): default `c_start_`, the program's entry point, which SR exports by itself; `c_main_` is the game's C `main` named by `main_` in `global_aliases.sci`, for a port that skips the startup code |
| `GAME_PROGRAM_MEMORY`, `GAME_PROGRAM_STACK` | `Game-asm.c`, a game started at its entry point: the size of the program's memory block (default 64 MiB, less if the guest window has no room) and of the stack in it (1 MiB); the startup code grows its heap in the block (INT 21h EAX=80004800h of the flat model it is told it runs under: `Game_dosint.c`, AH=30h) |
| `GAME_ARGS(argc, argv)` | `Game_dosmain.c`: takes the port's own options out of `argv`, returns the new `argc` (default `Game_ReadArgs`: `--config-file=`) |
| `GAME_CONFIG()` | the port's configuration, read after the options and the defaults of `Init_Display`/`Init_Audio`/`Init_Input` (default `Game_ReadConfig`, `runtime/core/Game_config.c`: `--config-file=`, else `<program>.cfg` in the data directory or the write root; `Sound`, `Music` and the keys of the display, audio and input) |
| `GAME_CONFIG_KEY(key, value)` | a key of the settings file that is the game's (`Game_config.c`, DOS and Win32); nonzero: taken |
| `GAME_DISPLAY_INIT()`, `GAME_DISPLAY_INIT2()`, `GAME_DISPLAY_CLEANUP()`, `GAME_DISPLAY_DESTROY()` | `core/display/pc.c`, the default display: after the defaults of `Init_Display`; at the end of `Init_Display2` (may change `Render_Width`, `Render_Height`, `Display_Flip_Procedure`); in `Cleanup_Display`; in `Game_Display_Destroy` |
| `GAME_DISPLAY_WINDOW_FLAGS`, `GAME_DISPLAY_FLIP()` | the same: more `SDL_WINDOW_*` flags; the start of `Game_Display_Flip`, on the flip thread (nonzero: the game showed the frame itself) |
| `GAME_DISPLAY_MAX_PIXELS` | the same: the most pixels of the frame, for a `Display_Flip_Procedure` that changes `Render_Width` and `Render_Height` as the game runs (default 1024×768) |
| `GAME_RUNTIME_INIT()` / `GAME_RUNTIME_DEINIT()` | the game's own part of the runtime (nonzero: failed). `Game_dosmain.c` makes the DOS world of every game first: conventional memory at its own addresses (`Game_dosmem.h`: the PSP with the command line and its environment at 10000h, the VGA window at A0000h as `Game_FrameBuffer`), the disc and the music; the game's end comes before the world's |
| `GAME_DOS_ENV` | the variables of the DOS environment, `"NAME=VALUE"` strings separated by commas; WATCOM's startup builds the array of their addresses itself; for a game that starts at its main (`GAME_ENTRY`), which skips that, it is `Game_DosEnviron`, and `instruction_replacements.sci` points the C library's `getenv` at it |
| `GAME_DOS_SOUNDBLASTER` | the Sound Blaster 2.0 of `Game_dossb.c` (DSP, 8-bit DMA, the IRQ from `SR_CheckTimer`), with `BLASTER=A220 I5 D1 T1` in the environment; `GAME_SB_BASE`, `GAME_SB_IRQ`, `GAME_SB_DMA` change the card. The IRQ handler the game installs has to be translated code: SR finds a handler given to INT 21h AH=25h by itself ([`scons/README.md`](../../scons/README.md#srs-inputs)) |
| `GAME_DOS_SB_OPL` | with `GAME_DOS_SOUNDBLASTER`: the card's FM chip, at base+8/9 and at the Ad Lib's 388h/389h (the OPL2 of `core/Game_opl.c`, opened at the first write; the status with the chip's two timers), for a driver that plays its music on the card's FM |
| `GAME_DOS_REALMODE` | `Game_dosreal.c`: an interpreter of the 8086 (with the 80186's additions) for the 16-bit code a game loads into its DOS memory at run time and calls through a real-mode vector (a sound driver read from a file), which SR cannot translate: DPMI 0201h, an INT that protected mode does not handle, DPMI 0300h, and the card's IRQ the game's handler passes on. Without it those find no real-mode handler, as before |
| `GAME_DOS_KBD_MIN_HOLD_MS` | `Game_doskbd.c`: how long (ms) a key's press reaches the game's INT 9 handler before its release, and its release before its next press (default 60: a tap the event loop hands over in one batch is seen by a game that reads the keys once a frame); counted for each key on its own, so the keys of two fingers keep their times. 0 hands the bytes over as they come, as DOSBox does |
| `GAME_DOS_VESA` | the VESA BIOS of `Game_dosvga.c` (INT 10h AH=4Fh, VBE 1.2): the 256-colour modes 100h, 101h, 103h and 105h, banked through the window at A0000h (4F05h, no window function), with 4F06h and 4F07h; the blocks of 4F00h and 4F01h at ES:DI of a real-mode call (DPMI 0300h). Without it AH=4Fh fails as before: a game that only probes VESA keeps its VGA modes |
| `GAME_DOS_DSMI` | `Game_dosdsmi.c`: DSMI's MCP (Otto Chrons' Multi Channel Player 2.6) mixed on the host: the game's `instruction_replacements.sci` puts `call Game_DsmiMcpGetDelta` at MCP's getdelta and `call Game_DsmiMcpPoll microseconds, vars, order` at its poll (CDI entries 38h and 34h; the head of the file says how to find them and MCP's variables); the rest of DSMI (AMP, the CDI, the game's effects) stays the game's |
| `GAME_LOG_FILE` | Windows: the name of the log file of a program without a console (default `<program>.log`) |
| `GAME_MIXER_CHANNELS` | `core/Game_audio.c`, the SDL_mixer device of every port (DOS and Win32): its channels (default SDL_mixer's 8) |
| `GAME_AUDIO_ON_MIXER` | `core/Game_audiostream.c`: 1 plays the PCM streams (the Sound Blaster, DirectSound, a game's own driver) in SDL_mixer's output after its mix, 0 on devices of their own; the default is 1 on Android, where SDL has one playback device, and 0 elsewhere |
| `GAME_DOS_INTERRUPT(number, regs)` | the game's own INT services, tried first (nonzero: served) |
| `GAME_DOS_REALMODE_INTERRUPT(vector, regs, es)` | DPMI 0300h (a real-mode interrupt) for a vector the shared services do not know, on the copy of the call structure; `es` is the structure's ES (nonzero: served; otherwise INT 10h, 1Ah, 21h and 2Fh go to the shared services and every other vector fails with CF set) |
| `GAME_DUMP_GUEST_MEMORY()` | called when the game opens its error log: a debug dump |
| `GAME_DOS_AIL_PERIOD_POINTER()` / `GAME_DOS_AIL_SAMPLE_NAME(address)` / `GAME_DOS_AIL_SERVICE_US` | `Game_dosail.c`: where the game reads AIL's current PIT period (a dword of microseconds, kept current); the name of the sound at a guest address, for `$<GAME>_TRACE_SOUND`; the period of the DIG driver's own service timer (default 5000 us, SB16.DIG's) |
| `GAME_DOS_TIMER_TICK()` | what the PIT's channel 0 interrupt runs in `Game_dostimer.c`'s `SR_CheckTimer`, in place of `Game_DosTimerTick` (the guest's INT 8 and INT 1Ch handlers): a sound driver's timer service and then the game's handler |
| `GAME_DOS_MOUSE_MARGIN_Y(width, height)` | `Game_dosmouse.c`: the rows of the picture (the mode's `width` × `height`) above and below the part the mouse's range covers, for a game that draws a smaller screen in the middle and sets the range to it; a touch and the gyroscope then put the pointer under the finger (default 0: the range covers the picture) |

## Files

- `Game_dosmain.c`: the `main` of a DOS port;
- `Game_dostimer.c`: `SR_CheckTimer`, the PIT's interrupt and the card's IRQ;
- `Game_dosrt.c`, `.h`: start and end of a DOS game's runtime, the I/O ports, the
  segment-override accesses (PSP, the last DOS block, a DOS address);
- `Game_dosint.c`, `.h`: the interrupt vectors, the game's handlers run as hardware
  interrupts, AIL timer callbacks and a driver's far callbacks with their registers (the
  mouse's event handler: `RunProcStack` of `llasm/Game-llasm.llasm`, which takes the target from
  the stack, so ECX is an argument), the software interrupts every DOS game here shares
  (INT 21h and DPMI as far as a flat game asks them), after the game's `Game_DosInterrupt`;
- `Game_dosregs.h`: the registers of an interrupt and their 8- and 16-bit parts;
- `Game_dosvga.c`, `.h`: the VGA DAC (3C7h-3C9h), Input Status #1 (3DAh), the Map Mask and the
  graphics controller, and mode 12h (640x480, 16 colours, four planes: the VGA window holds the
  plane the Map Mask addresses, the picture is made of the planes through the attribute palette),
  and the VESA BIOS of `GAME_DOS_VESA` (VBE 1.2: modes 100h, 101h, 103h, 105h in 256 colours,
  a frame of 2 MB that the window at A0000h shows 64 KB of, the bank of 4F05h, the line of 4F06h
  and the display start of 4F07h);
- `Game_dosmem.c`, `.h`: DOS conventional memory of a DOS4GW game (DPMI blocks, real-mode
  buffers, the PSP) at its own addresses in the low memory of the guest window (zeros below
  the PSP at 10000h, the VGA window at A0000h), and the memory block of a
  program started at its entry point (`Game_DosProgramInit`: stack and heap above the image,
  grown by INT 21h AH=4Ah or EAX=80004800h); the blocks of DPMI 0100h are taken first fit
  and given back (0101h), resized in place (0102h);
- `Game_dosexec.c`, `.h`: INT 21h AH=4Bh, the other programs of the game (`exe_modules=` of
  `game.cfg`, modules of the same binary): run on the game's CPU with a PSP and a program block
  of their own, also behind `COMMAND.COM /C`; their AH=4Ch comes back, AH=4Dh gives the code;
- `Game_dosfile.c`, `.h`: the file functions of INT 21h (open, create, read, write, seek,
  close, FindFirst/FindNext, the DTA, the devices CON and NUL);
- `Game_doskbd.c`, `.h`: the keyboard: the controller's bytes (port 60h, E0 before a grey
  key) delivered to the game's INT 9 handler, each key held at least `GAME_DOS_KBD_MIN_HOLD_MS`
  (a tap again while its press still waits is left out: quick taps do not pile up), the BIOS
  buffer behind INT 16h, INT 21h 08h/0Bh and WATCOM's kbhit/getch;
- `Game_dosmouse.c`, `.h`: the Microsoft mouse driver of INT 33h with AH = 0: the host mouse in
  the 4:3 picture, polled, or the button events and the motion handed to the game's event handler
  of function 0Ch as an interrupt of the mouse (SR finds no such handler,
  `fixup_interpret_as_code.sci` lists it); the game draws its pointer, so the host's is hidden
  over the window once it uses the driver;
- `Game_doscd.c`, `.h`: MSCDEX on drive D:: the install and drive checks, the table of
  contents and the audio status (IOCTL input), PLAY/STOP/RESUME AUDIO through `core/Game_music.c`;
- `Game_dospit.c`, `.h`: PIT channel 0 on the host clock: the reload value and the counter
  of ports 40h/43h, when IRQ 0 is due (`Game_DosPitRun`), the BIOS ticks;
- `Game_dosail.c`, `.h`: the Miles AIL 3.x a game was linked with, over `core/Game_ail.c`: its
  timer service on the PIT (`Game_AIL_TimerInterrupt` from the game's `GAME_DOS_TIMER_TICK()`),
  the digital driver (`$<GAME>_NOSOUND`) and its samples; the game's `external_procedures.sci`
  routes AIL's entry points to the `SR_AIL_*` of `llasm/ail.llinc`, which its own llasm includes;
- `Game_dosail2.c`, `.h`: the drivers of the Miles AIL/32 2.x a game was linked with, by the
  numbers of the driver functions: a digital one (one .VOC at a time, over `core/Game_ail.c`) and
  an XMIDI one: the Ad Lib driver of AIL 2.14 (`Game_dosail2fm.c`, `.h`: its interpreter
  and its synthesizer ported to C, on the OPL2 of `core/Game_opl.c`, with the timbres the
  game loads from its `.AD` file), one at a time; the game's `external_procedures.sci`
  routes its `AIL_register_driver` and the dispatcher of the driver functions to
  `SR_AIL2_register_driver` and `SR_AIL2_call_driver` (`llasm/ail2.llinc`, in every DOS game's llasm);
- `Game_dosdsmi.c`: DSMI's MCP on the host (`GAME_DOS_DSMI`): its voices mixed into a stream of
  the port, its time the stream's, for a game whose MCP mixes through a based ES selector;
- `Game_dossb.c`, `.h`: the Sound Blaster 2.0 of `GAME_DOS_SOUNDBLASTER` (the DSP, the 8-bit
  DMA, the IRQ at the end of a block, the mixer's volume), with the FM chip of `GAME_DOS_SB_OPL`;
  the card plays on the host's clock, as the PIT, and its output keeps a steady margin by the
  speed it is read at ([`PORTING_DOS.md`](../../PORTING_DOS.md));
- `Game_dosjoy.c`, `.h`: the game port (201h) and the BIOS joystick (INT 15h AH=84h) on the host's
  first gamepad (`core/Game_pad.c`): the left stick and the D-pad are joystick A, the right stick B, the
  buttons A, B, X, Y the four buttons; without a pad, a port with nothing plugged in;
- `Game_dosreal.c`, `.h`: real-mode code in the DOS memory (`GAME_DOS_REALMODE`), on an
  interpreter of the 8086 and the 80186 of its own: a driver the game loads into a DOS block
  and calls through a real-mode vector (DPMI 0201h, an INT the protected-mode code does not
  handle, DPMI 0300h), its handler of the card's IRQ
  (INT 21h AH=25h of the driver) reached when the game's protected-mode handler passes the IRQ
  on (`Game_DosHardwareInterrupt` of `Game_dosint.c`), and the ports of the translated code;
- `llasm/`: the shared llasm of the DOS ports: `Game-llasm.llasm`, the macros and the calls of
  SR's output, `ail.llinc` and `ail2.llinc` (AIL's entry points), `asm-cpu.c`.
