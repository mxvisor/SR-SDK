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

#ifndef _GAME_DOSREAL_H_INCLUDED_
#define _GAME_DOSREAL_H_INCLUDED_

#include <stdint.h>
#include "Game_dosregs.h"

/* Real-mode code in the DOS memory: a driver the game loads into a DOS block
 * itself (a sound driver read from a file) and reaches through an interrupt.
 * SR translates the game's 32-bit code only; this runs the 16-bit code of the
 * block on an interpreter of the 8086 (Game_dosreal.c), with the ports of the
 * translated code (the Sound Blaster, the DMA, the PIT) and the real-mode
 * interrupt vectors the game and the driver set.  Only for a game with
 * GAME_DOS_REALMODE in Game_hooks.h: without it the functions do nothing (a
 * vector is never the driver's).
 *
 * A vector counts as the driver's when it points into the DOS memory above the
 * PSP (DOS_PSP_ADDRESS .. DOS_MEMORY_END): every other value (none, or what a
 * game that only saves and restores vectors puts back) leaves the interrupt to
 * the runtime's services, as before. */

#ifdef __cplusplus
extern "C" {
#endif

/* DPMI 0201h, and INT 21h AH=25h of the real-mode code */
void Game_DosRealSetVector(uint8_t vector, uint16_t segment, uint16_t offset);
/* segment << 16 | offset as it was set, 0 if never */
uint32_t Game_DosRealGetVector(uint8_t vector);
/* Runs the real-mode handler of the vector, if it has one in the DOS memory,
 * with the registers of regs (the results go back into them, with the
 * arithmetic flags); ds and es are the segment registers, -1 for 0.  1 if it
 * ran. */
int Game_DosRealInterrupt(uint8_t vector, Game_DosRegs *regs, int ds, int es);
/* Forgets the vectors (the game starts again). */
void Game_DosRealReset(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_DOSREAL_H_INCLUDED_ */
