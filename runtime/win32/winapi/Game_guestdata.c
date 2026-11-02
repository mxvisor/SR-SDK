/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* A game that reaches its data at the addresses the data had in the original
 * (guest_data_in_memory=yes in game.cfg: llasm -guest-data-in-memory, and
 * GAME_GUEST_DATA_IN_MEMORY here).  llasm puts the data in the guest's memory
 * (llasm_copy_guest_data), so the port maps the range the data needs
 * (llasm_guest_data_range) before the guest starts, and the generated code
 * asks this file for the code at an address of the original program. */

#ifdef GAME_GUEST_DATA_IN_MEMORY

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "Game_memory.h"
#include "Game_guestdata.h"

#ifdef PTROFS_64BIT

extern "C" {
extern uint32_t llasm_resolve_guest_code(uint32_t address, uint64_t ptrofs);
extern void llasm_guest_data_range(uint32_t *range);
extern void llasm_copy_guest_data(uint64_t ptrofs);
extern uint32_t llasm_guest_code_count(void);
extern uint32_t llasm_guest_code_va(uint32_t index);
}

/* "jmp reg" with an address of the original program's code (the lists of
 * game/recomp/.../guest_address_jumps.sci): the generated code's pointers are
 * not the addresses of the original program, llasm tabulates them. */
extern "C" uint32_t x86_resolve_guest_code(uint32_t address)
{
    uint32_t target = llasm_resolve_guest_code(address, pointer_offset);

    if (target == 0)
    {
        fprintf(stderr, "no code at the guest address %" PRIx32 "\n", address);
        abort();
    }

    return target;
}

/* "add reg, offset" with a code pointer in the register (the lists of
 * guest_address_jumps.sci): the program starts a task at the address of a
 * procedure plus the size of the procedure's head.  The code pointers are not
 * the addresses of the original program, so the pointer is turned back into
 * the address, the offset is added, and the code at the new address is the
 * result (the address is a "target" of the list). */
typedef struct { uint32_t pointer; uint32_t address; } Game_CodePointer;

static Game_CodePointer *code_pointers = NULL;
static uint32_t code_pointer_count = 0;

static int compare_code_pointers(const void *a, const void *b)
{
    uint32_t pa = ((const Game_CodePointer *)a)->pointer, pb = ((const Game_CodePointer *)b)->pointer;

    return (pa > pb) - (pa < pb);
}

extern "C" uint32_t x86_guest_code_add(uint32_t pointer, uint32_t offset)
{
    Game_CodePointer key, *found;
    uint32_t target;

    if (code_pointers == NULL)
    {
        uint32_t i, count = llasm_guest_code_count();

        code_pointers = (Game_CodePointer *)malloc(count * sizeof(Game_CodePointer));
        for (i = 0; i < count; i++)
        {
            code_pointers[i].address = llasm_guest_code_va(i);
            code_pointers[i].pointer = llasm_resolve_guest_code(code_pointers[i].address, pointer_offset);
        }
        qsort(code_pointers, count, sizeof(Game_CodePointer), compare_code_pointers);
        code_pointer_count = count;
    }

    key.pointer = pointer;
    found = (Game_CodePointer *)bsearch(&key, code_pointers, code_pointer_count,
                                        sizeof(Game_CodePointer), compare_code_pointers);
    if (found == NULL)
    {
        fprintf(stderr, "no procedure at the code pointer %" PRIx32 "\n", pointer);
        abort();
    }

    target = llasm_resolve_guest_code(found->address + offset, pointer_offset);
    if (target == 0)
    {
        fprintf(stderr, "no code at the guest address %" PRIx32 " + %" PRIx32 "\n", found->address, offset);
        abort();
    }

    return target;
}

/* Maps the guest's memory: zeros, over whatever the runtime has reserved there. */
static int map_guest_memory(uintptr_t address, size_t size)
{
#ifdef _WIN32
    /* the part the runtime reserved is committed, the rest is reserved and committed */
    if (VirtualAlloc((void *)address, size, MEM_COMMIT, PAGE_READWRITE) != NULL)
    {
        return 0;
    }

    return (VirtualAlloc((void *)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != NULL) ? 0 : -1;
#else
    /* MAP_FIXED: the runtime reserves the area of the guest's memory
     * (PROT_NONE) for the guest's heap, and the game needs the low part of
     * it for its own data */
    void *mem = mmap((void *)address, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);

    return (mem == MAP_FAILED) ? -1 : 0;
#endif
}

/* how much of the guest's memory the data of the game needs (GAME_GUEST_LOW_MEMORY
 * of Game_memory.c) */
extern "C" unsigned int Game_GuestLowMemory(void)
{
    uint32_t range[2] = {0, 0};

    llasm_guest_data_range(range);

    return (unsigned int)((range[1] > 0x110000) ? range[1] : 0x110000) + 0x10000;
}

extern "C" void Game_GuestDataStart(void)
{
    uint32_t range[2] = {0, 0};
    size_t size;

    llasm_guest_data_range(range);

    /* The game was a 16-bit program in a 32-bit executable: it uses addresses
     * of the low memory of Windows 95 as well, so the low area of the guest's
     * address space is mapped (with zeros) and the data of the game is
     * copied into it (llasm_copy_guest_data).  The whole gigabyte on Linux
     * (the memory is not committed); on Windows and Android only the data and
     * a margin: on Android the runtime has reserved the guest region and hands
     * its arenas out of it, so a gigabyte mapping here would be overwritten by
     * the first arenas. */
#if defined(_WIN32) || defined(__ANDROID__)
    size = (size_t)Game_GuestLowMemory();
#else
    size = 0x40000000;
    if (range[1] > 0x40000000)
    {
        size = range[1];
    }
#endif

    if (map_guest_memory((uintptr_t)pointer_offset, size) != 0)
    {
        fprintf(stderr, "could not map the guest's memory at %" PRIxPTR "\n", (uintptr_t)pointer_offset);
        return;
    }

#if defined(__ANDROID__)
    Game_MemorySetGuestNext((uint64_t)size);
#endif
    llasm_copy_guest_data(pointer_offset);
}

#endif /* PTROFS_64BIT */

#endif /* GAME_GUEST_DATA_IN_MEMORY */
