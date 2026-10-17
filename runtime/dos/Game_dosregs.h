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

#ifndef _GAME_DOSREGS_H_INCLUDED_
#define _GAME_DOSREGS_H_INCLUDED_

#include <stdint.h>

/* The registers of a software interrupt, as SR's X86_InterruptProcedure
 * hands them over: pushad order, then the flags.  It is also the start of the
 * register structure of DPMI 0300h, where the word after the flags is ES. */
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t ignored_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eflags;
} Game_DosRegs;

#define GAME_DOS_CF 0x0001u
#define GAME_DOS_ZF 0x0040u

/* A 16-bit or 8-bit result leaves the rest of its 32-bit register as it was,
 * as DOS and the DPMI host do. */
static inline uint8_t Game_DosAL(const Game_DosRegs *regs) { return (uint8_t)regs->eax; }
static inline uint8_t Game_DosAH(const Game_DosRegs *regs) { return (uint8_t)(regs->eax >> 8); }
static inline uint16_t Game_DosAX(const Game_DosRegs *regs) { return (uint16_t)regs->eax; }
static inline uint16_t Game_DosBX(const Game_DosRegs *regs) { return (uint16_t)regs->ebx; }
static inline uint16_t Game_DosCX(const Game_DosRegs *regs) { return (uint16_t)regs->ecx; }
static inline uint16_t Game_DosDX(const Game_DosRegs *regs) { return (uint16_t)regs->edx; }

static inline void Game_DosSetAL(Game_DosRegs *regs, uint8_t value)
{
    regs->eax = (regs->eax & 0xffffff00u) | value;
}

static inline void Game_DosSetAX(Game_DosRegs *regs, uint16_t value)
{
    regs->eax = (regs->eax & 0xffff0000u) | value;
}

static inline void Game_DosSetBX(Game_DosRegs *regs, uint16_t value)
{
    regs->ebx = (regs->ebx & 0xffff0000u) | value;
}

static inline void Game_DosSetCX(Game_DosRegs *regs, uint16_t value)
{
    regs->ecx = (regs->ecx & 0xffff0000u) | value;
}

static inline void Game_DosSetDX(Game_DosRegs *regs, uint16_t value)
{
    regs->edx = (regs->edx & 0xffff0000u) | value;
}

static inline void Game_DosSetCarry(Game_DosRegs *regs, int value)
{
    if (value) regs->eflags |= GAME_DOS_CF;
    else regs->eflags &= ~GAME_DOS_CF;
}

/* AX = the error code, CF set */
static inline void Game_DosError(Game_DosRegs *regs, uint16_t error)
{
    Game_DosSetAX(regs, error);
    Game_DosSetCarry(regs, 1);
}

#endif
