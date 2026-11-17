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


/* The Ad Lib XMIDI driver of AIL 2 (Game_dosail2fm.c), for Game_dosail2.c */

#if !defined(_GAME_DOSAIL2FM_H_INCLUDED_)
#define _GAME_DOSAIL2FM_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the chip and the music: 0 when there is no audio device, or when the
 * driver is open already (one OPL2) */
int Game_AIL2_FmOpen(void);
/* a function of the driver (AIL.INC's number, 101-194) with the game's
 * arguments, args[0] being the driver's handle */
uint32_t Game_AIL2_FmCall(uint32_t function, const uint32_t *args);
void Game_AIL2_FmClose(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_DOSAIL2FM_H_INCLUDED_ */
