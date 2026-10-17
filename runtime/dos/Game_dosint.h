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

#ifndef _GAME_DOSINT_H_INCLUDED_
#define _GAME_DOSINT_H_INCLUDED_

#include <stdint.h>
#include "Game_dosregs.h"

/* The interrupts of a DOS4GW game: its interrupt vectors, its handlers run as
 * hardware interrupts, and the software interrupts of the translated code
 * (X86_InterruptProcedure) -- the game's own services first, then those every
 * DOS game here shares: INT 08h, 16h, 1Ah AH=00h, 21h, 2Fh, 31h. */

#ifdef __cplusplus
extern "C" {
#endif

/* Every vector to null_isr, the guest address of the game's handler that
 * returns at once (its llasm glue): a game that chains to the vector it found
 * lands there and not at address 0. */
void Game_DosIntInit(uint32_t null_isr);

uint32_t Game_DosGetVector(uint8_t vector);
void Game_DosSetVector(uint8_t vector, uint32_t address);

/* Runs the guest code at target as a hardware interrupt runs a handler: an
 * iret frame (return address 0, which unwinds to the host), IF set, the
 * registers as they were afterwards.  0 if it could not run now: not
 * translated code, the guest's IF clear, or another guest handler running. */
int Game_DosCallGuestIsr(uint32_t target);
/* The same for the handler of a vector (the keyboard's IRQ 1, IRQ 0). */
int Game_CallGuestInterrupt(uint8_t vector);
int Game_GuestIsrRunning(void);
/* The guest's IF set and no guest handler running. */
int Game_DosGuestInterruptsEnabled(void);
/* A timer callback registered with AIL, called as AIL's own ISR calls it once
 * it has re-enabled interrupts: `push user`, a near call, eax = user. */
int Game_DosCallGuestTimer(uint32_t target, uint32_t user);

/* The host pointer of a guest address, NULL for 0. */
void *Game_DosGuestPointer(uint32_t address);

/* One software interrupt: the game's GAME_DOS_INTERRUPT(number, regs) from
 * Game_hooks.h (nonzero: it served the call), then the shared
 * services; an interrupt nobody serves is reported and returns CF set.  A
 * game whose extender re-enters DOS through an interrupt of its own passes
 * the call on with the number of the service it means. */
void Game_DosDispatch(uint8_t number, Game_DosRegs *regs);

#ifdef __cplusplus
}
#endif

#endif
