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


#include <string.h>

#include "Game_dosmem.h"
#include "Game_memory.h"

uint32_t Game_DosMemBias;
uint32_t Game_DosMemLastBlock;

static uint8_t *pool;
static uint32_t pool_dos_base;
static uint32_t pool_size;
static uint32_t pool_next;

int Game_DosMemInit(uint32_t dos_base, uint32_t size, uint32_t first_free)
{
    pool = (uint8_t *)map_memory_32bit(size, 0);
    if (pool == NULL) return -1;

    pool_dos_base = dos_base;
    pool_size = size;
    Game_DosMemBias = GAME_PTR_TO_GUEST32(pool) - dos_base;
    Game_DosMemReset(first_free);
    return 0;
}

void Game_DosMemDeinit(void)
{
    if (pool != NULL) unmap_memory_32bit(pool, pool_size);
    pool = NULL;
    pool_size = 0;
    pool_next = 0;
    Game_DosMemLastBlock = 0;
}

uint8_t *Game_DosMemBase(void)
{
    return pool;
}

void Game_DosMemReset(uint32_t first_free)
{
    if (pool != NULL) memset(pool, 0, pool_size);
    pool_next = first_free;
    Game_DosMemLastBlock = 0;
}

uint32_t Game_DosMemAlloc(uint32_t paragraphs, uint16_t *segment)
{
    uint32_t bytes = paragraphs * 16u;
    uint32_t at = (pool_next + 15u) & ~15u;

    if (pool == NULL) return 0;
    if (bytes == 0) bytes = 16u;
    if (at > pool_size || bytes > pool_size - at) return 0;

    pool_next = at + bytes;
    memset(pool + at, 0, bytes);
    *segment = (uint16_t)((pool_dos_base + at) >> 4);
    Game_DosMemLastBlock = GAME_PTR_TO_GUEST32(pool + at);
    return Game_DosMemLastBlock;
}

uint32_t Game_DosMemFreeBytes(void)
{
    return pool_next < pool_size ? pool_size - pool_next : 0;
}

uint32_t Game_DosMemFreeParagraphs(void)
{
    uint32_t available = Game_DosMemFreeBytes() / 16u;

    return available > 0xffffu ? 0xffffu : available;
}

uint8_t *Game_DosMemPtr(uint32_t address, uint32_t size)
{
    uint32_t guest_base;

    if (pool == NULL) return NULL;
    if (address >= pool_dos_base && address - pool_dos_base < pool_size && size <= pool_size - (address - pool_dos_base))
        return pool + (address - pool_dos_base);
    guest_base = GAME_PTR_TO_GUEST32(pool);
    if (address >= guest_base && address - guest_base < pool_size && size <= pool_size - (address - guest_base))
        return pool + (address - guest_base);
    return NULL;
}

uint32_t Game_DosMemFlat(uint32_t address)
{
    if (pool != NULL && address >= pool_dos_base && address - pool_dos_base < pool_size) return address + Game_DosMemBias;
    return address;
}
