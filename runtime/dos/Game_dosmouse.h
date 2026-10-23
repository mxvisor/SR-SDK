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

#ifndef _GAME_DOSMOUSE_H_INCLUDED_
#define _GAME_DOSMOUSE_H_INCLUDED_

#include "Game_dosregs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Microsoft mouse driver of INT 33h: the function in AX (0 reset, 3 the
 * position and the buttons, 4 set it, 7 and 8 the range, ...), the arguments
 * in BX, CX, DX.  The position is the host mouse's in the picture of the game.
 * Returns nonzero when regs is a call to the driver; the DOS4GW extender
 * re-enters its own services through INT 33h with the DOS function in AH, so
 * only a function number below 100h is the mouse's. */
int Game_DosMouseCall(Game_DosRegs *regs);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_DOSMOUSE_H_INCLUDED_ */
