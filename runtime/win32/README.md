# runtime/win32: what a port of a Windows original runs on

| Path | What |
|---|---|
| `platform.h`, `X86_FS_mem.c`, `llasm/` | what every port of a Windows original needs: the guest's `fs:` (the TIB), the CPU glue (`asm-cpu.c`) |
| `winapi/` | the Win32 API layer: kernel32, user32, gdi32, DirectDraw, Direct3D, DirectInput, DirectSound, winmm, MCI, MIDI output, avifile, msvfw, the registry and the game's config file; `WinApi-runtime.c` (start-up, data and write roots, the vfs, file handles, the disc, CD audio), `Game_RuntimeGlue.c`; `winapi/llasm/` is its llasm glue. `winapi=no` of `game.cfg` leaves it out |
| `glide/` | 3dfx Glide 2 on OpenGL (`glide=yes`) |
| `gl/` | Direct3D on OpenGL (`d3d_gl=yes`) |

## Settings

A Win32 game has its `main` in `winapi/Game_winmain.c` and its settings file in
`runtime/core/Game_config.c` (`--config-file=`, else `<program>.cfg` in the game directory; `Sound`,
`Music` and the keys of the display, audio and input):

- `Display_Additional_Resolutions=off|on`, a mod: DirectDraw's list of display modes with
  1024x768, 1152x864, 1280x960, 1280x1024 and 1600x1200 after the original's 640x480 and 800x600;
- with Direct3D also `3D=direct3d|software|off` and `Display_Antialiasing=off|on|2|4|8`, the
  multisampling of the scene on OpenGL, `on` 4 samples, the game's own
  `D3DRENDERSTATE_ANTIALIAS` 4 when the key is off;
- with Glide `Display_3D_Resolution=WxH|off`, the 3D at that resolution (the picture is 4:3
  either way), `<ENVP>_3D_RESOLUTION` over it, and `Display_Antialiasing` too, the multisampling
  of the 3D, off unless set;
- an item of the port's panel in both, off/2/4/8, seen at the next frame.

## What a Win32 game provides

