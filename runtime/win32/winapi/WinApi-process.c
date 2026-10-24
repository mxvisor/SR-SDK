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

/* The kernel32 functions that a Win32 program's startup (the CRT, the first
 * checks) and its memory, locale and time calls need, as the layer answers them:
 * the process, the heap (Heap*, Global*, Local*, Virtual*), code page and locale,
 * the standard handles, the environment, time, wide characters.  A game says
 * what is its own in Game_hooks.h:
 *
 *   GAME_MAIN_FILENAME      the original's file name (module file name, command line)
 *   GAME_MODULE_PATH_SEP    optional: "\\" or "/" (default) between the working
 *                           directory and the file name of GetModuleFileNameA
 *   GAME_EXTRA_PROCS(X)     optional: X(name) for each export besides the layer's own
 *                           that GetProcAddress hands out (name_asm2c must exist)
 *
 * The program's arguments come from Winapi_SetCommandLine (WinApi.h), which the
 * port's main() calls.  Guest pointers: the recompiled code stores 32-bit
 * addresses (host value minus pointer_offset); arguments arrive as raw uint32_t. */

#include "WinApi.h"
#include "Game_memory.h"
#include "CLIB.h"
#include "Game_hooks.h"
#include "WinApi-d3d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef WINAPI_MODULE_PATH_SEP
#ifdef GAME_MODULE_PATH_SEP
#define WINAPI_MODULE_PATH_SEP GAME_MODULE_PATH_SEP
#else
#define WINAPI_MODULE_PATH_SEP "/"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PTROFS_64BIT)
/* pointer_offset: Game_memory.h */
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))
#define GUEST_ADDR(p) ((uint32_t)((p) ? ((uintptr_t)(p) - pointer_offset) : 0))
#else
#define GUEST_MEM(a) ((void *)(uintptr_t)(a))
#define GUEST_ADDR(p) ((uint32_t)(uintptr_t)(p))
#endif

/* ---------------------------------------------------------------- version */

uint32_t CCALL GetVersion_c(void)
{
    /* Windows 95, version 4.0; the high bit marks 9x for IsWin9x checks. */
    return 0x80000004u;
}

uint32_t CCALL GetVersionExA_c(uint32_t a0)
{
    uint32_t *info;

    if (a0 == 0) return 0;
    info = (uint32_t *)GUEST_MEM(a0);
    /* OSVERSIONINFOA: Windows 95 (4.0, build 950, VER_PLATFORM_WIN32_WINDOWS) */
    if (info[0] < 20) return 0;
    info[1] = 4;
    info[2] = 0;
    info[3] = 950;
    info[4] = 1;
    if (info[0] >= 24) ((char *)info)[20] = 0;
    return 1;
}

/* -------------------------------------------------------- module / process */

uint32_t CCALL GetModuleHandleA_c(uint32_t a0)
{
    (void)a0;
    /* The original ImageBase; only ever compared for equality/passed to the
     * window class, so any stable non-null value works. */
    return 0x00400000u;
}

uint32_t CCALL GetModuleFileNameA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    char path[4096];
    char *dst;
    size_t length;

    (void)a0;
    if (a1 == 0 || a2 == 0) return 0;
    if (getcwd(path, sizeof(path)) != NULL)
    {
        size_t used = strlen(path);
        /* the program cuts the file name off at the last separator */
        if (used + sizeof(GAME_MAIN_FILENAME) + 2 < sizeof(path)) strcat(path, WINAPI_MODULE_PATH_SEP GAME_MAIN_FILENAME);
    }
    else
    {
        strcpy(path, GAME_MAIN_FILENAME);
    }
    dst = (char *)GUEST_MEM(a1);
    length = strlen(path);
    if (length >= a2) length = a2 - 1;
    memcpy(dst, path, length);
    dst[length] = 0;
    return (uint32_t)length;
}

static char *winapi_command_line;
static int winapi_argc;
static char **winapi_argv;

/* main.c hands over the port's arguments (its own options already taken out);
 * the program reads its switches from the command line the CRT gets here. */
void Winapi_SetCommandLine(int argc, char *argv[])
{
    winapi_argc = argc;
    winapi_argv = argv;
}

