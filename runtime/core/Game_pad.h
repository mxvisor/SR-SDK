/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

/* The gamepads of the host, for the joystick APIs of the games (winmm joy*,
 * DirectInput, the DOS game port).  A pad is what SDL_GameController knows
 * (a mapping of its own or of gamecontrollerdb.txt; on Windows SDL reads
 * XInput itself) and its state is that of XInput: the buttons of
 * XINPUT_GAMEPAD, two sticks with the Y axis up, two triggers 0..255.  Up to
 * GAME_PAD_MAX pads, each in the slot it took when it came: a pad that comes
 * later takes the first free one, and the others keep theirs.  Pads come and
 * go while the game runs.  Without a pad (or with Gamepad=off in the cfg)
 * nothing is opened and the games see no joystick, as before.
 *
 * Every function may be called from any thread.  <GAME>_DEBUG_PAD writes the
 * pads that come and go and every change of their state to stderr. */

#if !defined(_GAME_PAD_H_INCLUDED_)
#define _GAME_PAD_H_INCLUDED_

#include <stdint.h>

#define GAME_PAD_MAX 4

/* the buttons, as XINPUT_GAMEPAD.wButtons */
#define GAME_PAD_DPAD_UP        0x0001
#define GAME_PAD_DPAD_DOWN      0x0002
#define GAME_PAD_DPAD_LEFT      0x0004
#define GAME_PAD_DPAD_RIGHT     0x0008
#define GAME_PAD_START          0x0010
#define GAME_PAD_BACK           0x0020
#define GAME_PAD_LEFT_THUMB     0x0040
#define GAME_PAD_RIGHT_THUMB    0x0080
#define GAME_PAD_LEFT_SHOULDER  0x0100
#define GAME_PAD_RIGHT_SHOULDER 0x0200
#define GAME_PAD_GUIDE          0x0400
#define GAME_PAD_A              0x1000
#define GAME_PAD_B              0x2000
#define GAME_PAD_X              0x4000
#define GAME_PAD_Y              0x8000

/* XInput's own dead zones (XINPUT_GAMEPAD_*_THUMB_DEADZONE, _TRIGGER_THRESHOLD) */
#define GAME_PAD_LEFT_DEADZONE   7849
#define GAME_PAD_RIGHT_DEADZONE  8689
#define GAME_PAD_TRIGGER_DEADZONE  30

typedef struct {
    uint32_t packet;            /* changes when the state does (XINPUT_STATE.dwPacketNumber) */
    uint16_t buttons;           /* GAME_PAD_* */
    uint8_t  left_trigger;      /* 0..255 */
    uint8_t  right_trigger;
    int16_t  left_x, left_y;    /* -32768..32767, Y up */
    int16_t  right_x, right_y;
} Game_PadState;

#ifdef __cplusplus
extern "C" {
#endif

/* opens the pads there are and watches for others (Init_Input2, after SDL_Init) */
void Game_PadInit(void);
void Game_PadQuit(void);
/* the cfg keys Gamepad=on/off (on by default) and the mod Gamepad_RightStickDpad=on/off
 * (off by default: the right stick, past half its way, is also the D-pad while the
 * D-pad is not pressed, for a game that has a POV but no right stick); nonzero if
 * it is one of them */
int  Game_PadConfig(const char *key, const char *value);
/* the panel's item of Gamepad_RightStickDpad, for the GAME_OSD_ITEMS() of a game
 * that has the mod */
void Game_PadAddRightStickDpadItem(void);
/* the timer thread's 10 ms: the panel's keys of the pad, and <GAME>_DEBUG_PAD */
void Game_PadTick(void);
/* the host's main thread, for each event it takes out of the queue: a pad that
 * came or went (SDL_CONTROLLERDEVICEADDED, ..REMOVED); nonzero if it was one */
int  Game_PadEvent(const union SDL_Event *event);

/* how many pads there are; a bit for each slot that has one */
int  Game_PadCount(void);
unsigned int Game_PadSlots(void);
/* 0 when slot `pad` has none.  GetState: with the dead zones applied (in a
 * stick's dead zone it is 0, past it the stick goes from 0 to the edge
 * smoothly); GetRawState: as SDL reads it, the way XInputGetState does. */
int  Game_PadGetState(int pad, Game_PadState *state);
int  Game_PadGetRawState(int pad, Game_PadState *state);
/* the pad's name, "" when there is none */
const char *Game_PadName(int pad);
/* the motors: 0..65535 each, for `ms` milliseconds (0: stop); 0 when the pad
 * has none or SDL cannot */
int  Game_PadRumble(int pad, uint16_t low, uint16_t high, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_PAD_H_INCLUDED_ */
