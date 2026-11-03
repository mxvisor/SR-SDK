/**
 *
 *  Copyright (C) 2025-2026 Roman Pauer
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

#ifdef __cplusplus

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <inttypes.h>
#include "Game_hooks.h"
#else
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <link.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#if !defined(MAP_FIXED_NOREPLACE) && defined(MAP_EXCL)
#define MAP_FIXED_NOREPLACE (MAP_FIXED | MAP_EXCL)
#endif
#include <pthread.h>
#if defined(__ANDROID__)
#include "Game_android.h"
#endif
#include "Game_hooks.h"
#ifdef __linux__
#include <linux/prctl.h>
#include <sys/prctl.h>
#endif
#endif

/* The guest's memory is the guest window (below): a 64-bit host, the guest's
 * addresses offsets from pointer_offset.  Linux, Windows and Android. */
#if !defined(PTROFS_64BIT)
#error "the guest's memory needs PTROFS_64BIT (a 64-bit host)"
#endif

/* Game_hooks.h may set GAME_GUEST_LOW_MEMORY(): how much of the guest's
 * address space from pointer_offset up the program wants to be free of
 * the host (the low memory of the guest window). The default is the low
 * megabyte of the guest and the 64 KB after it. */
#if defined(GAME_GUEST_DATA_IN_MEMORY) && !defined(GAME_GUEST_LOW_MEMORY)
#include "Game_guestdata.h"
#define GAME_GUEST_LOW_MEMORY() Game_GuestLowMemory()
#endif
#ifndef GAME_GUEST_LOW_MEMORY
#define GAME_GUEST_LOW_MEMORY() (1024*1024 + 65536)
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "Game_memory.h"


#define HEAPBLOCK_BASE_SIZE (1024 * 1024)

typedef struct _memblock_t
{
    union
    {
        int32_t inverted_value; // allocated block: inverted value of block size; top bit is one, bottom three bits are one
        int32_t offset_next; // free block: offset (from start of heap block) to next free block; top bit is zero, bottom three bits are zero
    } u;
    int32_t block_size; // block size including header; top bit is zero, bottom three bits are zero
} memblock_t;

typedef struct _heapblock_t
{
    uint8_t *address; // address of mapped memory
    int32_t size; // size of mapped memory
    int32_t offset_first_free; // offset (from start of heap block) to first free block
    int32_t offset_largest_free; // offset (from start of heap block) to largest free block
    int32_t offset_largest_free_prev; // offset (from start of heap block) to last free block before largest free block
    struct _heapblock_t *prev_block; // previous heapblock
} heapblock_t;


static heapblock_t *last_heapblock = NULL;
static heapblock_t *last_released_heapblock = NULL;

#ifdef _WIN32
static CRITICAL_SECTION mutex;
#else
static pthread_mutex_t mutex;
#endif


extern "C" void (*ptr_initialize_pointers)(uint64_t pointer_offset);

uint64_t pointer_offset;

unsigned int pointer_reserved_length;


int x86_init_malloc(void)
{
#ifdef _WIN32
    InitializeCriticalSection(&mutex);
    return 0;
#else
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr))
    {
        return 1;
    }

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
    {
        pthread_mutexattr_destroy(&attr);
        return 2;
    }

    if (pthread_mutex_init(&mutex, &attr))
    {
        pthread_mutexattr_destroy(&attr);
        return 3;
    }

    pthread_mutexattr_destroy(&attr);
    return 0;
#endif
}

void x86_deinit_malloc(void)
{
#ifdef _WIN32
    DeleteCriticalSection(&mutex);
#else
    pthread_mutex_destroy(&mutex);
#endif
}

#ifdef _WIN32
    #define LOCK_MUTEX EnterCriticalSection(&mutex);
    #define UNLOCK_MUTEX LeaveCriticalSection(&mutex);
#else
    #define LOCK_MUTEX pthread_mutex_lock(&mutex);
    #define UNLOCK_MUTEX pthread_mutex_unlock(&mutex);
#endif

static memblock_t *get_free_block(heapblock_t *heapblock, int32_t offset)
{
    memblock_t *freeblock;

    freeblock = (memblock_t *)(heapblock->address + offset);

    // check header integrity
    if ((freeblock->u.offset_next & 0x80000007u) != 0 ||
        (freeblock->block_size & 0x80000007u) != 0 ||
        freeblock->block_size < (long)sizeof(memblock_t) ||
        freeblock->block_size > heapblock->size - offset ||
        freeblock->u.offset_next > heapblock->size ||
        freeblock->u.offset_next < freeblock->block_size + offset
       )
    {
        fprintf(stderr, "Corrupted memory header: heap %p size %d offset %d block %p next %08x size %d\n",
                (void *)heapblock->address, heapblock->size, offset,
                (void *)freeblock, freeblock->u.offset_next, freeblock->block_size);
        Game_MemoryReport("corrupted heap", 0);
        exit(1);
    }

    return freeblock;
}

static void find_largest_free_block(heapblock_t *heapblock)
{
    memblock_t *freeblock;
    int32_t largestsize, nextoffset, prevoffset;

    heapblock->offset_largest_free = heapblock->offset_first_free;
    heapblock->offset_largest_free_prev = -1;
    if (heapblock->offset_first_free >= 0)
    {
        freeblock = get_free_block(heapblock, heapblock->offset_first_free);
        largestsize = freeblock->block_size;

        prevoffset = heapblock->offset_first_free;
        nextoffset = freeblock->u.offset_next;
        while (nextoffset < heapblock->size)
        {
            freeblock = get_free_block(heapblock, nextoffset);
            if (freeblock->block_size > largestsize)
            {
                largestsize = freeblock->block_size;
                heapblock->offset_largest_free = nextoffset;
                heapblock->offset_largest_free_prev = prevoffset;
            }
            prevoffset = nextoffset;
            nextoffset = freeblock->u.offset_next;
        }
    }
}