uint32_t CCALL GetCommandLineA_c(void)
{
    if (winapi_command_line == NULL)
    {
        static const char program[] = "\"" GAME_MAIN_FILENAME "\"";
        size_t length = sizeof(program);
        int index;
        char *text;

        for (index = 1; index < winapi_argc; index++) length += strlen(winapi_argv[index]) + 3;
#ifdef GAME_DIRECT3D_ARG
        length += sizeof(GAME_DIRECT3D_ARG) + 1;
#endif
        winapi_command_line = (char *)x86_malloc((unsigned int)length);
        if (winapi_command_line == NULL) return 0;

        text = winapi_command_line;
        strcpy(text, program);
        for (index = 1; index < winapi_argc; index++)
        {
            /* the CRT splits at blanks; quote an argument that has one */
            int quote = (strchr(winapi_argv[index], ' ') != NULL) || (strchr(winapi_argv[index], '\t') != NULL);
            text += strlen(text);
            sprintf(text, quote ? " \"%s\"" : " %s", winapi_argv[index]);
        }
#ifdef GAME_DIRECT3D_ARG
        /* the switch with which the game takes its Direct3D device */
        if (Winapi_Direct3DEnabled()) strcat(text, " " GAME_DIRECT3D_ARG);
#endif
    }
    return GUEST_ADDR(winapi_command_line);
}

uint32_t CCALL GetStartupInfoA_c(uint32_t a0)
{
    if (a0 != 0)
    {
        /* STARTUPINFOA is 68 bytes on Win32; cb must be its size. */
        memset(GUEST_MEM(a0), 0, 68);
        *(uint32_t *)GUEST_MEM(a0) = 68;
    }
    return 0;
}

/* ------------------------------------------------------------ library load */

uint32_t CCALL LoadLibraryA_c(uint32_t a0)
{
    (void)a0;
    /* Pseudo handle; GetProcAddress ignores it.  Never 0 so a delay-load
     * helper sees success. */
    return 0x00060000u;
}

struct winapi_proc { const char *name; void (*proc)(void); };

#define WINAPI_PROC_EXTERN(name) extern void name##_asm2c(void);
#define WINAPI_PROC_ENTRY(name) { #name, name##_asm2c },
WINAPI_PROC_EXTERN(MessageBoxA)
WINAPI_PROC_EXTERN(DirectDrawCreate)
WINAPI_PROC_EXTERN(DirectSoundCreate)
#ifdef GAME_EXTRA_PROCS
GAME_EXTRA_PROCS(WINAPI_PROC_EXTERN)
#endif

/* what the guest may ask for with GetProcAddress: the exports of the layer that
 * a program loads late; a game adds its own with GAME_EXTRA_PROCS(X) in
 * Game_hooks.h (X(DirectInputCreateA) X(...)) */
static const struct winapi_proc winapi_proc_table[] = {
    WINAPI_PROC_ENTRY(MessageBoxA)
    WINAPI_PROC_ENTRY(DirectDrawCreate)
    WINAPI_PROC_ENTRY(DirectSoundCreate)
#ifdef GAME_EXTRA_PROCS
    GAME_EXTRA_PROCS(WINAPI_PROC_ENTRY)
#endif
};

uint32_t CCALL GetProcAddress_c(uint32_t a0, uint32_t a1)
{
    const char *name;

    (void)a0;
    if (a1 == 0) return 0;
    name = (const char *)GUEST_MEM(a1);
    for (unsigned int i = 0; i < sizeof(winapi_proc_table) / sizeof(winapi_proc_table[0]); i++)
    {
        if (strcmp(name, winapi_proc_table[i].name) == 0)
        {
            return GUEST_ADDR((void *)winapi_proc_table[i].proc);
        }
    }
    return 0;
}

/* ------------------------------------------------------------- exceptions */

static uint32_t winapi_prev_filter;

uint32_t CCALL SetUnhandledExceptionFilter_c(uint32_t a0)
{
    uint32_t old = winapi_prev_filter;
    winapi_prev_filter = a0;
    return old;
}

uint32_t CCALL UnhandledExceptionFilter_c(uint32_t a0)
{
    (void)a0;
    return 1; /* EXCEPTION_EXECUTE_HANDLER */
}

/* -------------------------------------------------------------- heap/alloc */

#define WINAPI_HEAP_MAGIC  0x544e5048u
#define WINAPI_HEAP_ZERO   0x00000008u

