# runtime/core: what every port shares

Threads, memory, the window and its frontends, the OSD and its font, printf, Android
start-up, and what the DOS and the Win32 ports share. A game that has no use for a file
leaves it out with `runtime_skip=` of its `game.cfg`.

- `Game_memory.c`, `.h`: the guest window ([`../README.md`](../README.md#the-guests-memory));
- `Game_thread.c`, `.h`, `Game_eventloop.c`: the threads, the event loop, the windows
  (`Game_WindowCreate`, `Game_RunOnMain`);
- `display/`, `input/`, `audio/`: the default window ([`../README.md`](../README.md#the-window)),
  input and sound of a port (`pc.c` each); `gl/`: the OpenGL that shows the picture, with its
  filters (xBR);
- `Game_ui.c`, `.h`: the port's own screen interface over the game's picture: the touch gamepad,
  one client (the window menu, a dialog) and the on-screen display, drawn after the picture;
- `Game_osd.c`, `.h`, `VKfont2.h`, `Game_font_cyr.h`: the port's panel and its font (the
  Cyrillic letters: `tools/font_cyrillic.py`);
- `Game_config.c`, `.h`: the settings file, `<program>.cfg`;
- `Game_log.c`, `.h`, `Game_state.c`, `Game_vars.h`: the log, the report of a crash, the state;
- `Game_clib.c`: the WATCOM C library functions a game's
  `external_procedures.sci` routes to the host (kbhit and getch: `dos/Game_doskbd.c`);
- `Game_vfs.c`, `.h`: WATCOM stdio over DOS handles, with a write root that
  can overlay the game directory;
- `Game_file.c`, `.h`: DOS files as host files or as files of the CD image;
- `Game_gamedir.c`: the game directory (C:), the write root, the disc (D:), the DOS
  handle table;
- `Game_cdrom.c`, `.h`: drive D: from a cue sheet (ISO9660 data track,
  CDDA or ogg/flac/wav audio tracks) or from a copy of the disc;
- `Game_music.c`, `.h`: MSCDEX audio playback through SDL_mixer;
- `Game_ail.c`, `.h`: the Miles Sound System (AIL 3.x) without its entry points: digital
  samples on reserved channels of the SDL_mixer device, XMIDI sequences through
  `Game_midi`; `$<GAME>_TRACE_SOUND` logs what it plays;
- `Game_midi.c`, `.h`, `Game_tsf_impl.c`: MIDI and XMIDI on TinySoundFont (`extern/tsf`), with
  `soundfont.sf2` of the game directory;
- `Game_audio.c`, `Game_sound.h`: the SDL_mixer device (`Game_AudioInit`) shared by CD audio,
  MIDI and a game's digital sound, DOS and Win32 (`GAME_MIXER_CHANNELS`);
- `Game_audiostream.c`, `.h`: a stream of PCM (`Game_AudioSourceOpen`, SDL_OpenAudioDevice's
  calls: a fill callback or a queue) on a device of its own or in SDL_mixer's output, whose one
  post-mix hook it owns (`GAME_AUDIO_ON_MIXER`); the Sound Blaster, DirectSound and a game's own
  driver play through it;
- `Game_opl.c`, `.h`: an OPL2 (emu8950, `extern/emu8950`, built as C by `Game_opl_impl.c` and
  `Game_opl_adpcm_impl.c`) on SDL_mixer's music, with the service routine of the FM driver that
  writes it called on the audio thread at the driver's rate (AIL's 120 Hz), so the music keeps its
  time whatever the game's frame rate is;
- `Game_keys.c`, `.h`: the PC scancode of an SDL key, `<GAME>_AUTO_KEYS`;
- `Game_pad.c`, `.h`: the host's gamepads (`SDL_GameController`, up to four, plugged in and out
  while the game runs) with the state of XInput: its buttons, two sticks, two triggers, with or
  without XInput's dead zones, and the motors; for the joystick APIs of the games. No pad, or
  `Gamepad=off` in the settings file: nothing is opened. `$<GAME>_DEBUG_PAD` logs the pads and
  every change of their state;
- `touchgamepad.c`, `.h`: the on-screen buttons of the Android build, drawn over the game and
  pressing keys; a game's `game/Game_touchlayout.h` places them (`touchlayout_default.h` otherwise),
  and may define `GP_SECOND_KEY(index)`, a second key a button holds with its own (a chord: Ctrl+Alt of one thumb).
  A button with the key `TOUCHGAMEPAD_KEYBOARD` opens a translucent PC keyboard (F1-F12, Esc,
  Shift, Ctrl, Alt and the arrows; EN/RU changes only its labels, the game makes the keys
  letters) in place of the buttons: its HIDE key, or a touch above it, closes it, and while it is
  open no touch is the game's mouse. A game without such a button has no keyboard. While a
  button with the key `TOUCHGAMEPAD_MOUSE_RIGHT` is held, a touch on the picture is the right
  button of the DOS mouse where the finger is, until it is lifted;
- `Game_gyro.c`, `.h`: the host's gyroscope (a phone, a tablet) as the pointer, the mod
  `Gyro_Mouse=on` with `Gyro_Mouse_Speed=1..9` (5: the picture's width in a turn of 38 degrees);
  opened on the thread that pumps the events, only while the mod is on, and closed there before
  that thread ends (`Game_GyroQuit`: on Android its events come through that thread's looper).
  The INT 33h driver of `runtime/dos` then keeps its own position, which turning the device moves; a touch away from the
  on-screen buttons (the pointer of `touchgamepad.c`, not SDL's mouse of the first finger) puts
  it under the finger and presses the button, and the turns go on from there. An on-screen button
  with the key `TOUCHGAMEPAD_MOUSE_LEFT` is the mouse's left button; a layout may give it to a
  button while the mod is on (`Game_GyroMod()`; a switch of the mod places the buttons again). A game shows the two items in its panel with `Game_GyroAddItems()` from
  `GAME_OSD_ITEMS()`;
- `Game_android.c`, `.h`: the start on Android (`Game_AndroidSetup`);
- `printf_x86.c`, `.h`: the guest's printf family;
- `virtualfs.c`, `.h`: the tree of the host's files under their DOS names (8.3);
- `llasm/Game-asm.c`: the C side of the call into the recompiled entry point.

## extern

`extern/tsf/` is the [TinySoundFont](https://github.com/schellingb/TinySoundFont)
submodule: `tsf.h` (MIT) and `tml.h` (Zlib), header-only. Its directory is on
the game's include path, for ports that synthesize MIDI music in the audio
callback (`Game_midi.c`); SDL2_mixer's own MIDI backend is then not needed.

`extern/emu8950/` is the [emu8950](https://github.com/digital-sound-antiques/emu8950)
submodule (MIT): an emulator of the YM3812, the OPL2 FM chip of the Ad Lib, in C, for
the FM music of the DOS games (`Game_opl.c`).
