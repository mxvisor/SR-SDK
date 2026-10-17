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

#ifndef _GAME_DOSCD_H_INCLUDED_
#define _GAME_DOSCD_H_INCLUDED_

#include <stdint.h>
#include "Game_dosfile.h"

/* MSCDEX for a DOS4GW game: the CD drive of Game_cdrom.c's disc, its table
 * of contents from the cue sheet, and its Red Book audio played by
 * Game_music.c. */

#ifdef __cplusplus
extern "C" {
#endif

/* INT 2Fh AH = 15h, as the game calls it through DPMI 0300h: `regs` are the
 * real-mode registers of the call structure (the same order up to the
 * flags, of which only CF is changed) and `es` their ES, the segment of a
 * buffer (ES:BX).  Done, and 1 returned; any other AH: 0, the registers
 * untouched. */
int Game_DosCdCall(Game_DosRegs *regs, uint16_t es);

#ifdef __cplusplus
}
#endif

#endif