static heapblock_t *find_alloc_heap_block(memblock_t *allocblock)
{
    heapblock_t *heapblock;

    for (heapblock = last_heapblock; heapblock != NULL; heapblock = heapblock->prev_block)
    {
        if ((uintptr_t)allocblock < (uintptr_t)heapblock->address) continue;
        if ((uintptr_t)allocblock >= heapblock->size + (uintptr_t)heapblock->address) continue;

        // check header integrity
        if (allocblock->block_size != ~allocblock->u.inverted_value ||
            (allocblock->block_size & 0x80000007u) != 0 ||
            allocblock->block_size < (long)sizeof(memblock_t) ||
            allocblock->block_size > (int32_t)((heapblock->size + (uintptr_t)heapblock->address) - ((uintptr_t)allocblock))
           )
        {
            fprintf(stderr, "Corrupted memory header: heap %p size %d block %p inv %08x size %d\n",
                    (void *)heapblock->address, heapblock->size, (void *)allocblock,
                    allocblock->u.inverted_value, allocblock->block_size);
            Game_MemoryReport("corrupted heap", 0);
            exit(1);
        }

        return heapblock;
    }

    fprintf(stderr, "Corrupted heap\n");
    exit(1);
}

static memblock_t *find_last_free_block_before(heapblock_t *heapblock, int32_t offset)
{
    memblock_t *freeblock;
    int32_t current_offset;

    if (heapblock->offset_largest_free < offset)
    {
        current_offset = heapblock->offset_largest_free;
    }
    else if (heapblock->offset_largest_free_prev < offset)
    {
        current_offset = heapblock->offset_largest_free_prev;
    }
    else
    {
        current_offset = heapblock->offset_first_free;
    }

    do
    {
        freeblock = get_free_block(heapblock, current_offset);
        if (freeblock->u.offset_next >= offset) return freeblock;

        current_offset = freeblock->u.offset_next;
    } while (current_offset < heapblock->size);

    fprintf(stderr, "Corrupted heap\n");
    exit(1);
}

void *x86_malloc(unsigned int size)
{
    heapblock_t *heapblock;
    memblock_t *largestblock, *freeblock, *prevblock;

    if (size > 0x80000000u - (2 * sizeof(memblock_t) + HEAPBLOCK_BASE_SIZE))
    {
        errno = ENOMEM;
        return NULL;
    }

    // round up allocated size and add header size
    size = (size + 2 * sizeof(memblock_t) - 1) & ~(sizeof(memblock_t) - 1);

    LOCK_MUTEX

    // try to find free memory in existing heapblocks
    for (heapblock = last_heapblock; heapblock != NULL; heapblock = heapblock->prev_block)
    {
        if (heapblock->offset_largest_free < 0) continue; // no free memory in heapblock
        largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
        if (largestblock->block_size < (int)size) continue; // size of largest free block in heapblock is smaller than required

        if (largestblock->block_size > (int)size)
        {
            // use the beginning of largest free block as allocated block and the rest as free block
            freeblock = (memblock_t *)(size + (uint8_t *)largestblock);
            freeblock->u.offset_next = largestblock->u.offset_next;
            freeblock->block_size = largestblock->block_size - size;
            largestblock->block_size = size;
            largestblock->u.inverted_value = ~largestblock->block_size;

            if (heapblock->offset_largest_free == heapblock->offset_first_free)
            {
                heapblock->offset_first_free += size;
            }
            else
            {
                prevblock = get_free_block(heapblock, heapblock->offset_largest_free_prev);
                prevblock->u.offset_next += size;
            }

            if (freeblock->block_size >= heapblock->size >> 1)
            {
                heapblock->offset_largest_free += size;
            }
            else
            {
                find_largest_free_block(heapblock);
            }
        }
        else
        {
            // change largest free block into allocated block
            if (heapblock->offset_largest_free == heapblock->offset_first_free)
            {
                heapblock->offset_first_free = largestblock->u.offset_next;
                if (heapblock->offset_first_free >= heapblock->size)
                {
                    heapblock->offset_first_free = -1;
                }
            }
            else
            {
                prevblock = get_free_block(heapblock, heapblock->offset_largest_free_prev);
                prevblock->u.offset_next = largestblock->u.offset_next;
            }

            largestblock->u.inverted_value = ~largestblock->block_size;

            find_largest_free_block(heapblock);
        }

        UNLOCK_MUTEX
        errno = 0;
        return (void *)(sizeof(memblock_t) + (uint8_t *)largestblock);
    }

    do
    {
        if (last_released_heapblock != NULL)
        {
            // reuse last released heapblock
            heapblock = last_released_heapblock;
            last_released_heapblock = NULL;

            if (heapblock->size >= (int)((size + HEAPBLOCK_BASE_SIZE - 1) & ~(HEAPBLOCK_BASE_SIZE - 1))) break;

            unmap_memory_32bit(heapblock->address, heapblock->size);
        }
        else
        {
            // allocate new heapblock header
            heapblock = (heapblock_t *) malloc(sizeof(heapblock_t));
            if (heapblock == NULL)
            {
                UNLOCK_MUTEX
                errno = ENOMEM;
                return NULL;
            }
        }

        // allocate heapblock memory
        heapblock->size = (size + HEAPBLOCK_BASE_SIZE - 1) & ~(HEAPBLOCK_BASE_SIZE - 1);
        heapblock->address = (uint8_t *)map_memory_32bit(heapblock->size);
        if (heapblock->address == NULL)
        {
            free(heapblock);
            UNLOCK_MUTEX
            errno = ENOMEM;
            return NULL;
        }

#ifdef PR_SET_VMA_ANON_NAME
        prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, heapblock->address, heapblock->size, "heap32");
#endif
    } while (0);

    heapblock->prev_block = last_heapblock;
    last_heapblock = heapblock;

    // use the beginning of heapblock memory as allocated block
    largestblock = (memblock_t *)heapblock->address;
    largestblock->block_size = size;
    largestblock->u.inverted_value = ~largestblock->block_size;

    // use the rest of heapblock memory as free block
    if ((int)size < heapblock->size)
    {
        heapblock->offset_first_free = size;
        freeblock = (memblock_t *)(size + heapblock->address);
        freeblock->block_size = heapblock->size - size;
        freeblock->u.offset_next = heapblock->size;
    }
    else heapblock->offset_first_free = -1;

    heapblock->offset_largest_free = heapblock->offset_first_free;
    heapblock->offset_largest_free_prev = -1;

    UNLOCK_MUTEX
    errno = 0;
    return (void *)(sizeof(memblock_t) + (uint8_t *)largestblock);
}