typedef struct { uint32_t magic; uint32_t size; } winapi_alloc_hdr;

static int winapi_heaps[8];

static uint32_t winapi_alloc(uint32_t flags, uint32_t size)
{
    uint32_t alloc_size = size;
    if (alloc_size == 0) alloc_size = 1;
    winapi_alloc_hdr *block = (winapi_alloc_hdr *)x86_malloc(alloc_size + sizeof(winapi_alloc_hdr));
    if (block == NULL) { fprintf(stderr, "[winapi_alloc FAIL size=%x]\n", size); return 0; }
    /* Always zeroed: Windows hands out fresh zero pages for most early allocations
       and the program's C++ code (e.g. a display mode list) relies on that. */
    (void)flags;
    memset(block + 1, 0, alloc_size);
    block->magic = WINAPI_HEAP_MAGIC;
    block->size = size;
    return GUEST_ADDR(block + 1);
}

static int winapi_release(uint32_t address)
{
    winapi_alloc_hdr *block;
    if (address == 0) return 1;
    void *host = GUEST_MEM(address);
    block = (winapi_alloc_hdr *)((char *)host - sizeof(winapi_alloc_hdr));
    /* Basic sanity check */
    if ((uintptr_t)block < 0x1000) return 0;
    if (block->magic != WINAPI_HEAP_MAGIC) return 0;
    block->magic = 0;
    x86_free(block);
    return 1;
}

uint32_t CCALL HeapCreate_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    for (int i = 0; i < 8; i++)
    {
        if (!winapi_heaps[i]) { winapi_heaps[i] = 1; return 0x70000000u + (uint32_t)i; }
    }
    return 0;
}

uint32_t CCALL HeapDestroy_c(uint32_t a0)
{
    (void)a0;
    return 1;
}

uint32_t CCALL HeapAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t r;
    (void)a0;
    r = winapi_alloc(a1, a2);
    return r;
}

uint32_t CCALL HeapFree_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t r;
    (void)a0; (void)a1;
    r = (uint32_t)winapi_release(a2);
    return r;
}

uint32_t CCALL HeapSize_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    winapi_alloc_hdr *block;
    (void)a0; (void)a1;
    if (a2 == 0) return (uint32_t)-1;
    block = (winapi_alloc_hdr *)GUEST_MEM(a2) - 1;
    if (block->magic != WINAPI_HEAP_MAGIC) return (uint32_t)-1;
    return block->size;
}

uint32_t CCALL HeapReAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    winapi_alloc_hdr *block, *grown;
    uint32_t old_size;

    (void)a0;
    if (a2 == 0) return winapi_alloc(a1, a3);
    block = (winapi_alloc_hdr *)GUEST_MEM(a2) - 1;
    if (block->magic != WINAPI_HEAP_MAGIC) return 0;
    old_size = block->size;
    grown = (winapi_alloc_hdr *)x86_realloc(block, a3 + sizeof(winapi_alloc_hdr));
    if (grown == NULL) return 0;
    grown->magic = WINAPI_HEAP_MAGIC;
    grown->size = a3;
    if ((a1 & WINAPI_HEAP_ZERO) && a3 > old_size) memset((uint8_t *)(grown + 1) + old_size, 0, a3 - old_size);
    return GUEST_ADDR(grown + 1);
}

uint32_t CCALL GlobalAlloc_c(uint32_t a0, uint32_t a1)
{
    /* GMEM_ZEROINIT (0x40) must zero the block; MOVEABLE/other flags are
     * treated as a plain fixed allocation. */
    return winapi_alloc((a0 & 0x0040u) ? WINAPI_HEAP_ZERO : 0, a1);
}

uint32_t CCALL GlobalLock_c(uint32_t a0)
{
    return a0; /* a handle is the address */
}

uint32_t CCALL GlobalUnlock_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

uint32_t CCALL GlobalHandle_c(uint32_t a0)
{
    return a0;
}

uint32_t CCALL GlobalFree_c(uint32_t a0)
{
    winapi_release(a0);
    return 0;
}

uint32_t CCALL GlobalSize_c(uint32_t a0)
{
    winapi_alloc_hdr *block;
    if (a0 == 0) return 0;
    block = (winapi_alloc_hdr *)GUEST_MEM(a0) - 1;
    if (block->magic != WINAPI_HEAP_MAGIC) return 0;
    return block->size;
}

