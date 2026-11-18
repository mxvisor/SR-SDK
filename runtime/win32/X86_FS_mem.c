/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#include <stdint.h>

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdio.h>
#include <stdlib.h>

uint32_t current_SEH_frame;
#endif
#include "platform.h"

#ifdef __cplusplus
extern "C"
#endif
uint32_t x86_guest_stack_bound(int top);    /* asm-cpu.c */

#ifdef __cplusplus
extern "C"
#endif
uint32_t CCALL X86_ReadFsDword(uint32_t addr)
{
    /* the bounds of the guest's stack (a C library's stack check: WATCOM's
     * reads StackLimit), not of the host thread's */
    if (addr == 4 || addr == 8)
    {
        return x86_guest_stack_bound(addr == 4);
    }
#ifdef _WIN32
    // addr == 0
    uint32_t *address;

    address = (uint32_t *) (addr + (uintptr_t)NtCurrentTeb());

    return *address;
#else
    if (addr == 0)
    {
        return current_SEH_frame;
    }
    else
    {
        fprintf(stderr, "fs:[0x%x]: a field of the TIB the port does not have (read)\n", (unsigned int)addr);
        exit(1);
    }
#endif
}

#ifdef __cplusplus
extern "C"
#endif
void CCALL X86_WriteFsDword(uint32_t addr, uint32_t value)
{
#ifdef _WIN32
    // addr == 0
    uint32_t *address;

    address = (uint32_t *) (addr + (uintptr_t)NtCurrentTeb());

    *address = value;
#else
    if (addr == 0)
    {
        current_SEH_frame = value;
    }
    else
    {
        fprintf(stderr, "fs:[0x%x]: a field of the TIB the port does not have (write)\n", (unsigned int)addr);
        exit(1);
    }
#endif
}

