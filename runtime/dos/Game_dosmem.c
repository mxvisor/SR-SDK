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

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uint32_t Game_DosMemBias;
uint32_t Game_DosMemLastBlock;

static uint8_t *pool;
static uint32_t pool_dos_base;
static uint32_t pool_size;
static uint32_t pool_next;
static int pool_mapped;

uint8_t *Game_DosMapLow(uint32_t address, uint32_t size)
{
#ifdef PTROFS_64BIT
    /* initialize_pointer_offset keeps [0, 1 MiB + 64 KiB) of the guest
     * address space free (reserved, no access); with pointer_offset == 0 there
     * is nothing to commit and guest 0 is the host's. */
    uint8_t *host;
    uint32_t end = (address + size + 0xfffu) & ~0xfffu;

    address &= ~0xfffu;
    if (pointer_offset == 0 || size == 0 || end <= address || end > pointer_reserved_length) return NULL;
    host = (uint8_t *)GAME_GUEST32_TO_PTR(address);
#if defined(_WIN32)
    if (VirtualAlloc(host, end - address, MEM_COMMIT, PAGE_READWRITE) == NULL) return NULL;
#else
    if (mprotect(host, end - address, PROT_READ | PROT_WRITE) != 0) return NULL;
#endif
    return host;
#else
    (void)address;
    (void)size;
    return NULL;
#endif
}

int Game_DosMemInit(uint32_t dos_base, uint32_t size, uint32_t first_free)
{
    /* The DOS range at its own guest addresses when they are free: the flat
     * accesses of the code (PSP at 80h, the VGA at A0000h) then find what DOS
     * would have there, and Game_DosMemBias is 0. */
    pool = Game_DosMapLow(dos_base, size);
    pool_mapped = pool != NULL;
    if (pool == NULL) pool = (uint8_t *)map_memory_32bit(size, 0);
    if (pool == NULL) return -1;

    pool_dos_base = dos_base;
    pool_size = size;
    Game_DosMemBias = GAME_PTR_TO_GUEST32(pool) - dos_base;
    Game_DosMemReset(first_free);
    return 0;
}

void Game_DosMemDeinit(void)
{
    if (pool != NULL && !pool_mapped) unmap_memory_32bit(pool, pool_size);
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

static uint8_t *program_block;
static uint32_t program_size;

/* [guest, guest + size) as memory, if nothing else is there */
static uint8_t *map_at(uint32_t guest, uint32_t size)
{
    void *want = (void *)GAME_GUEST32_TO_PTR(guest);
#if defined(_WIN32)
    return (uint8_t *)VirtualAlloc(want, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif
    void *mem = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem == MAP_FAILED) return NULL;
    if (mem != want) {
        munmap(mem, size);
        return NULL;
    }
    return (uint8_t *)mem;
#endif
}

uint32_t Game_DosProgramInit(uint32_t size, uint32_t stack_size, uint32_t above)
{
    /* the guest window is 2 GiB (the code compares addresses as unsigned, the
     * game as signed): from its top down, the first free place above the
     * image, for the size asked or, if there is no room, half of it, down to
     * 4 MiB */
    uint32_t guest;

    Game_DosProgramDeinit();
    for (; size >= (4u << 20) && program_block == NULL; size >>= 1) {
        for (guest = 0x80000000u - size; guest >= above + 0x10000u; guest -= 0x10000u) {
            program_block = map_at(guest, size);
            if (program_block != NULL) {
                program_size = size;
                break;
            }
        }
        if (program_block != NULL) break;
    }
    if (program_block == NULL) {
        fprintf(stderr, GAME_NAME ": no free memory between the program's image (%08x) and the top of the guest window\n", above);
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