uint32_t CCALL GlobalMemoryStatus_c(uint32_t a0)
{
    if (a0 != 0)
    {
        uint32_t *out = (uint32_t *)GUEST_MEM(a0);
        memset(out, 0, 32);
        out[0] = 32;          /* dwLength */
        out[1] = 50;          /* dwMemoryLoad */
        out[2] = 0x40000000u; /* dwTotalPhys  (1 GB, saturated) */
        out[3] = 0x20000000u; /* dwAvailPhys */
        out[4] = 0x40000000u; /* dwTotalPageFile */
        out[5] = 0x20000000u; /* dwAvailPageFile */
        out[6] = 0x7fffffffu; /* dwTotalVirtual */
        out[7] = 0x7fffffffu; /* dwAvailVirtual */
    }
    return 0;
}

uint32_t CCALL LocalFree_c(uint32_t a0)
{
    winapi_release(a0);
    return 0;
}

/* A program's own pool allocator reserves a large contiguous region with
 * VirtualAlloc(MEM_RESERVE) and then commits sub-ranges inside it with
 * VirtualAlloc(addr, size, MEM_COMMIT) -- and crucially checks that the commit
 * returns the exact requested address (`cmp eax, edi` after the call).  So we map
 * the whole reserve range up front and let commits return their address. */
#define VA_MAX_RESERVE 128
typedef struct { uint32_t base; uint32_t size; void *raw; } va_reserve_t;
static va_reserve_t va_res[VA_MAX_RESERVE];
static int va_res_n = 0;

uint32_t CCALL VirtualAlloc_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    void *mem;
    uint32_t r;
    (void)a3;
    if (a2 & 0x00001000u) /* MEM_COMMIT */
    {
        if (a0 != 0)
        {
            /* Commit at a specific address inside a previously reserved (and
             * therefore already mapped) range. */
            if (a1 != 0) memset(GUEST_MEM(a0), 0, a1);
            return a0;
        }
        mem = x86_malloc(a1);
        if (mem != NULL) memset(mem, 0, a1);
        r = GUEST_ADDR(mem);
        return r;
    }

    /* MEM_RESERVE: map the whole range now so later commits just return their
     * address without further mapping. */
    if (a1 == 0) return 0;
    /* Windows hands out 64 KB-aligned reservations; the MSVC small-block heap
     * derives page/group indices from that alignment, so over-allocate and
     * align up (a misaligned base made free() miss its own blocks). */
    mem = x86_malloc(a1 + 0x10000u);
    if (mem == NULL) return 0;
    {
        void *raw = mem;
        uint32_t base;
        base = (GUEST_ADDR(raw) + 0xffffu) & ~0xffffu;
        mem = GUEST_MEM(base);
        memset(mem, 0, a1);
        if (va_res_n < VA_MAX_RESERVE)
        {
            va_res[va_res_n].base = base;
            va_res[va_res_n].size = a1;
            va_res[va_res_n].raw = raw;
            va_res_n++;
        }
        return base;
    }
}

uint32_t CCALL VirtualFree_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a1;
    if (a0 == 0) return 1;
    if (a2 & 0x00008000u) /* MEM_RELEASE */
    {
        int i;
        for (i = 0; i < va_res_n; i++)
        {
            if (va_res[i].base == a0)
            {
                x86_free(va_res[i].raw);
                va_res[i].base = 0;
                return 1;
            }
        }
        /* not a tracked reserve base: nothing to release */
        return 1;
    }
    /* MEM_DECOMMIT: our reserve range stays mapped, so this is a no-op */
    return 1;
}

uint32_t CCALL IsBadReadPtr_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 0;
}

uint32_t CCALL IsBadWritePtr_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 0;
}

uint32_t CCALL IsBadCodePtr_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

/* ----------------------------------------------------------------- codepage */

uint32_t CCALL GetACP_c(void) { return 1252; }
uint32_t CCALL GetOEMCP_c(void) { return 437; }

uint32_t CCALL IsValidCodePage_c(uint32_t a0)
{
    (void)a0;
    return 1;
}

uint32_t CCALL IsValidLocale_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 1;
}

