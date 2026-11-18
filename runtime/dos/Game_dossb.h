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

#ifndef _GAME_DOSSB_H_INCLUDED_
#define _GAME_DOSSB_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The card the game's own driver looks for (BLASTER=A220 I5 D1 T1): the base
 * port, the IRQ and the 8-bit DMA channel; a game that has its own say
 * defines them in Game_hooks.h. */
#ifndef GAME_SB_BASE
#define GAME_SB_BASE 0x220u
#endif
#ifndef GAME_SB_IRQ
#define GAME_SB_IRQ 5
#endif
#ifndef GAME_SB_DMA
#define GAME_SB_DMA 1
#endif

/* The ports of the card (DSP, the 8237's registers of the channels): both
 * return 1 when the port is the card's (the byte read), 0 when it is not. */
int Game_DosSbIn(uint16_t port, uint32_t *value);
int Game_DosSbOut(uint16_t port, uint32_t value);

/* From the game's timer (SR_CheckTimer): the card takes the samples of the
 * DMA buffer that are due by its clock, and raises its IRQ at the end of a
 * block. */
void Game_DosSbDeliverIrq(void);

/* Closes the audio device. */
void Game_DosSbShutdown(void);

/* "BLASTER=A220 I5 D1 T1": the variable of the environment that names the card. */
const char *Game_DosBlaster(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_DOSSB_H_INCLUDED_ */