void x86_free(void *ptr)
{
    memblock_t *allocblock, *freeblock, *largestblock, *prevblock, *oldblock;
    heapblock_t *heapblock, *prevheapblock;
    int32_t offsetalloc, offsetprev, offsetfree;

    if (ptr == NULL)
    {
        errno = 0;
        return;
    }

    allocblock = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));

    LOCK_MUTEX

    heapblock = find_alloc_heap_block(allocblock);

    offsetalloc = (int32_t)((uintptr_t)allocblock - (uintptr_t)heapblock->address);

    if (heapblock->offset_first_free < 0)
    {
        // no free blocks in heapblock -> freed block will be the only free block
        allocblock->u.offset_next = heapblock->size;
        heapblock->offset_first_free = offsetalloc;
        heapblock->offset_largest_free = offsetalloc;
    }
    else if (heapblock->offset_first_free > offsetalloc)
    {
        // first free block in heapblock is after freed block -> freed block will be the first free block
        freeblock = get_free_block(heapblock, heapblock->offset_first_free);

        if (offsetalloc + allocblock->block_size == heapblock->offset_first_free)
        {
            // freed block is adjacent to first free block -> coalesce free blocks
            allocblock->u.offset_next = freeblock->u.offset_next;
            allocblock->block_size += freeblock->block_size;
            freeblock->u.offset_next = 0;
            freeblock->block_size = 0;
            get_free_block(heapblock, offsetalloc);

            if (heapblock->offset_first_free == heapblock->offset_largest_free)
            {
                heapblock->offset_largest_free = offsetalloc;
            }
            else
            {
                largestblock = get_free_block(heapblock, heapblock->offset_largest_free);

                if (allocblock->block_size >= largestblock->block_size)
                {
                    heapblock->offset_largest_free = offsetalloc;
                    heapblock->offset_largest_free_prev = -1;
                }
                else if (heapblock->offset_largest_free_prev == heapblock->offset_first_free)
                {
                    heapblock->offset_largest_free_prev = offsetalloc;
                }
            }
        }
        else
        {
            // freed block is not adjacent to first free block
            allocblock->u.offset_next = heapblock->offset_first_free;
            get_free_block(heapblock, offsetalloc);

            if (heapblock->offset_first_free == heapblock->offset_largest_free)
            {
                largestblock = freeblock;
            }
            else
            {
                largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
            }

            if (allocblock->block_size >= largestblock->block_size)
            {
                heapblock->offset_largest_free = offsetalloc;
                heapblock->offset_largest_free_prev = -1;
            }
            else if (heapblock->offset_largest_free == heapblock->offset_first_free)
            {
                heapblock->offset_largest_free_prev = offsetalloc;
            }
        }

        heapblock->offset_first_free = offsetalloc;
    }
    else
    {
        // first free block in heapblock is before freed block
        prevblock = find_last_free_block_before(heapblock, offsetalloc);

        offsetprev = (int32_t)((uintptr_t)prevblock - (uintptr_t)heapblock->address);

        if (offsetprev + prevblock->block_size == offsetalloc)
        {
            // previous free block is adjacent to freed block -> coalesce free blocks
            prevblock->block_size += allocblock->block_size;
            allocblock->u.offset_next = 0;
            allocblock->block_size = 0;

            if (offsetprev + prevblock->block_size == prevblock->u.offset_next && prevblock->u.offset_next < heapblock->size)
            {
                // next free block is adjacent to freed block -> coalesce free blocks
                offsetfree = prevblock->u.offset_next;
                freeblock = get_free_block(heapblock, prevblock->u.offset_next);
                prevblock->block_size += freeblock->block_size;
                prevblock->u.offset_next = freeblock->u.offset_next;
                oldblock = freeblock;

                if (heapblock->offset_largest_free != offsetprev)
                {
                    if (heapblock->offset_largest_free == offsetfree)
                    {
                        largestblock = freeblock;
                    }
                    else
                    {
                        largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
                    }

                    if (prevblock->block_size > largestblock->block_size)
                    {
                        freeblock = find_last_free_block_before(heapblock, offsetprev);

                        heapblock->offset_largest_free = offsetprev;
                        heapblock->offset_largest_free_prev = (int32_t)((uintptr_t)freeblock - (uintptr_t)heapblock->address);
                    }
                    else if (heapblock->offset_largest_free_prev == offsetfree)
                    {
                        heapblock->offset_largest_free_prev = offsetprev;
                    }
                }

                oldblock->u.offset_next = 0;
                oldblock->block_size = 0;
            }
            else
            {
                // next free block is not adjacent to freed block

                if (heapblock->offset_largest_free != offsetprev)
                {
                    largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
                    if (prevblock->block_size > largestblock->block_size)
                    {
                        freeblock = find_last_free_block_before(heapblock, offsetprev);

                        heapblock->offset_largest_free = offsetprev;
                        heapblock->offset_largest_free_prev = (int32_t)((uintptr_t)freeblock - (uintptr_t)heapblock->address);
                    }
                }
            }
        }
        else
        {
            // previous free block is not adjacent to freed block
            allocblock->u.offset_next = prevblock->u.offset_next;
            get_free_block(heapblock, offsetalloc);
            prevblock->u.offset_next = offsetalloc;

            if (offsetalloc + allocblock->block_size == allocblock->u.offset_next && allocblock->u.offset_next < heapblock->size)
            {
                // next free block is adjacent to freed block -> coalesce free blocks
                offsetfree = allocblock->u.offset_next;
                freeblock = get_free_block(heapblock, allocblock->u.offset_next);
                allocblock->block_size += freeblock->block_size;
                allocblock->u.offset_next = freeblock->u.offset_next;
                freeblock->u.offset_next = 0;
                freeblock->block_size = 0;

                if (heapblock->offset_largest_free == offsetfree)
                {
                    heapblock->offset_largest_free = offsetalloc;
                }
                else
                {
                    if (heapblock->offset_largest_free == offsetprev)
                    {
                        largestblock = prevblock;
                    }
                    else
                    {
                        largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
                    }

                    if (allocblock->block_size >= largestblock->block_size)
                    {
                        heapblock->offset_largest_free = offsetalloc;
                        heapblock->offset_largest_free_prev = offsetprev;
                    }
                    else if (heapblock->offset_largest_free_prev == offsetfree)
                    {
                        heapblock->offset_largest_free_prev = offsetalloc;
                    }
                }
            }
            else
            {
                // next free block is not adjacent to freed block

                if (heapblock->offset_largest_free == offsetprev)
                {
                    largestblock = prevblock;
                }
                else
                {
                    largestblock = get_free_block(heapblock, heapblock->offset_largest_free);
                }

                if (allocblock->block_size >= largestblock->block_size)
                {
                    heapblock->offset_largest_free = offsetalloc;
                    heapblock->offset_largest_free_prev = offsetprev;
                }
                else if (heapblock->offset_largest_free_prev == offsetprev)
                {
                    heapblock->offset_largest_free_prev = offsetalloc;
                }
            }
        }
    }

    if (heapblock == last_heapblock && heapblock->offset_largest_free == 0)
    {
        largestblock = (memblock_t *)heapblock->address;
        if (largestblock->block_size == heapblock->size)
        {
            // last heapblock is empty -> unmap it
            for (;;)
            {
                prevheapblock = heapblock->prev_block;
                last_heapblock = prevheapblock;
                if (last_released_heapblock != NULL)
                {
                    unmap_memory_32bit(last_released_heapblock->address, last_released_heapblock->size);
                    free(last_released_heapblock);
                }
                if (prevheapblock == NULL)
                {
                    last_released_heapblock = NULL;
                    unmap_memory_32bit(heapblock->address, heapblock->size);
                    free(heapblock);
                    break;
                }
                else
                {
                    last_released_heapblock = heapblock;
                    heapblock->prev_block = NULL;
                }

                if (prevheapblock->offset_largest_free != 0) break;
                largestblock = get_free_block(prevheapblock, 0);
                if (largestblock->block_size != prevheapblock->size) break;
                heapblock = prevheapblock;
            }
        }
    }

    UNLOCK_MUTEX
    errno = 0;
    return;
}