uint32_t CCALL GetUserDefaultLCID_c(void) { return 0x0409; } /* en-US */

uint32_t CCALL GetCPInfo_c(uint32_t /*a0*/, uint32_t a1)
{
    if (a1 == 0) return 0;
    memset(GUEST_MEM(a1), 0, 20);
    return 1;
}

/* --------------------------------------------------------------- env / std */

uint32_t CCALL GetStdHandle_c(uint32_t a0)
{
    (void)a0;
    return (uint32_t)-1; /* INVALID_HANDLE_VALUE; a GUI program */
}

uint32_t CCALL SetStdHandle_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 1;
}

uint32_t CCALL GetFileType_c(uint32_t a0)
{
    (void)a0;
    return 1; /* FILE_TYPE_DISK: the CRT _open() fails on FILE_TYPE_UNKNOWN */
}

uint32_t CCALL SetHandleCount_c(uint32_t a0)
{
    return a0;
}

uint32_t CCALL FlushFileBuffers_c(uint32_t a0)
{
    (void)a0;
    return 1;
}

uint32_t CCALL SetEndOfFile_c(uint32_t a0)
{
    (void)a0;
    return 1;
}

uint32_t CCALL GetEnvironmentStrings_c(void)
{
    static char empty[2];
    return GUEST_ADDR(empty);
}

uint32_t CCALL GetEnvironmentStringsW_c(void)
{
    static uint16_t empty[2];
    return GUEST_ADDR(empty);
}

uint32_t CCALL FreeEnvironmentStringsA_c(uint32_t a0) { (void)a0; return 1; }
uint32_t CCALL FreeEnvironmentStringsW_c(uint32_t a0) { (void)a0; return 1; }

/* ------------------------------------------------------------- clib strings */

uint32_t CCALL lstrlenA_c(uint32_t a0)
{
    return (uint32_t)strlen((const char *)GUEST_MEM(a0));
}

uint32_t CCALL lstrcpyA_c(uint32_t a0, uint32_t a1)
{
    char *dst = (char *)GUEST_MEM(a0);
    const char *src = (const char *)GUEST_MEM(a1);
    strcpy(dst, src);
    return a0;
}

uint32_t CCALL OutputDebugStringA_c(uint32_t a0)
{
    /* the program says why it gives up (no DirectX, ...) */
    if (a0 != 0) fprintf(stderr, "OutputDebugString: %s", (const char *)GUEST_MEM(a0));
    return 0;
}

/* -------------------------------------------------------------- file attrs */

uint32_t CCALL GetFileAttributesA_c(uint32_t a0)
{
    char real[4096];
    if (a0 == 0 || !CLIB_FindFile((const char *)GUEST_MEM(a0), real)) return 0xffffffffu; /* INVALID_FILE_ATTRIBUTES */
    return 0x00000080u; /* FILE_ATTRIBUTE_NORMAL */
}

/* ------------------------------------------------------------------- time */

uint32_t CCALL GetSystemTime_c(uint32_t a0)
{
    time_t now;
    struct tm parts;
    uint16_t *out;

    if (a0 == 0) return 0;
    now = time(NULL);
    parts = *gmtime(&now);
    out = (uint16_t *)GUEST_MEM(a0);
    out[0] = (uint16_t)(parts.tm_year + 1900);
    out[1] = (uint16_t)(parts.tm_mon + 1);
    out[2] = (uint16_t)parts.tm_wday;
    out[3] = (uint16_t)parts.tm_mday;
    out[4] = (uint16_t)parts.tm_hour;
    out[5] = (uint16_t)parts.tm_min;
    out[6] = (uint16_t)parts.tm_sec;
    out[7] = 0;
    return 0;
}

uint32_t CCALL GetLocalTime_c(uint32_t a0)
{
    time_t now;
    struct tm parts;
    uint16_t *out;

    if (a0 == 0) return 0;
    now = time(NULL);
    parts = *localtime(&now);
    out = (uint16_t *)GUEST_MEM(a0);
    out[0] = (uint16_t)(parts.tm_year + 1900);
    out[1] = (uint16_t)(parts.tm_mon + 1);
    out[2] = (uint16_t)parts.tm_wday;
    out[3] = (uint16_t)parts.tm_mday;
    out[4] = (uint16_t)parts.tm_hour;
    out[5] = (uint16_t)parts.tm_min;
    out[6] = (uint16_t)parts.tm_sec;
    out[7] = 0;
    return 0;
}

