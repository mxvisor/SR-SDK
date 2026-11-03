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

/* DOS conventional memory for a DOS4GW game: the PSP, the blocks DPMI 0100h
 * hands out, the real-mode buffers the interrupt services read, and the VGA
 * window.
 *
 * It lies at its own guest addresses, in the low memory of the guest window
 * (initialize_pointer_offset keeps it free), so a DOS address, segment << 4
 * plus offset, is the guest address of the byte:
 *   0 .. DOS_PSP_ADDRESS               the vectors and the BIOS data, zeros
 *                                      (a null pointer of the game reads them)
 *   DOS_PSP_ADDRESS .. DOS_MEMORY_END  the PSP, its environment, then the blocks
 *   DOS_VGA_ADDRESS, DOS_VGA_SIZE      the VGA window, Game_FrameBuffer
 * Where the game turns a segment into a flat address itself, the address is
 * right as it is; where SR drops the segment of an access, the code loads
 * Game_DosMemLastBlock, the guest address of the latest block. */

#define DOS_PSP_ADDRESS 0x10000u
#define DOS_MEMORY_END 0xA0000u
#define DOS_VGA_ADDRESS 0xA0000u
#define DOS_VGA_SIZE 0x10000u

#ifdef __cplusplus
extern "C" {
#endif

/* guest address of the block DPMI 0100h allocated last, 0 before the first */
extern uint32_t Game_DosMemLastBlock;

/* Makes the DOS memory and the VGA window real memory, zeros; the first
 * first_free bytes from the PSP on (PSP, environment) are taken.  0 on
 * success, nonzero when the guest has no window of its own (pointer_offset
 * 0: the host's address 0 is not the game's). */
int Game_DosMemInit(uint32_t first_free);
void Game_DosMemDeinit(void);
/* The PSP's first byte. */
uint8_t *Game_DosMemBase(void);
/* Gives all of the memory back except the first_free bytes from the PSP on. */
void Game_DosMemReset(uint32_t first_free);

/* Takes paragraphs * 16 bytes (at least 16), zeroed.  Returns the guest
 * address of the block, 0 if the memory is exhausted; *segment is its DOS
 * segment.  The block is also Game_DosMemLastBlock. */
uint32_t Game_DosMemAlloc(uint32_t paragraphs, uint16_t *segment);
/* The largest block DPMI 0100h could still give, in paragraphs (at most 0xffff). */
uint32_t Game_DosMemFreeParagraphs(void);
/* Bytes left below DOS_MEMORY_END. */
uint32_t Game_DosMemFreeBytes(void);

/* An address of the game's real-mode world, segment << 4 plus offset, as the
 * host pointer of the byte; NULL if the size does not fit below the end of the
 * VGA window. */
uint8_t *Game_DosMemPtr(uint32_t address, uint32_t size);

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