void *x86_calloc(unsigned int nmemb, unsigned int size)
{
    void *ptr;

    if (size > 1)
    {
        if ((0x80000000u - (2 * sizeof(memblock_t) + HEAPBLOCK_BASE_SIZE)) / size < nmemb)
        {
            errno = ENOMEM;
            return NULL;
        }
    }

    ptr = x86_malloc(nmemb * size);
    if (ptr != NULL)
    {
        memset(ptr, 0, nmemb * size);
    }

    errno = 0;
    return ptr;
}

void *x86_realloc(void *ptr, unsigned int size)
{
    memblock_t *allocblock, *adjacentblock, *freeblock, *prevblock;
    heapblock_t *heapblock;
    int32_t offsetalloc, offsetadjacent, allocsize, copysize;
    void *ptr2;

    if (ptr == NULL)
    {
        return x86_malloc(size);
    }


    if (size == 0)
    {
        x86_free(ptr);
        return NULL;
    }

    if (size > 0x80000000u - (2 * sizeof(memblock_t) + HEAPBLOCK_BASE_SIZE))
    {
        errno = ENOMEM;
        return NULL;
    }

    // round up allocated size and add header size
    allocsize = (size + 2 * sizeof(memblock_t) - 1) & ~(sizeof(memblock_t) - 1);

    allocblock = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));

    LOCK_MUTEX

    heapblock = find_alloc_heap_block(allocblock);

    if (allocblock->block_size == allocsize)
    {
        // existing block size is the same as new size -> do nothing
        UNLOCK_MUTEX
        errno = 0;
        return ptr;
    }
    else if (allocblock->block_size > allocsize)
    {
        // existing block size is larger than new size -> split block into two and free the second one
        freeblock = (memblock_t *)(allocsize + (uint8_t *)allocblock);
        freeblock->block_size = allocblock->block_size - allocsize;
        freeblock->u.inverted_value = ~freeblock->block_size;
        allocblock->block_size = allocsize;
        allocblock->u.inverted_value = ~allocblock->block_size;

        x86_free((void *)(sizeof(memblock_t) + (uint8_t *)freeblock));
        UNLOCK_MUTEX
        errno = 0;
        return ptr;
    }
    else
    {
        // existing block size is smaller than new size -> check if adjacent block is large enough free block

        offsetalloc = (int32_t)((uintptr_t)allocblock - (uintptr_t)heapblock->address);
        if (offsetalloc + allocblock->block_size < heapblock->size)
        {
            adjacentblock = (memblock_t *)(allocblock->block_size + (uint8_t *)allocblock);
            if (adjacentblock->u.inverted_value >= 0 && adjacentblock->block_size >= allocsize - allocblock->block_size)
            {
                offsetadjacent = offsetalloc + allocblock->block_size;
                adjacentblock = get_free_block(heapblock, offsetadjacent);

                if (adjacentblock->block_size > allocsize - allocblock->block_size)
                {
                    // append beginning of the free block to existing block
                    freeblock = (memblock_t *)(allocsize + (uint8_t *)allocblock);
                    freeblock->u.offset_next = adjacentblock->u.offset_next;
                    freeblock->block_size = allocblock->block_size + adjacentblock->block_size - allocsize;
                    allocblock->block_size = allocsize;
                    allocblock->u.inverted_value = ~allocblock->block_size;
                    adjacentblock->u.offset_next = 0;
                    adjacentblock->block_size = 0;

                    if (heapblock->offset_first_free == offsetadjacent)
                    {
                        heapblock->offset_first_free = offsetalloc + allocsize;
                    }
                    else
                    {
                        prevblock = find_last_free_block_before(heapblock, offsetalloc);
                        prevblock->u.offset_next = offsetalloc + allocsize;
                    }

                    if (heapblock->offset_largest_free == offsetadjacent)
                    {
                        if (freeblock->block_size >= heapblock->size >> 1)
                        {
                            heapblock->offset_largest_free = offsetalloc + allocsize;
                        }
                        else
                        {
                            find_largest_free_block(heapblock);
                        }
                    }
                    else if (heapblock->offset_largest_free_prev == offsetadjacent)
                    {
                        heapblock->offset_largest_free_prev = offsetalloc + allocsize;
                    }
                }
                else
                {
                    // append whole free block to existing block
                    allocblock->block_size += adjacentblock->block_size;
                    allocblock->u.inverted_value = ~allocblock->block_size;

                    prevblock = NULL; // silence compiler warning

                    if (heapblock->offset_first_free == offsetadjacent)
                    {
                        heapblock->offset_first_free = adjacentblock->u.offset_next;
                        if (heapblock->offset_first_free >= heapblock->size)
                        {
                            heapblock->offset_first_free = -1;
                        }
                    }
                    else
                    {
                        prevblock = find_last_free_block_before(heapblock, offsetalloc);
                        prevblock->u.offset_next = adjacentblock->u.offset_next;
                    }

                    adjacentblock->u.offset_next = 0;
                    adjacentblock->block_size = 0;

                    if (heapblock->offset_largest_free == offsetadjacent)
                    {
                        find_largest_free_block(heapblock);
                    }
                    else if (heapblock->offset_largest_free_prev == offsetadjacent)
                    {
                        if (heapblock->offset_largest_free == heapblock->offset_first_free)
                        {
                            heapblock->offset_largest_free_prev = -1;
                        }
                        else
                        {
                            // if execution gets here, then the same statement was executed above, so it doesn't need to be executed again
                            //prevblock = find_last_free_block_before(heapblock, offsetalloc);
                            heapblock->offset_largest_free_prev = (int32_t)((uintptr_t)prevblock - (uintptr_t)heapblock->address);
                        }
                    }
                }

                UNLOCK_MUTEX
                errno = 0;
                return ptr;
            }
        }
    }

    copysize = allocblock->block_size - sizeof(memblock_t);

    ptr2 = x86_malloc(size);
    if (ptr2 == NULL)
    {
        UNLOCK_MUTEX
        errno = ENOMEM;
        return NULL;
    }

    memcpy(ptr2, ptr, copysize);
    x86_free(ptr);
    UNLOCK_MUTEX
    errno = 0;
    return ptr2;
}


