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

#ifndef _GAME_KEYS_H_INCLUDED_
#define _GAME_KEYS_H_INCLUDED_

#include <stdint.h>
#include <SDL.h>

/* The keyboard as the originals saw it: the PC scancodes of SDL's keys, and
 * the keys <GAME>_AUTO_KEYS presses for unattended runs. */

#ifdef __cplusplus
extern "C" {
#endif

/* The PC (set 1) make code of an SDL key, with 0x80 added for a key of the
 * grey block that the keyboard sends after an E0 byte (DirectInput's DIK_*
 * values); 0 for a key the PC keyboard does not have. */
uint8_t Game_PcScancode(SDL_Scancode scancode);

/* Starts, once, the thread that presses the keys of <GAME>_AUTO_KEYS (if set).
 * Its key events go through SDL's queue as real ones do, with windowID 0, by
 * which the event loop takes them without the window having the focus. */
void Game_AutoKeysStart(void);
/* Whether <GAME>_AUTO_KEYS holds that key down: SDL_GetKeyboardState does not
 * see pushed events. */
int Game_AutoKeyDown(SDL_Scancode scancode);

#ifdef __cplusplus
}
#endif

#endif