uint32_t CCALL GetTimeZoneInformation_c(uint32_t a0)
{
    if (a0 != 0) memset(GUEST_MEM(a0), 0, 172);
    return 0; /* TIME_ZONE_ID_UNKNOWN */
}

/* ------------------------------------------------------- locale conversion */

/* Western single-byte approximation of the ANSI/OEM codepages: enough for the
 * CRT startup (command line, environment) and the program's own strings. */
uint32_t CCALL WideCharToMultiByte_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                     uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    const uint16_t *wide = (const uint16_t *)GUEST_MEM(a2);
    char *out = (char *)GUEST_MEM(a4);

    (void)a0; (void)a1; (void)a6; (void)a7;
    if (wide == NULL) return 0;
    if (a3 == (uint32_t)-1)
    {
        a3 = 0;
        while (wide[a3] != 0) a3++;
        a3++; /* include the terminating null */
    }
    if (a5 == 0) return a3;                  /* caller asks for the size */
    if (a4 == 0 || a5 < a3) return 0;
    for (uint32_t i = 0; i < a3; i++)
    {
        uint16_t c = wide[i];
        out[i] = (c < 0x100) ? (char)c : '?';
    }
    return a3;
}

uint32_t CCALL MultiByteToWideChar_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                     uint32_t a4, uint32_t a5)
{
    const uint8_t *mb = (const uint8_t *)GUEST_MEM(a2);
    uint16_t *wide = (uint16_t *)GUEST_MEM(a4);

    (void)a0; (void)a1;
    if (mb == NULL) return 0;
    if (a3 == (uint32_t)-1) a3 = (uint32_t)strlen((const char *)mb) + 1;
    if (a5 == 0) return a3;
    if (a4 == 0 || a5 < a3) return 0;
    for (uint32_t i = 0; i < a3; i++) wide[i] = (uint16_t)mb[i];
    return a3;
}

uint32_t CCALL GetStringTypeW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint16_t *src = (const uint16_t *)GUEST_MEM(a1);
    uint16_t *out = (uint16_t *)GUEST_MEM(a3);

    (void)a0;
    if (src == NULL || out == NULL) return 0;
    if (a2 == (uint32_t)-1)
    {
        a2 = 0;
        while (src[a2] != 0) a2++;
        a2++;
    }
    for (uint32_t i = 0; i < a2 && src[i] != 0; i++)
    {
        uint16_t c = src[i], type = 0;
        if (c < 0x80)
        {
            if (c >= 'A' && c <= 'Z') type |= 0x0001;
            if (c >= 'a' && c <= 'z') type |= 0x0002;
            if (c >= '0' && c <= '9') type |= 0x0004;
            if (c == ' ' || (c >= 9 && c <= 13)) type |= 0x0008;
        }
        out[i] = type;
    }
    return 1;
}

uint32_t CCALL GetStringTypeA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    const uint8_t *src = (const uint8_t *)GUEST_MEM(a2);
    uint16_t *out = (uint16_t *)GUEST_MEM(a4);

    (void)a0; (void)a1;
    if (src == NULL || out == NULL) return 0;
    if (a3 == (uint32_t)-1) a3 = (uint32_t)strlen((const char *)src) + 1;
    for (uint32_t i = 0; i < a3 && src[i] != 0; i++)
    {
        uint16_t c = src[i], type = 0;
        if (c >= 'A' && c <= 'Z') type |= 0x0001;
        if (c >= 'a' && c <= 'z') type |= 0x0002;
        if (c >= '0' && c <= '9') type |= 0x0004;
        if (c == ' ' || (c >= 9 && c <= 13)) type |= 0x0008;
        out[i] = type;
    }
    return 1;
}

uint32_t CCALL GetLocaleInfoA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1;
    if (a2 != 0 && a3 != 0) *(char *)GUEST_MEM(a2) = 0;
    return 0;
}

uint32_t CCALL GetLocaleInfoW_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1;
    if (a2 != 0 && a3 != 0) *(uint16_t *)GUEST_MEM(a2) = 0;
    return 0;
}

#ifdef __cplusplus
}
#endif
