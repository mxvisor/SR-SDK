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

/* An on-screen gamepad for touch devices: a D-pad on the left, action buttons
 * on the right.  Only the Android build draws and reacts to it; the desktop
 * builds get no-ops. */

#if !defined(_TOUCHGAMEPAD_H_INCLUDED_)
#define _TOUCHGAMEPAD_H_INCLUDED_

#include "Game_defs.h"

void TouchGamepad_Draw(SDL_Renderer *renderer);
/* the renderer of the last Draw goes away: its glyphs with it */
void TouchGamepad_ForgetRenderer(void);
/* changes when a button does: for a caller that keeps the drawn picture */
int  TouchGamepad_Stamp(void);
/* the game's state that gp_layout reads has changed: the buttons are let go and
 * placed again at the next Draw */
void TouchGamepad_Relayout(void);
int  TouchGamepad_Event(SDL_Event *event);
void TouchGamepad_ReleaseAll(void);
/* is a button at this place of the window (0..1)?  (anywhere while the
 * keyboard is open) */
int  TouchGamepad_ButtonAt(float x, float y);
/* the button `index`: 0 past the last one, otherwise *key is what it holds
 * down (SDLK_UNKNOWN: nothing); EmulateKey does not reach SDL's key state */
int  TouchGamepad_HeldKey(int index, SDL_Keycode *key);

/* The key of a button that is the left button of the mouse, not a key of the
 * keyboard: TouchGamepad_MouseButtons reads it. */
#define TOUCHGAMEPAD_MOUSE_LEFT ((SDL_Keycode)-2)
/* The key of a button that opens the on-screen keyboard (when it is let go):
 * a PC keyboard over the lower part of the screen, drawn instead of the
 * layout's buttons until its HIDE key or a touch above it closes it. */
#define TOUCHGAMEPAD_KEYBOARD ((SDL_Keycode)-3)
/* the keyboard is open: every touch is its own, none is the mouse's */
int  TouchGamepad_KeyboardShown(void);
/* the buttons of the mouse the on-screen buttons hold (1: the left one) */
int  TouchGamepad_MouseButtons(void);
/* The pointer: the finger that came down away from the buttons, the first of
 * such while none holds it.  *x, *y: where it is or was last (0..1 of the
 * window), *down: it still touches; the result counts the fingers that became
 * the pointer (a new touch), 0 before the first. */
unsigned int TouchGamepad_Pointer(float *x, float *y, int *down);

#endif /* _TOUCHGAMEPAD_H_INCLUDED_ */
