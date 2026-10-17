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


#ifndef _GAME_DOSMEM_H_INCLUDED_
#define _GAME_DOSMEM_H_INCLUDED_

#include <stdint.h>

/* DOS conventional memory for a DOS4GW game: the blocks DPMI 0100h hands out,
 * the real-mode buffers the interrupt services read, and the PSP.
 *
 * A real-mode segment is 16 bits, so the segments are those of the range
 * [dos_base, dos_base + size) as DOS would give them.  The memory itself is a
 * pool from map_memory_32bit, anywhere below 4 GB (Windows does not leave the
 * low megabyte free).  Where the game turns a segment into a flat address
 * itself, instruction_replacements.sci adds Game_DosMemBias, or loads
 * Game_DosMemLastBlock, the guest address of the latest block. */

#ifdef __cplusplus
extern "C" {
#endif

/* guest address of the pool minus dos_base, modulo 2^32: a DOS address in the
 * range plus this is the guest address of the byte */
extern uint32_t Game_DosMemBias;
/* guest address of the block DPMI 0100h allocated last, 0 before the first */
extern uint32_t Game_DosMemLastBlock;

/* Makes [address, address + size) of the guest address space real memory, the
 * way DOS has it: the low 1 MiB (and 64 KiB) are free for it.  Returns the
 * host pointer, NULL when they are not (the host has the guest's 0 itself, a
 * 32-bit host).  The pages are zeroed once, and stay while the process lives. */
uint8_t *Game_DosMapLow(uint32_t address, uint32_t size);

/* Maps the pool for the DOS range [dos_base, dos_base + size); the first
 * first_free bytes (PSP, environment) are taken.  0 on success. */
int Game_DosMemInit(uint32_t dos_base, uint32_t size, uint32_t first_free);
void Game_DosMemDeinit(void);
/* The pool's first byte, where the PSP lives. */
uint8_t *Game_DosMemBase(void);
/* Gives all of the pool back except the first_free bytes. */
void Game_DosMemReset(uint32_t first_free);

/* Takes paragraphs * 16 bytes (at least 16), zeroed.  Returns the guest
 * address of the block, 0 if the pool is exhausted; *segment is its DOS
 * segment.  The block is also Game_DosMemLastBlock. */
uint32_t Game_DosMemAlloc(uint32_t paragraphs, uint16_t *segment);
/* The largest block DPMI 0100h could still give, in paragraphs (at most 0xffff). */
uint32_t Game_DosMemFreeParagraphs(void);
/* Bytes left in the pool. */
uint32_t Game_DosMemFreeBytes(void);

/* An address of the game's real-mode world, segment << 4 plus offset, or a
 * guest address of the pool, as the host pointer of the byte; NULL if it is
 * neither or the size does not fit. */
uint8_t *Game_DosMemPtr(uint32_t address, uint32_t size);
/* The same address as a guest address: the pool's for a DOS one, else itself. */
uint32_t Game_DosMemFlat(uint32_t address);

/* The memory block of a program that starts at its entry point, as the
 * extender gives it: the stack at the bottom, then the heap that the startup
 * code grows with INT 21h AH=4Ah (it begins at the initial ESP and asks for
 * the new end of the block, in 4 KiB pages from address 0).  The startup code
 * measures the stack against the end of the program's image, so the block has
 * to lie above `above`, a guest address.  Returns the initial ESP, 0 if no
 * block of `size` bytes lies there. */
uint32_t Game_DosProgramInit(uint32_t size, uint32_t stack_size, uint32_t above);
void Game_DosProgramDeinit(void);
/* INT 21h AH=4Ah on the block: 1 if its end can be at `pages` * 4 KiB. */
int Game_DosProgramResize(uint32_t pages);
/* 0 without a program block */
uint32_t Game_DosProgramEnd(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_DOSMEM_H_INCLUDED_ */
