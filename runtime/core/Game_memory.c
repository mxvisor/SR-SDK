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
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <pthread.h>
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

#if !defined(__APPLE__)
/* Game_hooks.h may set GAME_GUEST_LOW_MEMORY(): how much of the guest's
 * address space from pointer_offset up the program wants to be free of
 * the host (the low memory of the guest window, see map_memory_32bit). The
 * default is the low megabyte of the guest and the 64 KB after it. */
#if defined(GAME_GUEST_DATA_IN_MEMORY) && !defined(GAME_GUEST_LOW_MEMORY)
#include "Game_guestdata.h"
#define GAME_GUEST_LOW_MEMORY() Game_GuestLowMemory()
#endif
#ifndef GAME_GUEST_LOW_MEMORY
#define GAME_GUEST_LOW_MEMORY() (1024*1024 + 65536)
#endif
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


#ifdef PTROFS_64BIT
extern "C" void (*ptr_initialize_pointers)(uint64_t pointer_offset);

uint64_t pointer_offset;

unsigned int pointer_reserved_length;
#else
#define pointer_offset 0
#endif


int x86_init_malloc(void)
{
    if (sizeof(void *) > 4)
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
    else
    {
        return 0;
    }
}

void x86_deinit_malloc(void)
{
    if (sizeof(void *) > 4)
    {
#ifdef _WIN32
        DeleteCriticalSection(&mutex);
#else
        pthread_mutex_destroy(&mutex);
#endif
    }
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
    if (sizeof(void *) > 4)
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
            heapblock->address = (uint8_t *)map_memory_32bit(heapblock->size, 0);
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
    else
    {
        return malloc(size);
    }
}

void x86_free(void *ptr)
{
    if (sizeof(void *) > 4)
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
    else
    {
        free(ptr);
    }
}

void *x86_calloc(unsigned int nmemb, unsigned int size)
{
    if (sizeof(void *) > 4)
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
    else
    {
        return calloc(nmemb, size);
    }
}

void *x86_realloc(void *ptr, unsigned int size)
{
    if (sizeof(void *) > 4)
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
    else
    {
        return realloc(ptr, size);
    }
}


#if defined(PTROFS_64BIT) && !defined(__APPLE__)
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