/* The guest window: the 2 GB of the host's address space from pointer_offset
 * up, which the guest sees as its addresses 0 .. 2 GB.  It is reserved whole
 * (no access: no memory and no commit charge until a part is used), so after
 * the start nothing of the host's lands in it.  Inside, from the bottom:
 *   0 .. low          the guest's low memory (GAME_GUEST_LOW_MEMORY: the DOS
 *                     memory, the data of a game at its own addresses)
 *   low ..            the guest's heap, from the bottom up (map_memory_32bit)
 *   GUEST_IMAGE_AT    the host's image (the code and the game's data), where
 *                     the host leaves room below it (Android: the loader puts
 *                     it there, android/gameloader.c)
 *   .. 2 GB           a DOS program's own memory, from the top down
 *                     (map_memory_32bit_top)
 * What the host had there before the start (the image, the heap of brk) stays
 * and is left out.  The map is kept in granules of 64 KB, the allocation
 * granularity of Windows. */
#define GUEST_WINDOW_SIZE UINT64_C(0x80000000)
#define GUEST_IMAGE_AT UINT64_C(0x04000000)
#define GRANULE_SIZE UINT64_C(0x10000)
#define GRANULE_COUNT ((uint32_t)(GUEST_WINDOW_SIZE / GRANULE_SIZE))

