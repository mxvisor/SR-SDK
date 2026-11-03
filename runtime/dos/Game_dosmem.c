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


#include <stdio.h>
#include <string.h>

#include "Game_dosmem.h"
#include "Game_hooks.h"
#include "Game_memory.h"

uint32_t Game_DosMemLastBlock;

#define DOS_LOW_END (DOS_VGA_ADDRESS + DOS_VGA_SIZE)
#define POOL_SIZE (DOS_MEMORY_END - DOS_PSP_ADDRESS)

static uint8_t *pool;
static uint32_t pool_next;

int Game_DosMemInit(uint32_t first_free)
{
#ifdef PTROFS_64BIT
    /* the DOS memory is the low memory of the guest window */
    if (commit_guest_low(DOS_LOW_END) != 0) return -1;
    pool = (uint8_t *)GAME_GUEST32_TO_PTR(DOS_PSP_ADDRESS);
    Game_DosMemReset(first_free);
    return 0;
#else
    (void)first_free;
    return -1;
#endif
}

void Game_DosMemDeinit(void)
{
    pool = NULL;
    pool_next = 0;
    Game_DosMemLastBlock = 0;
}

uint8_t *Game_DosMemBase(void)
{
    return pool;
}

void Game_DosMemReset(uint32_t first_free)
{
    if (pool != NULL) memset(pool, 0, POOL_SIZE);
    pool_next = first_free;
    Game_DosMemLastBlock = 0;
}

uint32_t Game_DosMemAlloc(uint32_t paragraphs, uint16_t *segment)
{
    uint32_t bytes = paragraphs * 16u;
    uint32_t at = (pool_next + 15u) & ~15u;

    if (pool == NULL) return 0;
    if (bytes == 0) bytes = 16u;
    if (at > POOL_SIZE || bytes > POOL_SIZE - at) return 0;

    pool_next = at + bytes;
    memset(pool + at, 0, bytes);
    *segment = (uint16_t)((DOS_PSP_ADDRESS + at) >> 4);
    Game_DosMemLastBlock = DOS_PSP_ADDRESS + at;
    return Game_DosMemLastBlock;
}

uint32_t Game_DosMemFreeBytes(void)
{
    return pool_next < POOL_SIZE ? POOL_SIZE - pool_next : 0;
}

uint32_t Game_DosMemFreeParagraphs(void)
{
    uint32_t available = Game_DosMemFreeBytes() / 16u;

    return available > 0xffffu ? 0xffffu : available;
}

uint8_t *Game_DosMemPtr(uint32_t address, uint32_t size)
{
    if (pool == NULL || address >= DOS_LOW_END || size > DOS_LOW_END - address) return NULL;
    return (uint8_t *)GAME_GUEST32_TO_PTR(address);
}

static uint8_t *program_block;
static uint32_t program_size;

uint32_t Game_DosProgramInit(uint32_t size, uint32_t stack_size, uint32_t above)
{
    /* the guest window is 2 GiB (the code compares addresses as unsigned, the
     * game as signed): from its top down, the first free place above the
     * image, for the size asked or, if there is no room, half of it, down to
     * 4 MiB */
    Game_DosProgramDeinit();
    for (; size >= (4u << 20); size >>= 1) {
        program_block = (uint8_t *)map_memory_32bit_top(size, above);
        if (program_block != NULL) {
            program_size = size;
            break;
        }
    }
    if (program_block == NULL) {
        fprintf(stderr, GAME_NAME ": no free memory between the program's image (%08x) and the top of the guest window\n", above);
        Game_MemoryReport("the program's memory", 4u << 20);
        return 0;
    }
    return GAME_PTR_TO_GUEST32(program_block) + stack_size;
}

void Game_DosProgramDeinit(void)
{
    if (program_block != NULL) unmap_memory_32bit(program_block, program_size);
    program_block = NULL;
    program_size = 0;
}

uint32_t Game_DosProgramEnd(void)
{
    return program_block == NULL ? 0 : GAME_PTR_TO_GUEST32(program_block) + program_size;
}

int Game_DosProgramResize(uint32_t pages)
{
    return pages <= (Game_DosProgramEnd() >> 12);
}