static void *use_granules(uint32_t first, uint32_t count, int only_address_space)
{
    void *want = (void *)(uintptr_t)(pointer_offset + first * GRANULE_SIZE);

    if (!only_address_space)
    {
#ifdef _WIN32
        if (VirtualAlloc(want, count * GRANULE_SIZE, MEM_COMMIT, PAGE_READWRITE) != want) return NULL;
#else
        if (mmap(want, count * GRANULE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != want) return NULL;
#endif
    }
    set_granules(first, count, GRANULE_USED);
    return want;
}

static uint32_t granules_of(uint64_t size)
{
    return (uint32_t)((size + GRANULE_SIZE - 1) / GRANULE_SIZE);
}

/* from the bottom up, above the low memory; a multiple of the heap's blocks
 * on a boundary of them */
static void *window_map_memory(unsigned int size, int only_address_space)
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
            mem = use_granules(first, count, only_address_space);
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
#endif

/* stderr: the guest window, what is where in it, and what was wanted of it */
void Game_MemoryReport(const char *what, uint64_t wanted)
{
#if defined(PTROFS_64BIT) && !defined(__APPLE__)
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
#else
    (void)what;
    (void)wanted;
#endif
}

void *map_memory_32bit(unsigned int size, int only_address_space)
{
#if defined(PTROFS_64BIT) && !defined(__APPLE__)
    if (window_ready)
    {
        static int reported;
        void *mem = window_map_memory(size, only_address_space);

        if (mem == NULL && !reported)
        {
            reported = 1;
            Game_MemoryReport("no room in the guest window", size);
        }
        return mem;
    }
#endif
#ifdef _WIN32
    uint64_t maddr, reg_base, reg_size;
    void *mem;
    SYSTEM_INFO sinfo;
    MEMORY_BASIC_INFORMATION minfo;

    if (size == 0) return NULL;

    GetSystemInfo(&sinfo);

    // round up requested size to the next page boundary
    size = (size + (sinfo.dwPageSize - 1)) & ~(sinfo.dwPageSize - 1);

    // set starting memory address
    maddr = pointer_offset + 1024*1024 + 65536;

    // round up starting memory address to the nearest multiple of the allocation granularity
    maddr = (maddr + (sinfo.dwAllocationGranularity - 1)) & ~(uintptr_t)(sinfo.dwAllocationGranularity - 1);

    // look for unused memory up to 2GB from pointer_offset
    while (maddr < pointer_offset + UINT64_C(0x80000000) && maddr + size <= pointer_offset + UINT64_C(0x80000000))
    {
        if (0 == VirtualQuery((void *)maddr, &minfo, sizeof(minfo))) return NULL;

        if (minfo.State == MEM_FREE)
        {
            reg_base = (((uintptr_t)minfo.BaseAddress) + (sinfo.dwAllocationGranularity - 1)) & ~(uintptr_t)(sinfo.dwAllocationGranularity - 1);
            if (minfo.RegionSize >= reg_base - (uintptr_t)minfo.BaseAddress)
            {
                reg_size = minfo.RegionSize - (reg_base - (uintptr_t)minfo.BaseAddress);

                if (reg_size >= size)
                {
                    mem = VirtualAlloc((void *)reg_base, size, MEM_RESERVE | (only_address_space ? 0 : MEM_COMMIT), only_address_space ? PAGE_NOACCESS : PAGE_READWRITE);
                    if (mem != NULL) return mem;
                }
            }
        }

        maddr = ((minfo.RegionSize + (uintptr_t)minfo.BaseAddress) + (sinfo.dwAllocationGranularity - 1)) & ~(uintptr_t)(sinfo.dwAllocationGranularity - 1);
    }

    return NULL;
#elif defined(__APPLE__)
    void *mem, *start, *start_aligned;
    int prot, flags;
    long page_size;
    mach_port_t task;
    mach_vm_address_t region_address, free_region_start, free_region_end;
    mach_vm_size_t region_size;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count;
    mach_port_t object_name;

    if (size == 0) return NULL;

    prot = only_address_space ? PROT_NONE : (PROT_READ | PROT_WRITE);
    flags = MAP_PRIVATE | MAP_ANONYMOUS | (only_address_space ? MAP_NORESERVE : 0);

    // if platform supports MAP_32BIT, then try mapping memory with it
#if defined(MAP_32BIT) && (MAP_32BIT != 0)
#ifdef PTROFS_64BIT
    if (pointer_offset == 0)
#endif
    {
        mem = mmap(0, size, prot, MAP_32BIT | flags, -1, 0);
        if (mem != MAP_FAILED)
        {
            if (((uintptr_t)mem >= UINT64_C(0x80000000)) || (size + (uintptr_t)mem > UINT64_C(0x80000000)))
            {
                // mapped memory is above 2GB
                munmap(mem, size);
            }
            else
            {
                return mem;
            }
        }
    }
#endif

    // look for unused memory up to 2GB from pointer_offset and try mapping memory there
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    task = current_task();

    region_address = pointer_offset;
    count = VM_REGION_BASIC_INFO_COUNT_64;
    if (KERN_SUCCESS != mach_vm_region(task, &region_address, &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t) &info, &count, &object_name)) return NULL;

    // first free region (starting at address zero) belongs to segment __PAGEZERO, so don't try using memory here

    if (region_address >= pointer_offset + UINT64_C(0x80000000)) return NULL;

    free_region_start = region_address + region_size;
    while (free_region_start < pointer_offset + UINT64_C(0x80000000))
    {
        region_address = free_region_start;
        count = VM_REGION_BASIC_INFO_COUNT_64;
        if (KERN_SUCCESS != mach_vm_region(task, &region_address, &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t) &info, &count, &object_name))
        {
            region_address = free_region_end = pointer_offset + UINT64_C(0x80000000);
            region_size = 0;
        }
        else
        {
            free_region_end = region_address;
            if (free_region_end >= pointer_offset + UINT64_C(0x80000000))
            {
                free_region_end = pointer_offset + UINT64_C(0x80000000);
                region_size = 0;
            }
        }

        if (free_region_end - free_region_start >= size)
        {
            // try using memory at the end of the free region
            start = (void *)((free_region_end - size) & ~(uintptr_t)(page_size - 1));
            if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
            {
                start_aligned = (void *)(((uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
                if ((uintptr_t)start_aligned >= free_region_start + HEAPBLOCK_BASE_SIZE) start = start_aligned;
            }
            if (start != (void *)free_region_start)
            {
                mem = mmap(start, size, prot, MAP_FIXED | flags, -1, 0);
                if (mem == start) return mem;
                if (mem != MAP_FAILED)
                {
                    munmap(start, size);
                    goto error1;
                }
            }

            // try using memory at the start of the free region
            start = (void *)free_region_start;
            if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
            {
                start_aligned = (void *)(((HEAPBLOCK_BASE_SIZE - 1) + (uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
                if (size + (uintptr_t)start_aligned <= free_region_end) start = start_aligned;
            }
            mem = mmap(start, size, prot, MAP_FIXED | flags, -1, 0);
            if (mem == start) return mem;
            if (mem != MAP_FAILED)
            {
                munmap(start, size);
                goto error1;
            }
        }

        free_region_start = region_address + region_size;
    }

    return NULL;

error1:
    fprintf(stderr, "Error: memory mapped at different address\n");
    return NULL;
#else
    void *mem, *start, *start_aligned;
    int num_matches, prot, flags;
    long page_size;
    FILE *f;
    uintmax_t num0, num1, num2;

    if (size == 0) return NULL;

    prot = only_address_space ? PROT_NONE : (PROT_READ | PROT_WRITE);

#if !defined(MAP_NORESERVE) && defined(MAP_GUARD)
    flags = only_address_space ? MAP_GUARD : (MAP_PRIVATE | MAP_ANONYMOUS);
#else
    flags = MAP_PRIVATE | MAP_ANONYMOUS | (only_address_space ? MAP_NORESERVE : 0);
#endif

    // if platform supports MAP_32BIT, then try mapping memory with it
#if defined(MAP_32BIT) && (MAP_32BIT != 0)
#ifdef PTROFS_64BIT
    if (pointer_offset == 0)
#endif
    {
        mem = mmap(0, size, prot, MAP_32BIT | flags, -1, 0);
        if (mem != MAP_FAILED)
        {
            if (((uintptr_t)mem >= UINT64_C(0x80000000)) || (size + (uintptr_t)mem > UINT64_C(0x80000000)))
            {
                // mapped memory is above 2GB
                munmap(mem, size);
            }
            else
            {
                return mem;
            }
        }
    }
#endif

    // look for unused memory up to 2GB from pointer_offset in memory maps and try mapping memory there
    f = fopen("/proc/self/maps", "rb");
    if (f == NULL)
    {
        char mapname[32];
        sprintf(mapname, "/proc/%" PRIuMAX "/map", (uintmax_t)getpid());
        f = fopen(mapname, "rb");
        if (f == NULL) return NULL;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    mem = NULL;
    num0 = (pointer_offset + 1024*1024 + 65536 + (page_size - 1)) & ~(uintptr_t)(page_size - 1);
    while (num0 < pointer_offset + UINT64_C(0x80000000))
    {
        num_matches = fscanf(f, "%" SCNxMAX "%*[ -]%" SCNxMAX " %*[^\n^\r]%*[\n\r]", &num1, &num2);
        if (num_matches == EOF) break;

        if (num_matches < 2)
        {
            num0 = pointer_offset + UINT64_C(0x80000000);
            break;
        }

        // num1-num2 block is used
        // num0-num1 block is not used

        if (num1 >= pointer_offset + UINT64_C(0x80000000)) break;

        // num0-num1 block is below 2GB

        if ((num0 < num1) && (num1 - num0 >= size))
        {
            // try using memory at the end of the block
            start = (void *)((num1 - size) & ~(uintptr_t)(page_size - 1));
            if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
            {
                start_aligned = (void *)(((uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
                if ((uintptr_t)start_aligned >= num0 + HEAPBLOCK_BASE_SIZE) start = start_aligned;
            }
            if (start != (void *)num0)
            {
                mem = mmap(start, size, prot, MAP_FIXED_NOREPLACE | flags, -1, 0);
                if (mem == start) break;
                if (mem != MAP_FAILED)
                {
                    munmap(start, size);
                    goto error2;
                }
            }

            // try using memory at the start of the block
            start = (void *)num0;
            if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
            {
                start_aligned = (void *)(((HEAPBLOCK_BASE_SIZE - 1) + (uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
                if (size + (uintptr_t)start_aligned <= num1) start = start_aligned;
            }
            mem = mmap(start, size, prot, MAP_FIXED_NOREPLACE | flags, -1, 0);
            if (mem == start) break;
            if (mem != MAP_FAILED)
            {
                munmap(start, size);
                goto error2;
            }

            mem = NULL;
        }

        if (num2 > num0)
        {
            num0 = num2;
        }
    }

    fclose(f);

    if (mem != NULL) return mem;

    if ((num0 < pointer_offset + UINT64_C(0x80000000)) && (num0 + size <= pointer_offset + UINT64_C(0x80000000)))
    {
        // try using memory at the end of 2GB
        start = (void *)((pointer_offset + UINT64_C(0x80000000) - size) & ~(uintptr_t)(page_size - 1));
        if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
        {
            start_aligned = (void *)(((uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
            if ((uintptr_t)start_aligned >= num0 + HEAPBLOCK_BASE_SIZE) start = start_aligned;
        }
        if (start != (void *)num0)
        {
            mem = mmap(start, size, prot, MAP_FIXED_NOREPLACE | flags, -1, 0);
            if (mem == start) return mem;
            if (mem != MAP_FAILED)
            {
                munmap(start, size);
                goto error1;
            }
        }

        // try using memory after the end of the last block
        start = (void *)num0;
        if ((size & (HEAPBLOCK_BASE_SIZE - 1)) == 0)
        {
            start_aligned = (void *)(((HEAPBLOCK_BASE_SIZE - 1) + (uintptr_t)start) & ~(uintptr_t)(HEAPBLOCK_BASE_SIZE - 1));
            if (size + (uintptr_t)start_aligned <= pointer_offset + UINT64_C(0x80000000)) start = start_aligned;
        }
        mem = mmap(start, size, prot, MAP_FIXED_NOREPLACE | flags, -1, 0);
        if (mem == start) return mem;
        if (mem != MAP_FAILED)
        {
            munmap(start, size);
            goto error1;
        }
    }

    fprintf(stderr, "Error: no free memory for %u bytes in [%" PRIuMAX ", %" PRIuMAX ")\n",
            size, (uintmax_t)pointer_offset + 1024*1024 + 65536, (uintmax_t)pointer_offset + UINT64_C(0x80000000));
    return NULL;

error2:
    fclose(f);

error1:
    fprintf(stderr, "Error: memory mapped at different address\n");
    return NULL;
#endif
}

void unmap_memory_32bit(void *mem, unsigned int size)
{
    if (mem != NULL && size != 0)
    {
#if defined(PTROFS_64BIT) && !defined(__APPLE__)
        if (window_ready && (uintptr_t)mem >= pointer_offset && (uintptr_t)mem < pointer_offset + GUEST_WINDOW_SIZE)
        {
            window_unmap_memory(mem, size);
            return;
        }
#endif
#ifdef _WIN32
        VirtualFree(mem, 0, MEM_RELEASE);
#else
        munmap(mem, size);
#endif
    }
}

/* [guest, guest + size) as memory, if nothing else is there */
static void *map_at(uint32_t guest, unsigned int size)
{
    void *want = (void *)GAME_GUEST32_TO_PTR(guest);
#if defined(_WIN32)
    return VirtualAlloc(want, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif
    void *mem = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem == MAP_FAILED) return NULL;
    if (mem != want)
    {
        munmap(mem, size);
        return NULL;
    }
    return mem;
#endif
}

void *map_memory_32bit_top(unsigned int size, uint32_t above)
{
    uint32_t guest;

#if defined(PTROFS_64BIT) && !defined(__APPLE__)
    if (window_ready)
    {
        uint32_t count = granules_of(size), lowest = granules_of((uint64_t)above + GRANULE_SIZE), first;
        void *mem = NULL;

        if (count > GRANULE_COUNT) return NULL;
        LOCK_MUTEX
        for (first = GRANULE_COUNT - count + 1; first-- > lowest; )
        {
            if (run_is_free(first, count))
            {
                mem = use_granules(first, count, 0);
                break;
            }
        }
        UNLOCK_MUTEX
        return mem;
    }
#endif
    if (size == 0 || size > 0x80000000u) return NULL;
    for (guest = 0x80000000u - size; guest >= above + 0x10000u; guest -= 0x10000u)
    {
        void *mem = map_at(guest, size);
        if (mem != NULL) return mem;
    }
    return NULL;
}


#ifdef PTROFS_64BIT

int initialize_pointer_offset(void)
{
#if !defined(__APPLE__)
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
    if (image_start <= 2 * low)
    {
        // the image in the low megabytes: the guest's addresses are the host's
        if (ptr_initialize_pointers != NULL)
        {
            ptr_initialize_pointers(pointer_offset);
        }
        return 0;
    }

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
#elif defined(__APPLE__)
    Dl_info info;
    void *mem;
    long page_size;
    mach_port_t task;
    mach_vm_address_t region_address, maddr, free_offset;
    mach_vm_size_t region_size, min_length;
    vm_region_basic_info_data_64_t rinfo;
    mach_msg_type_number_t count;
    mach_port_t object_name;

    pointer_offset = 0;

    if (!dladdr(&ptr_initialize_pointers, &info))
    {
        return 1;
    }

    if (info.dli_fbase == NULL)
    {
        return 2;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    min_length = (1024*1024 + 65536 + (page_size - 1)) & ~(page_size - 1);

    if ((uintptr_t)info.dli_fbase <= 2*min_length)
    {
        pointer_reserved_length = 0;
        if (ptr_initialize_pointers != NULL)
        {
            ptr_initialize_pointers(pointer_offset);
        }
        return 0;
    }

    task = current_task();

    maddr = free_offset = 0;
    for (;;)
    {
        region_address = maddr;
        count = VM_REGION_BASIC_INFO_COUNT_64;
        if (KERN_SUCCESS != mach_vm_region(task, &region_address, &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t) &rinfo, &count, &object_name)) break;

        if (region_address - maddr >= min_length)
        {
            free_offset = region_address - min_length;
        }

        maddr = region_address + region_size;

        if ((uintptr_t)info.dli_fbase < maddr)
        {
            if (free_offset == 0) break;

            pointer_offset = free_offset;
            pointer_reserved_length = 0;

            // try reserving memory
            mem = mmap((void *)free_offset, min_length, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (mem == (void *)free_offset)
            {
                pointer_reserved_length = (unsigned int)min_length;
            }
            else
            {
                if (mem != MAP_FAILED) munmap(mem, min_length);
            }

            if (ptr_initialize_pointers != NULL)
            {
                ptr_initialize_pointers(pointer_offset);
            }
            return 0;
        }
    }

    return 4;
#endif
}

/* The first page of the guest (its address 0) readable and writable, zeros:
 * Windows 9x maps the DOS memory there, and a program that reads it faults
 * on NT, under Wine and in the reserved low memory of the port. */
int commit_guest_page0(void)
{
    if (pointer_reserved_length == 0) return 1;
#ifdef _WIN32
    return (VirtualAlloc((void *)pointer_offset, 4096, MEM_COMMIT, PAGE_READWRITE) == NULL) ? 2 : 0;
#else
    return (mprotect((void *)pointer_offset, 4096, PROT_READ | PROT_WRITE) != 0) ? 2 : 0;
#endif
}

#endif /* PTROFS_64BIT */

#endif /* __cplusplus */

