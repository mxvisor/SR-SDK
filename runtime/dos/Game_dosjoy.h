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

#ifndef _GAME_DOSJOY_H_INCLUDED_
#define _GAME_DOSJOY_H_INCLUDED_

#include <stdint.h>

/* The game port (201h) of a DOS game, on the host's first gamepad
 * (Game_pad.h).  Without a pad it reads FFh, as a port with no joystick. */

#ifdef __cplusplus
extern "C" {
#endif

/* IN from 201h: bits 0-3 the axes' one-shots (1 while timing), bits 4-7 the
 * buttons (0 pressed). */
uint8_t Game_DosJoyIn(void);
/* OUT to 201h: starts the one-shots. */
void Game_DosJoyOut(void);
/* The BIOS joystick (INT 15h AH=84h), which reads the same port.  DX=0: the
 * buttons as bits 4-7 of the port (F0h without a pad).  DX=1: the axes A X,
 * A Y, B X, B Y in the port's counts of reads (0 without a pad, as a BIOS
 * that finds nothing plugged in).  Neither starts the one-shots nor counts
 * as a read of the port. */
uint8_t Game_DosJoyButtons(void);
void Game_DosJoyAxes(uint16_t out[4]);

#ifdef __cplusplus
}
#endif

#endif
