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
/* where the blocks begin (the PSP and its environment lie before), from the
 * PSP on, a multiple of 16 */
static uint32_t pool_first;

/* The taken blocks, by address: offsets from the PSP and sizes, multiples of
 * 16.  DOS keeps its arena in the memory itself; the game must not see it, so
 * the list is here. */
#define POOL_BLOCKS 256
typedef struct { uint32_t at, bytes; } pool_block;
static pool_block blocks[POOL_BLOCKS];
static unsigned int block_count;

int Game_DosMemInit(uint32_t first_free)
{
    /* the DOS memory is the low memory of the guest window */
    if (commit_guest_low(DOS_LOW_END) != 0) return -1;
    pool = (uint8_t *)GAME_GUEST32_TO_PTR(DOS_PSP_ADDRESS);
    Game_DosMemReset(first_free);
    return 0;
}

void Game_DosMemDeinit(void)
{
    pool = NULL;
    block_count = 0;
    Game_DosMemLastBlock = 0;
}

uint8_t *Game_DosMemBase(void)
{
    return pool;
}

void Game_DosMemReset(uint32_t first_free)
{
    if (pool != NULL) memset(pool, 0, POOL_SIZE);
    pool_first = (first_free + 15u) & ~15u;
    block_count = 0;
    Game_DosMemLastBlock = 0;
}

/* The end of the free space after the index-th block's predecessor: where the
 * next block (or the pool) begins. */
static uint32_t gap_end(unsigned int index)
{
    return index < block_count ? blocks[index].at : POOL_SIZE;
}

static uint32_t gap_start(unsigned int index)
{
    return index == 0 ? pool_first : blocks[index - 1].at + blocks[index - 1].bytes;
}

uint32_t Game_DosMemAlloc(uint32_t paragraphs, uint16_t *segment)
{
    const uint32_t bytes = paragraphs == 0 ? 16u : paragraphs * 16u;
    unsigned int index;

    if (pool == NULL || block_count == POOL_BLOCKS) return 0;
    /* the first gap it fits in, as DOS's first fit */
    for (index = 0; index <= block_count; index++) {
        const uint32_t at = gap_start(index);
        if (gap_end(index) >= at && gap_end(index) - at >= bytes) {
            memmove(&blocks[index + 1], &blocks[index], (block_count - index) * sizeof(blocks[0]));
            blocks[index].at = at;
            blocks[index].bytes = bytes;
            block_count++;
            memset(pool + at, 0, bytes);
            *segment = (uint16_t)((DOS_PSP_ADDRESS + at) >> 4);
            Game_DosMemLastBlock = DOS_PSP_ADDRESS + at;
            return Game_DosMemLastBlock;
        }
    }
    return 0;
}

/* the index of the block at the segment, block_count for none */
static unsigned int block_of(uint16_t segment)
{
    const uint32_t at = ((uint32_t)segment << 4) - DOS_PSP_ADDRESS;
    unsigned int index;

    for (index = 0; index < block_count && blocks[index].at != at; index++) {}
    return index;
}

int Game_DosMemFree(uint16_t segment)
{
    const unsigned int index = block_of(segment);

    if (index == block_count) return 0;
    block_count--;
    memmove(&blocks[index], &blocks[index + 1], (block_count - index) * sizeof(blocks[0]));
    return 1;
}

int Game_DosMemResize(uint16_t segment, uint32_t paragraphs)
{
    const unsigned int index = block_of(segment);
    const uint32_t bytes = paragraphs == 0 ? 16u : paragraphs * 16u;

    if (index == block_count || gap_end(index + 1) - blocks[index].at < bytes) return 0;
    blocks[index].bytes = bytes;
    return 1;
}

uint32_t Game_DosMemFreeBytes(void)
{
    uint32_t total = 0;
    unsigned int index;

    for (index = 0; index <= block_count; index++)
        if (gap_end(index) > gap_start(index)) total += gap_end(index) - gap_start(index);
    return total;
}

uint32_t Game_DosMemFreeParagraphs(void)
{
    uint32_t largest = 0;
    unsigned int index;

    for (index = 0; index <= block_count; index++)
        if (gap_end(index) > gap_start(index) && gap_end(index) - gap_start(index) > largest)
            largest = gap_end(index) - gap_start(index);
    return largest / 16u > 0xffffu ? 0xffffu : largest / 16u;
}

uint8_t *Game_DosMemPtr(uint32_t address, uint32_t size)
{
    if (pool == NULL || address >= DOS_LOW_END || size > DOS_LOW_END - address) return NULL;
    return (uint8_t *)GAME_GUEST32_TO_PTR(address);
}

static uint8_t *program_block;
static uint32_t program_size;
/* the guest address where the block ends for the program now (its break) */
static uint32_t program_break;
/* the block of the program that ran another one (Game_dosexec.c) */
static uint8_t *saved_block;
static uint32_t saved_size, saved_break;

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
    program_break = GAME_PTR_TO_GUEST32(program_block) + stack_size;
    return program_break;
}

void Game_DosProgramDeinit(void)
{
    if (program_block != NULL) unmap_memory_32bit(program_block, program_size);
    program_block = NULL;
    program_size = 0;
    program_break = 0;
}

uint32_t Game_DosProgramEnd(void)
{
    return program_block == NULL ? 0 : GAME_PTR_TO_GUEST32(program_block) + program_size;
}

uint32_t Game_DosProgramBase(void)
{
    return program_block == NULL ? 0 : GAME_PTR_TO_GUEST32(program_block);
}

int Game_DosProgramResize(uint32_t end)
{
    if (program_block == NULL || end < GAME_PTR_TO_GUEST32(program_block) || end > Game_DosProgramEnd()) return 0;
    program_break = end;
    return 1;
}

uint32_t Game_DosProgramAlloc(uint32_t size)
{
    /* in pages, as an extender gives memory: the break of the start-up's
     * resize is ESP, and the word at it is the stack's */
    const uint32_t address = (program_break + 0xfffu) & ~0xfffu;
    if (program_block == NULL || address > Game_DosProgramEnd() || size > Game_DosProgramEnd() - address) return 0;
    program_break = address + size;
    return address;
}

void Game_DosProgramPush(void)
{
    saved_block = program_block;
    saved_size = program_size;
    saved_break = program_break;
    program_block = NULL;
    program_size = 0;
    program_break = 0;
}

void Game_DosProgramPop(void)
{
    Game_DosProgramDeinit();
    program_block = saved_block;
    program_size = saved_size;
    program_break = saved_break;
    saved_block = NULL;
}
