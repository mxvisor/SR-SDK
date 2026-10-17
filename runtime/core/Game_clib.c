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

/* Replacements for WATCOM C library routines of the original, which the
 * game's external_procedures.sci routes here (the .sci names the addresses,
 * this file does not): printf, sprintf, _nmalloc and _nfree.  kbhit and getch
 * are in runtime/dos/Game_doskbd.c. */

#include <stdint.h>
#include <stdio.h>

#include "printf_x86.h"
#include "Game_memory.h"

extern "C"
{
    static void stdout_char(char character, void *arg)
    {
        fputc(character, (FILE *)arg);
    }

    /* WATCOM's printf (INT 21h AH=40h through the guest's own stdio).
     *
     * The point is not speed but flushing. WATCOM line-buffers stdout, so
     * output without a trailing newline (a "Loading" banner with one "."
     * per resource) sits in the guest's buffer with nothing to push it out
     * and is still unprinted when the game switches to graphics. Under DOS
     * it shows, as DOS writes reach the screen as they are made. Formatting
     * host-side and flushing per call restores that.
     *
     * `ap` points at the caller's varargs on the guest stack; printf_x86.c
     * reads them 32-bit-at-a-time, which is what the guest pushed. */
    int32_t Game_vprintf(const char *format, uint32_t *ap)
    {
        int res;

        res = vfctprintf_x86(stdout_char, stdout, format, ap);
        fflush(stdout);

        return res;
    }

    /* WATCOM's sprintf: __prtf with a store-to-memory output routine and a
     * terminating NUL written at buf[result]; vsprintf_x86 does the same. */
    int32_t Game_vsprintf(char *buf, const char *format, uint32_t *ap)
    {
        return vsprintf_x86(buf, format, ap);
    }

    /* WATCOM's _nmalloc (also reached through Miles AIL's AIL_mem_use_malloc
     * hook, which holds its address). The two range checks are _nmalloc's
     * own: 0 and anything above 0xFFFFFFD4 return NULL without allocating. */
    void *Game_malloc(uint32_t size)
    {
        if (size == 0 || size > 0xFFFFFFD4u) return NULL;
        return x86_malloc(size);
    }

    /* WATCOM's _nfree (and AIL_mem_use_free). Every free site of a game that
     * replaces _nmalloc releases a pointer that came from it, so both move
     * together. */
    void Game_free(void *ptr)
    {
        x86_free(ptr);
    }
}
