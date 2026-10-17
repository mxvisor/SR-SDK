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

#ifndef _GAME_DOSPIT_H_INCLUDED_
#define _GAME_DOSPIT_H_INCLUDED_

#include <stdint.h>

/* Channel 0 of the PIT (8253) for a DOS4GW game, on the host clock: the
 * counter the game reads, its interrupt (IRQ 0) and the BIOS tick count. */

#ifdef __cplusplus
extern "C" {
#endif

/* The reload value 65536, as the BIOS leaves it, and the count in step with
 * the BIOS ticks: it wraps as they advance. */
void Game_DosPitInit(void);
/* IN from port 40h: the counter's low or high byte, as the access mode of
 * the last control word says; a latched value first. */
uint8_t Game_DosPitIn(uint16_t port);
/* OUT to port 43h (control word: channel 0's access mode, or a latch) or 40h
 * (the reload value; once complete it restarts the count). */
void Game_DosPitOut(uint16_t port, uint8_t value);
/* The reload value as AIL writes it: control word 36h, low byte, high byte
 * (0 = 65536). */
void Game_DosPitProgram(uint32_t divisor);
/* Runs `irq` once for each interrupt due since the last call, as the count
 * ran out; a handler that writes a new reload value ends the run.  After a
 * stall the interrupts missed are not caught up. */
void Game_DosPitRun(void (*irq)(void));
/* INT 1Ah AH=00h: the BIOS ticks since midnight, 65536 PIT clocks each. */
uint32_t Game_DosPitBiosTicks(void);

#ifdef __cplusplus
}
#endif

#endif