enum { GRANULE_HOST, GRANULE_FREE, GRANULE_USED, GRANULE_LOW };
#define GRANULE_STATE 0x0f
/* Windows: the first granule of a reservation (a commit does not cross one) */
#define GRANULE_FIRST 0x80

static uint8_t window_map[GRANULE_COUNT];
static int window_ready;
/* guest addresses */
static uint64_t window_image_start, window_image_end;

typedef struct { uint64_t start, end; } host_range_t;

static host_range_t *host_ranges;
static int host_range_count, host_range_capacity;

static void add_host_range(uint64_t start, uint64_t end)
{
    if (host_range_count == host_range_capacity)
    {
        host_range_t *grown = (host_range_t *)realloc(host_ranges, (host_range_capacity + 256) * sizeof(host_range_t));
        if (grown == NULL) return;
        host_ranges = grown;
        host_range_capacity += 256;
    }
    host_ranges[host_range_count].start = start;
    host_ranges[host_range_count].end = end;
    host_range_count++;
}

#if !defined(__ANDROID__)
/* what the host has mapped in [from, to), in order */
static int collect_host_ranges(uint64_t from, uint64_t to)
{
    host_range_count = 0;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION minfo;
    uint64_t maddr = from;

    while (maddr < to)
    {
        if (0 == VirtualQuery((void *)maddr, &minfo, sizeof(minfo))) break;
        if (minfo.State != MEM_FREE)
        {
            add_host_range((uintptr_t)minfo.BaseAddress, minfo.RegionSize + (uintptr_t)minfo.BaseAddress);
        }
        maddr = minfo.RegionSize + (uintptr_t)minfo.BaseAddress;
    }
    return 0;
#else
    char line[512];
    uintmax_t start, end;
    FILE *f = fopen("/proc/self/maps", "rb");

    if (f == NULL) return 1;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (sscanf(line, "%" SCNxMAX "-%" SCNxMAX, &start, &end) != 2) continue;
        if (end > from && start < to) add_host_range(start, end);
    }
    fclose(f);
    return 0;
#endif
}

static int host_overlaps(uint64_t from, uint64_t to)
{
    int index;

    for (index = 0; index < host_range_count; index++)
    {
        if (host_ranges[index].end > from && host_ranges[index].start < to) return 1;
    }
    return 0;
}
#endif

#ifndef _WIN32
typedef struct { uint64_t inside, start, end; } image_range_t;

static int find_image_range(struct dl_phdr_info *info, size_t size, void *data)
{
    image_range_t *image = (image_range_t *)data;
    uint64_t first = UINT64_MAX, last = 0;
    int index;

    (void)size;
    for (index = 0; index < info->dlpi_phnum; index++)
    {
        const ElfW(Phdr) *header = &info->dlpi_phdr[index];
        uint64_t at = info->dlpi_addr + header->p_vaddr;

        if (header->p_type != PT_LOAD) continue;
        if (at < first) first = at;
        if (at + header->p_memsz > last) last = at + header->p_memsz;
    }
    if (image->inside < first || image->inside >= last) return 0;
    image->start = first;
    image->end = last;
    return 1;
}
#endif

/* the host's image, the one with the game's code */
static int find_host_image(uint64_t *start, uint64_t *end)
{
#ifdef _WIN32
    HMODULE hModule;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ptr_initialize_pointers, &hModule) || hModule == NULL)
    {
        return 0;
    }
    dos = (IMAGE_DOS_HEADER *)hModule;
    nt = (IMAGE_NT_HEADERS *)(dos->e_lfanew + (uint8_t *)hModule);
    *start = (uintptr_t)hModule;
    *end = (uintptr_t)hModule + nt->OptionalHeader.SizeOfImage;
    return 1;
#else
    image_range_t image;

    image.inside = (uintptr_t)&ptr_initialize_pointers;
    if (!dl_iterate_phdr(find_image_range, &image)) return 0;
    *start = image.start;
    *end = image.end;
    return 1;
#endif
}

static void set_granules(uint32_t first, uint32_t count, int state)
{
    uint32_t index;

    for (index = first; index < first + count; index++)
    {
        window_map[index] = (window_map[index] & GRANULE_FIRST) | state;
    }
}

#if !defined(__ANDROID__)
/* [first, first + count) of the window: reserved, nothing there */
static int reserve_granules(uint32_t first, uint32_t count)
{
    void *want = (void *)(uintptr_t)(pointer_offset + first * GRANULE_SIZE);
    uint64_t size = count * GRANULE_SIZE;
#ifdef _WIN32
    if (VirtualAlloc(want, size, MEM_RESERVE, PAGE_NOACCESS) != want) return 0;
#else
    void *mem = mmap(want, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);

    if (mem != want)
    {
        if (mem != MAP_FAILED) munmap(mem, size);
        return 0;
    }
#ifdef PR_SET_VMA_ANON_NAME
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, want, size, "guest");
#endif
#endif
    set_granules(first, count, GRANULE_FREE);
    window_map[first] |= GRANULE_FIRST;
    return 1;
}
#endif

static int run_is_free(uint32_t first, uint32_t count)
{
    uint32_t index;

    for (index = first; index < first + count; index++)
    {
        if ((window_map[index] & GRANULE_STATE) != GRANULE_FREE) return 0;
#ifdef _WIN32
        if (index != first && (window_map[index] & GRANULE_FIRST)) return 0;
#endif
    }
    return 1;
}

