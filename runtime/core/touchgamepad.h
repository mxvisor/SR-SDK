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
int  TouchGamepad_Event(SDL_Event *event);
/* is a button at this place of the window (0..1)? */
void TouchGamepad_ReleaseAll(void);
int  TouchGamepad_ButtonAt(float x, float y);
/* the button `index`: 0 past the last one, otherwise *key is what it holds
 * down (SDLK_UNKNOWN: nothing); EmulateKey does not reach SDL's key state */
int  TouchGamepad_HeldKey(int index, SDL_Keycode *key);

#endif /* _TOUCHGAMEPAD_H_INCLUDED_ */
