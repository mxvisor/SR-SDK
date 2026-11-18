# runtime: what a port runs on

The runtime is what the original's code calls instead of DOS, DOS/4GW, Windows and the PC's
hardware. It is built with the game's recompiled code into one program
([`scons/README.md`](../scons/README.md#what-is-built)).

| Directory | What |
|---|---|
| `core/` | threads, memory, the window and the frontends, the OSD, and what the DOS and the Win32 ports share: files, the disc, sound, music, input ([`core/README.md`](core/README.md)) |
| `dos/` | the runtime of a DOS/4GW game ([`dos/README.md`](dos/README.md)) |
| `win32/` | the Win32 API layer, Glide and Direct3D on OpenGL ([`win32/README.md`](win32/README.md)) |

## The guest's memory

The base, `pointer_offset`, is the bottom of the guest window: the 2 GB the guest
sees as its addresses 0 .. 2 GB, the same on Linux, Windows and Android
(`core/Game_memory.c`). The window is reserved whole at the start (address
space only, no memory), so nothing of the host's lands in it later. From the
bottom: the guest's low memory (`GAME_GUEST_LOW_MEMORY()`), the guest's heap from
the bottom up, the host's image at 64 MB (`android/gameloader.c` loads it there on
Android), and a DOS program's own memory from the top down. What the host already
had in the window (the image, the heap of `brk`) is left out. `SR_SDK_MEMORY_MAP=1`
prints the map of the window at the start; a request that finds no room prints it
as well. The window is the only place of the guest's memory: there is no other way to
find it, and a program that cannot set the window up stops with its map. The runtime
is built for 64-bit hosts only (`PTROFS_64BIT`).

The low memory is the same for every game, DOS or Windows: reserved with no access,
so a NULL pointer faults, and the part a game needs becomes memory (zeros) with
`commit_guest_low(size)`: the DOS memory of a DOS game (`dos/Game_dosmem.c`,
the PSP at 10000h and the VGA window at A0000h at their own addresses), the data of
a `guest_data_in_memory` game, the page 0 of Windows 9x (`GAME_GUEST_PAGE0`). C code
turns a guest address into a pointer and back with `GAME_GUEST32_TO_PTR` /
`GAME_PTR_TO_GUEST32`, or `GUEST_MEM` / `GUEST_ADDR` when 0 has to stay NULL
(`core/Game_memory.h`).

## What a game provides

`game/Game_hooks.h`, for every port:

| Macro | Meaning |
|---|---|
| `GAME_MAIN_FILENAME` | the original's file name, as `argv[0]` of its `main` |
| `GAME_NAME` | the game's name in the port's messages |
| `GAME_ENV(name)` | the port's environment variables: `"XX_" name` |
| `GAME_CD_MARKER` | a file or directory the game reads from the CD: a game directory holding it is also the disc (a distrib copy); without it, only a disc image is found |
| `GAME_CD_OPTIONAL` | the game works without its disc: "no CD" is printed only when the CD trace is on |
| `GAME_DATA_MARKERS` | file names, one of which the game directory holds (`"A.DAT", "B.BAT"`); default: `GAME_MAIN_FILENAME` |
| `GAME_FLIP_HOOK()` | optional: run before each frame is shown |
| `GAME_OSD_ITEMS()` | optional: after the settings, registers the game's own items of the port's panel (a switch of a mod say) with `Game_OsdAddItem` (`core/Game_osd.h`). The panel's key (`Display_OSD_Key`, F12) is the port's only key: no item has a key of its own; an item with a key of the settings file is saved there when it is switched (`Game_ConfigSave`). Enter, a click and a touch switch an item to its next value, round; an item of values in an order (sizes) has `step`, and Left/Right go to the smaller/larger value; an item of many values has `count`, `value` and `choose`: Enter, a click, a touch or the pad's A open the list of its values under it. The rows scroll when they do not fit; on a touch screen (Android, a touch device, `SteamDeck` in the environment) a row is about 7 mm high and a tap chooses it when the finger is lifted, a finger drawn up or down scrolls |
| `GAME_KEYBOARD_BLOCKED()`, `GAME_MOUSE_BLOCKED()` | optional: non-zero while the game reads keys or the mouse by itself |

A DOS game takes more ([`dos/README.md`](dos/README.md#what-a-dos-game-provides)), and so does a
Win32 one ([`win32/README.md`](win32/README.md#what-a-win32-game-provides)).

The remaining `GAME_*` names of the runtime are not hooks: `GAME_D3D_GL`, `GAME_WINAPI` and
`GAME_PROGRAM_NAME` come from `scons/game.py`, `GAME_CORE_CONSUMES_INPUT`, `GAME_DOS_ENTRY` and
`GAME_ENTRY_IS_START` are derived from the platform, the others are include guards.

## The window

`core/display/pc.c` is the window of every port (mode-13h palette, 4:3, shown through
OpenGL by the nearest pixel, the original's look, or with the 2D filter of
`Display_Shader=none|smooth|pixelart|scanlines|crt|xbr|super-xbr` (a mod, also an item of the port's panel); OpenGL 2.0 (GLES 3.0 on Android) is needed, without it
`Game_GlUnavailable` says so in a message box and the program ends; on Win32 the window
stays hidden, the game draws in its own, and DirectDraw shows its picture through OpenGL too,
with the same filter). Every window of a picture comes from `Game_WindowCreate` (`Game_eventloop.c`,
`Game_thread.h`): it opens at the picture's own size at its aspect (320x240 for mode 13h, a Win32
game's display mode), the player makes it bigger and the picture stays at its aspect inside, with
black bars; a new mode resizes a window the player did not; `Display_Fullscreen=off|on` (the item
Fullscreen of the panel) puts it on the whole screen; `Display_FPS=off|on` (the item FPS) shows the frames per second in the upper right corner. `Game_state.c` the variables
of `Game_vars.h` and `Game_CleanState`, `Game_log.c` the log file of a Windows program without
a console and the report of a crash. A game with its own window has its own `game/display/pc.c`.

## Files and the disc

The game directory, the write root, the disc and the DOS handle table are
`core/Game_gamedir.c` (`Game_FilesInit`, declared in `Game_vfs.h`); the game
names its data in `Game_hooks.h` (`GAME_DATA_MARKERS`, `GAME_CD_MARKER`,
`GAME_CD_CURRENT_DRIVE` for a game that runs from the disc).

## Android

On Android the game's `main` calls `Game_AndroidSetup()` (`core/Game_android.h`)
first: it sends stdout and stderr to logcat and sets `GAME_ENV("DATA_DIR")`
and `GAME_ENV("WRITE_ROOT")` to the directories `GameActivity` made.

## Keys for unattended runs

`<GAME>_AUTO_KEYS="seconds:Key[~hold_ms],..."` (or `@fifo`) presses keys for unattended runs
(`core/Game_keys.c`, DOS and DirectInput alike; `run-sdl.sh --keys=`); a game may set
the default hold with `GAME_AUTO_KEY_HOLD_MS`.