static void *use_granules(uint32_t first, uint32_t count)
{
    void *want = (void *)(uintptr_t)(pointer_offset + first * GRANULE_SIZE);

#ifdef _WIN32
    if (VirtualAlloc(want, count * GRANULE_SIZE, MEM_COMMIT, PAGE_READWRITE) != want) return NULL;
#else
    if (mmap(want, count * GRANULE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != want) return NULL;
#endif
    set_granules(first, count, GRANULE_USED);
    return want;
}

static uint32_t granules_of(uint64_t size)
{
    return (uint32_t)((size + GRANULE_SIZE - 1) / GRANULE_SIZE);
}

/* from the bottom up, above the low memory; a multiple of the heap's blocks
 * on a boundary of them */
static void *window_map_memory(unsigned int size)
{
    uint32_t count = granules_of(size), step, first;
    void *mem = NULL;

    step = ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0) ? (uint32_t)(HEAPBLOCK_BASE_SIZE / GRANULE_SIZE) : 1;
    LOCK_MUTEX
    first = (granules_of(pointer_reserved_length) + step - 1) / step * step;
    for (; first + count <= GRANULE_COUNT; first += step)
    {
        if (run_is_free(first, count))
        {
            mem = use_granules(first, count);
            break;
        }
    }
    UNLOCK_MUTEX
    return mem;
}

static void window_unmap_memory(void *mem, unsigned int size)
{
    uint64_t offset = (uintptr_t)mem - pointer_offset;
    uint32_t count = granules_of(size);

#ifdef _WIN32
    VirtualFree(mem, count * GRANULE_SIZE, MEM_DECOMMIT);
#else
    mmap(mem, count * GRANULE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#ifdef PR_SET_VMA_ANON_NAME
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, mem, count * GRANULE_SIZE, "guest");
#endif
#endif
    LOCK_MUTEX
    set_granules((uint32_t)(offset / GRANULE_SIZE), count, GRANULE_FREE);
    UNLOCK_MUTEX
}

static const char *granule_name(int state)
{
    switch (state)
    {
        case GRANULE_FREE: return "free";
        case GRANULE_USED: return "used";
        case GRANULE_LOW: return "low memory";
        default: return "host";
    }
}

static int window_from_host_ranges(uint64_t image_start, uint64_t image_end)
{
    uint64_t at = 0, start, end;
    int index;

    memset(window_map, GRANULE_HOST, sizeof(window_map));
    for (index = 0; index <= host_range_count; index++)
    {
        if (index < host_range_count)
        {
            // the list may begin below the window
            if (host_ranges[index].end <= pointer_offset) continue;
            start = (host_ranges[index].start > pointer_offset) ? host_ranges[index].start - pointer_offset : 0;
            end = host_ranges[index].end - pointer_offset;
        }
        else
        {
            start = end = GUEST_WINDOW_SIZE;
        }
        if (start > GUEST_WINDOW_SIZE) start = GUEST_WINDOW_SIZE;
        if (end > GUEST_WINDOW_SIZE) end = GUEST_WINDOW_SIZE;

        /* the free gap [at, start), the granules wholly in it */
        if (start > at)
        {
            uint32_t first = granules_of(at), last = (uint32_t)(start / GRANULE_SIZE);
#if defined(__ANDROID__)
            /* the loader has reserved the window: all of it is the game's */
            if (last > first) set_granules(first, last - first, GRANULE_FREE);
#else
            if (last > first) reserve_granules(first, last - first);
#endif
        }
        if (end > at) at = end;
    }

    window_image_start = image_start - pointer_offset;
    window_image_end = image_end - pointer_offset;
    if (window_image_start > GUEST_WINDOW_SIZE) window_image_start = GUEST_WINDOW_SIZE;
    if (window_image_end > GUEST_WINDOW_SIZE) window_image_end = GUEST_WINDOW_SIZE;

    if (!run_is_free(0, granules_of(pointer_reserved_length))) return 0;
    set_granules(0, granules_of(pointer_reserved_length), GRANULE_LOW);
    window_ready = 1;
    return 1;
}

/* stderr: the guest window, what is where in it, and what was wanted of it */
void Game_MemoryReport(const char *what, uint64_t wanted)
{
    uint32_t index, first;
    uint64_t free_bytes = 0, largest = 0;

    fprintf(stderr, "guest memory: %s", what);
    if (wanted != 0) fprintf(stderr, ", %" PRIu64 " bytes wanted", wanted);
    fprintf(stderr, "\n");
    if (!window_ready)
    {
        fprintf(stderr, "  no guest window, pointer_offset %" PRIx64 "\n", pointer_offset);
        return;
    }
    fprintf(stderr, "  window %" PRIx64 "-%" PRIx64 ", low memory 0-%x, image %08" PRIx64 "-%08" PRIx64 "\n",
            pointer_offset, pointer_offset + GUEST_WINDOW_SIZE, pointer_reserved_length, window_image_start, window_image_end);
    LOCK_MUTEX
    for (first = 0; first < GRANULE_COUNT; first = index)
    {
        int state = window_map[first] & GRANULE_STATE;

        for (index = first + 1; index < GRANULE_COUNT && (window_map[index] & GRANULE_STATE) == state; index++) {}
        fprintf(stderr, "  %08" PRIx64 "-%08" PRIx64 " %s\n", first * GRANULE_SIZE, index * GRANULE_SIZE, granule_name(state));
        if (state == GRANULE_FREE)
        {
            free_bytes += (index - first) * GRANULE_SIZE;
            if ((index - first) * GRANULE_SIZE > largest) largest = (index - first) * GRANULE_SIZE;
        }
    }
    UNLOCK_MUTEX
    fprintf(stderr, "  free %" PRIu64 " bytes, the largest free place %" PRIu64 " bytes\n", free_bytes, largest);
#if !defined(_WIN32)
    {
        char line[512];
        uintmax_t start, end;
        FILE *f = fopen("/proc/self/maps", "rb");

        if (f != NULL)
        {
            while (fgets(line, sizeof(line), f) != NULL)
            {
                if (sscanf(line, "%" SCNxMAX "-%" SCNxMAX, &start, &end) != 2) continue;
                if (end <= pointer_offset || start >= pointer_offset + GUEST_WINDOW_SIZE) continue;
                fprintf(stderr, "  host map: %s", line);
            }
            fclose(f);
        }
    }
#endif
}