Besides the hooks of every port ([`../README.md`](../README.md#what-a-game-provides)),
`game/Game_hooks.h` of a Win32 game takes, all optional:

| Macro | Meaning |
|---|---|
| `GAME_CONFIG_FILENAME`, `GAME_LOG_FILE` | another name for the settings file or the log (default `<program>.cfg`, `<program>.log`) |
| `GAME_WIN_START()` | called once the winapi layer is up (a registry the game reads) |
| `GAME_WINAPI_CONFIG_FILE` | the file `ReadConfiguration` reads (default `<program>.cfg`) |
| `GAME_WINAPI_KEY_MESSAGES` | 0: the message pump does not deliver key events (default 1) |
| `GAME_WINAPI_START_DIRECTORY` | the directory the game starts in, `"D:\\"` for the disc (default the process's own) |
| `GAME_CD_VOLUME_LABEL` | the volume label of a copy of the disc in a directory (an image gives its own; default empty) |
| `GAME_CD_VOLUME_SERIAL` | the serial number of the disc's volume (default `0x0000CD01`) |
| `GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption)` | a button to answer a message box with without showing it (default 0) |
| `GAME_WINAPI_PAINT_WHEN_IDLE` | 1: the game draws in its window procedure and waits for the repaint request; the port makes the request when the program has no other message |
| `GAME_WINAPI_ACTIVATE_APP_ON_IDLE` | 1: the port sends `WM_ACTIVATEAPP` with 0 and then with 1 when the program has no other message (the game makes its picture buffer on the first and draws on the second) |
| `GAME_WINAPI_WINDOW_MESSAGES` | 1: the game's message loop looks at the window of a message: input messages carry the window's handle, `PostMessageA` delivers the `WM_USER` range |
| `GAME_WINDOW_WIDTH`, `GAME_WINDOW_HEIGHT` | both defined: `GetClientRect` and `GetWindowRect` give the game's window that size at the origin (the display mode it asks for; undefined: the stub, or the game's own function) |
| `GAME_WINAPI_GDI` | 1: the game draws into DIB sections and copies them to its window with `BitBlt`/`StretchBlt` (`WinApi-gdi.c`: device contexts, bitmaps with their pixels in the guest's memory, palettes; the window's picture, its client size, shown through OpenGL) |
| `GAME_WINAPI_WAIL32` | 1: the Miles Sound System for Win32 (`WinApi-wail32.c`, AIL 3.03, over `core/Game_ail.c`): samples from WAV images or raw PCM, played on the port's mixer, and XMIDI sequences (`soundfont.sf2` of the game directory); a game that loads `WAIL32.DLL` with `LoadLibrary` lists the functions in `GAME_EXTRA_PROCS` and its import shim |
| `GAME_DSOUND_CURSOR_BEHIND_MS` | optional: milliseconds; the play cursor of a DirectSound buffer is that far behind the mixer's, for a game that keeps its mix a fixed, long way ahead of it |
| `GAME_DSOUND_HARDWARE` | defined: DirectSound answers as a sound card's driver, not the emulation: `GetCaps` has no `DSCAPS_EMULDRIVER`, the write cursor of a buffer is the hardware's (the play cursor and what the next mix takes, not the end of what the game wrote) and `DSSCL_WRITEPRIMARY` lets the game write the primary buffer, played as it is (a driver that keeps its mix a little way past the write cursor) |
| `GAME_WINAPI_MIDI_TECHNOLOGY` | the `wTechnology` of the one MIDI output device (`midiOutGetDevCapsA`, `WinApi-midi.c`), the synthesizer of `Game_midi.c` with `soundfont.sf2`: by it a game picks its arrangements; default `MOD_SWSYNTH` (7), as Windows' software synthesizer; 2 `MOD_SYNTH`, 4 `MOD_FMSYNTH`, 6 `MOD_WAVETABLE` |
| `GAME_WINAPI_TIMER_EVENTS` | 1: the callback of `timeSetEvent` runs, from the game's message pump (default 0: it never runs) |
| `GAME_GUEST_PUMP` | 1: the thread that draws pumps SDL's events (a Win32 game's own thread, a DOS game's flip thread), as on Android (the default there; elsewhere 0: the host's main thread pumps); the host's main thread always reads them |
| `GAME_EARLY_GL_WINDOW` | defined: the window of Direct3D on OpenGL comes up at the start of the program, not after the game's loading |
| `GAME_DINPUT_EXCLUSIVE_MOUSE` | defined: the game draws its cursor from DirectInput's deltas: the host pointer is hidden and held in the window from the first click in it until the focus is lost |
| `GAME_WINAPI_ADDITIONAL_RESOLUTIONS` | 1: the game picks its mode from DirectDraw's list in its menu: the port's panel has the item of `Display_Additional_Resolutions` (seen at the next start) |
| `GAME_DIRECT3D` | 1: the game uses Direct3D, so `IDirectDraw::QueryInterface` gives `IDirect3D`; the log says which renderer runs |
| `GAME_D3D_FOG_HIDDEN` | optional: an expression, non-zero while a toggle of the port hides the fog the program turns on (read at every draw; Direct3D on OpenGL) |
| `GAME_D3D_FOG_TABLE_W` | optional: an expression, the 64 bytes of a fog table as Glide's `grFogTable` (by W, 0 no fog, 255 all) or `NULL`: with a table the fog of a vertex comes from its W, not from its specular alpha, and is drawn whatever `RS_FOGENABLE` says (a driver that has such a table but looks it up by z) |
| `GAME_GLIDE_FOG_HIDDEN` | optional: an expression, non-zero while a toggle of the port hides the fog of a Glide program |
| `GAME_GLIDE_SLI` | optional: 1, `grSstQueryHardware` reports a Voodoo2 SLI pair (`sliDetect`): a program that sizes its modes by the frame buffer memory gets twice as much (a depth buffer at 1024x768) |
| `GAME_GLIDE_3D_RESOLUTION_ITEM` | optional: 1, the port's panel has the item "3D resolution" (`Display_3D_Resolution`: off, 640x480 … 1920x1440), the colour buffers made again at that size at the next `grBufferSwap` |
| `GAME_COMMAND_LINE_ARGS` | the game's own switches, always first on its command line, before the player's arguments |
| `GAME_DIRECT3D_ARG` | a switch added to the game's command line when Direct3D is on |
| `GAME_DIRECT3D_ANTIALIAS_ARG` | the game's own switch of the antialiasing (it sets `D3DRENDERSTATE_ANTIALIAS`), added to its command line when Direct3D is on and `Display_Antialiasing` of the settings file (or `<ENVP>_ANTIALIASING`) asks for it |
| `GAME_GUEST_LOW_MEMORY()` | the low memory of the guest window: how much of the guest's address space from `pointer_offset` up has to stay free of the host and of the guest's heap (default 1 MiB + 64 KiB, at most 64 MiB) |
| `GAME_GUEST_PAGE0` | defined: the guest's page at address 0 reads as zeros and takes writes, as the DOS memory Windows 9x maps there |
| `GAME_PROC_LOOKUP(name)` | `GetProcAddress` asks the game first: the guest address of the procedure, or 0 for the layer's table |
| `GAME_EXTRA_PROCS(X)` | more exports for the `GetProcAddress` table of the layer: `X(DirectInputCreateA) X(...)` |
| `GAME_MODULE_PATH_SEP` | the path separator of `GetModuleFileNameA`, a string (default `"/"`) |

The game brings `WinApi-game.h`, the prototypes of the imports of its executable, and
`WinApi-game*.c`, their implementation, which `WinApi-guest.h` (calling back into the
guest) completes; `tools/gen_import_shim.py` makes the llasm bridges of the imports
(`game/import_args.tsv`).

## Threads and events

Only the event loop's thread pumps SDL's events, so the windows are made
and changed on it: `Game_RunOnMain` (`Game_thread.h`) runs a call there and waits for it,
and `Game_WindowCreate`, `Game_ShowWindow`, `Game_HideWindow`, `Game_ShowCursor`,
`Game_SetCursor`, `Game_SetRelativeMouseMode` and `Game_WarpMouse` go through it (on
Windows a window's messages reach only the thread that made it; SDL's X11 and Wayland
drivers wait for the server's answer on the connection the event loop pumps). The event
loop also reads all of SDL's queue: the port's panel takes its own, and what is the
game's goes as it came to the input queue of the window's thread (`Winapi_RouteEvent`,
`WinApi-user32.c`), where the ticks of `timeSetEvent` go too; the message pump of that
thread makes window messages of them and waits on the queue's condition variable.