/* the guest's heap: size bytes of the window, from the bottom up */
void *map_memory_32bit(unsigned int size)
{
    static int reported;
    void *mem;

    if (size == 0) return NULL;
    mem = window_map_memory(size);
    if (mem == NULL && !reported)
    {
        reported = 1;
        Game_MemoryReport("no room in the guest window", size);
    }
    return mem;
}

void unmap_memory_32bit(void *mem, unsigned int size)
{
    if (mem != NULL && size != 0) window_unmap_memory(mem, size);
}

void *map_memory_32bit_top(unsigned int size, uint32_t above)
{
    uint32_t count = granules_of(size), lowest = granules_of((uint64_t)above + GRANULE_SIZE), first;
    void *mem = NULL;

    if (size == 0 || count > GRANULE_COUNT) return NULL;
    LOCK_MUTEX
    for (first = GRANULE_COUNT - count + 1; first-- > lowest; )
    {
        if (run_is_free(first, count))
        {
            mem = use_granules(first, count);
            break;
        }
    }
    UNLOCK_MUTEX
    return mem;
}

int initialize_pointer_offset(void)
{
    uint64_t image_start, image_end, low, base;

    pointer_offset = 0;
    pointer_reserved_length = 0;
    low = (GAME_GUEST_LOW_MEMORY() + GRANULE_SIZE - 1) & ~(GRANULE_SIZE - 1);

    if (!find_host_image(&image_start, &image_end))
    {
        return 1;
    }

#if defined(__ANDROID__)
    /* android/gameloader.c has reserved the window and loaded the image
     * into it: ART takes the low 4 GB for itself, and System.loadLibrary puts
     * the image wherever the linker likes */
    {
        uint64_t size;

        if (!Game_AndroidGuestRegion(&base, &size))
        {
            return 4;
        }
        host_range_count = 0;
        add_host_range(image_start, image_end);
    }
#else
    if (collect_host_ranges((image_start > GUEST_WINDOW_SIZE) ? image_start - GUEST_WINDOW_SIZE : 0, image_start + GUEST_WINDOW_SIZE) != 0)
    {
        return 3;
    }

    // the window's bottom GUEST_IMAGE_AT below the image, or nearer to it
    // where something of the host's is in the way of the low memory
    base = (image_start > GUEST_IMAGE_AT + GRANULE_SIZE) ? ((image_start - GUEST_IMAGE_AT) & ~(GRANULE_SIZE - 1)) : GRANULE_SIZE;
    for (; base + low <= image_start; base += GRANULE_SIZE)
    {
        if (!host_overlaps(base, base + low)) break;
    }
    if (base + low > image_start)
    {
        fprintf(stderr, "guest memory: no room for the low memory (%" PRIu64 " bytes) below the image at %" PRIx64 "\n", low, image_start);
        return 4;
    }
#endif

    pointer_offset = base;
    pointer_reserved_length = (unsigned int)low;
    if (image_end > base + GUEST_WINDOW_SIZE || !window_from_host_ranges(image_start, image_end))
    {
        Game_MemoryReport("the guest window could not be set up", low);
        free(host_ranges);
        host_ranges = NULL;
        host_range_count = host_range_capacity = 0;
        pointer_offset = 0;
        pointer_reserved_length = 0;
        return 5;
    }
    free(host_ranges);
    host_ranges = NULL;
    host_range_count = host_range_capacity = 0;

#if defined(__ANDROID__)
    if (Game_AndroidCheckImageReach(pointer_offset, GUEST_WINDOW_SIZE) != 0)
    {
        return 5;
    }
#endif
    if (getenv("SR_SDK_MEMORY_MAP") != NULL)
    {
        Game_MemoryReport("start", 0);
    }

    if (ptr_initialize_pointers != NULL)
    {
        ptr_initialize_pointers(pointer_offset);
    }
    return 0;
}

/* The guest's first size bytes, memory (zeros) while the process lives:
 * initialize_pointer_offset keeps the low memory of the guest window
 * (pointer_reserved_length bytes) reserved with no access, and a program
 * that needs a part of it commits it here.  Every program has the same low
 * memory: the DOS memory of a DOS program, the data of a Windows program
 * kept at its original addresses (GAME_GUEST_DATA_IN_MEMORY), the page 0
 * of Windows 9x (GAME_GUEST_PAGE0). */
int commit_guest_low(unsigned int size)
{
    if (pointer_offset == 0 || size > pointer_reserved_length) return 1;
#ifdef _WIN32
    return (VirtualAlloc((void *)pointer_offset, size, MEM_COMMIT, PAGE_READWRITE) == NULL) ? 2 : 0;
#else
    return (mprotect((void *)pointer_offset, size, PROT_READ | PROT_WRITE) != 0) ? 2 : 0;
#endif
}

#endif /* __cplusplus */
