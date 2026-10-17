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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/timeb.h>
#else
#include <link.h>
#include <sys/time.h>
#endif

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_vfs.h"
#include "Game_dosint.h"
#include "Game_dosmem.h"
#include "Game_dosfile.h"
#include "Game_doskbd.h"
#include "Game_doscd.h"
#include "Game_dospit.h"
#include "Game_hooks.h"
#include "llasm_cpu.h"

#undef eax
#undef ebx
#undef ecx
#undef edx
#undef esi
#undef edi
#undef ebp
#undef esp
#undef eflags

/* the game's llasm glue: a tail call to the translated procedure in ecx, and
 * the end of the program as INT 21h AH=4Ch asks for it */
extern "C" void CCALL c_RunProcECX(CPU);
extern "C" void CCALL Game_ExitMain_Asm(int32_t status);
extern "C" _cpu *x86_initialize_cpu(void);

#define MAX_VECTORS 256

static uint32_t vectors[MAX_VECTORS];
/* Set while a guest handler runs.  Translated code checks for timers at every
 * backward jump, inside a handler too, so a handler could otherwise be entered
 * again before it returns. */
static int isr_running;
/* The executable code the translated procedures are in.  Not every value a
 * game puts into a vector is one: WATCOM's start-up leaves placeholders there
 * that were only ever compared or handed back, and calling one through
 * c_RunProcECX is a jump to garbage. */
static uintptr_t code_start;
static uintptr_t code_end;

/* <GAME>_TRACE_INT: every software interrupt with its registers before and after */
static int trace_int(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_INT")) != NULL;
    return enabled;
}

#if !defined(_WIN32)
static int find_code(struct dl_phdr_info *info, size_t size, void *data)
{
    const uintptr_t probe = (uintptr_t)&c_RunProcECX;
    (void)size;
    (void)data;
    for (unsigned int index = 0; index < (unsigned int)info->dlpi_phnum; index++) {
        const ElfW(Phdr) *header = &info->dlpi_phdr[index];
        const uintptr_t start = (uintptr_t)info->dlpi_addr + header->p_vaddr;
        if (header->p_type != PT_LOAD || (header->p_flags & PF_X) == 0) continue;
        if (probe < start || probe >= start + header->p_memsz) continue;
        code_start = start;
        code_end = start + header->p_memsz;
        return 1;
    }
    return 0;
}
#endif

/* The code section of the module c_RunProcECX is in.  On Windows from the
 * section table: VirtualQuery describes the pages from the one asked about
 * on, not the section around it. */
static void find_code_bounds(void)
{
#if defined(_WIN32)
    const uintptr_t probe = (uintptr_t)&c_RunProcECX;
    HMODULE module;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)probe, &module)) return;
    const uint8_t *base = (const uint8_t *)module;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections; index++, section++) {
        const uintptr_t start = (uintptr_t)base + section->VirtualAddress;
        const uintptr_t end = start + section->Misc.VirtualSize;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || probe < start || probe >= end) continue;
        code_start = start;
        code_end = end;
        return;
    }
#else
    dl_iterate_phdr(find_code, NULL);
#endif
}

static int code_address_valid(uint32_t address)
{
    const uintptr_t pointer = (uintptr_t)Game_DosGuestPointer(address);
    return pointer >= code_start && pointer < code_end;
}

void *Game_DosGuestPointer(uint32_t address)
{
    return (address == 0) ? NULL : (void *)GAME_GUEST32_TO_PTR(address);
}

void Game_DosIntInit(uint32_t null_isr)
{
    for (unsigned int vector = 0; vector < MAX_VECTORS; vector++) vectors[vector] = null_isr;
    isr_running = 0;
    if (code_end == 0) find_code_bounds();
    if (code_end == 0) fprintf(stderr, GAME_NAME ": the translated code was not found; the game's interrupt handlers will not run\n");
}

uint32_t Game_DosGetVector(uint8_t vector)
{
    return vectors[vector];
}

void Game_DosSetVector(uint8_t vector, uint32_t address)
{
    vectors[vector] = address;
}

int Game_DosGuestInterruptsEnabled(void)
{
    return X86_InterruptFlag != 0 && !isr_running;
}

/* Runs the guest code at target with `words` dwords pushed (frame[0] ends up
 * at [esp]) and eflags with IF set.  A hardware interrupt leaves no trace in
 * the code it interrupts, so all of the CPU state is restored afterwards. */
static int run_guest(uint32_t target, const uint32_t *frame, unsigned int words, uint32_t eax)
{
    if (!code_address_valid(target) || !Game_DosGuestInterruptsEnabled()) return 0;

    _cpu *cpu = x86_initialize_cpu();
    const _cpu saved = *cpu;
    const uint32_t saved_flag = X86_InterruptFlag;

    cpu->_eflags = 0x3202u;
    cpu->_eax = eax;
    cpu->_ecx = target;
    cpu->_esp -= 4u * words;
    memcpy(Game_DosGuestPointer(cpu->_esp), frame, 4u * words);
    isr_running = 1;
    c_RunProcECX(cpu);
    isr_running = 0;

    *cpu = saved;
    X86_InterruptFlag = saved_flag;
    return 1;
}

int Game_DosCallGuestIsr(uint32_t target)
{
    /* What the handler's iret pops: the return address, then CS and the flags
     * it restores (IF set, as on entry to a hardware interrupt).  0 is never a
     * translated procedure's address; the epilogues of translated code take a
     * return to it as the way back to their native caller.  With the return
     * address alone, iret popped CS and the flags from whatever lay above the
     * stack, and a handler that chains to another (WATCOM's _chain_intr)
     * rearranges exactly those slots. */
    const uint32_t frame[3] = {
        0,          /* EIP */
        0,          /* CS */
        0x3202u,    /* EFLAGS */
    };
    return run_guest(target, frame, 3, x86_initialize_cpu()->_eax);
}

int Game_CallGuestInterrupt(uint8_t vector)
{
    return Game_DosCallGuestIsr(vectors[vector]);
}

int Game_GuestIsrRunning(void)
{
    return isr_running;
}

int Game_DosCallGuestTimer(uint32_t target, uint32_t user)
{
    /* the return address, then the cdecl argument, which the caller pops */
    const uint32_t frame[2] = { 0, user };
    return run_guest(target, frame, 2, user);
}

/* AH=2Ch: the local time, CH hour, CL minute, DH second, DL hundredths.
 * WATCOM's sleep() polls it until the second changes. */
static void dos_get_time(Game_DosRegs *regs)
{
    struct tm *parts;
    unsigned int hundredths;
#if defined(_WIN32)
    struct timeb now;
    ftime(&now);
    parts = localtime(&now.time);
    hundredths = now.millitm / 10;
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    time_t seconds = now.tv_sec;
    parts = localtime(&seconds);
    hundredths = now.tv_usec / 10000;
#endif
    if (parts == NULL) {
        Game_DosSetCX(regs, 0);
        Game_DosSetDX(regs, (uint16_t)hundredths);
        return;
    }
    Game_DosSetCX(regs, (uint16_t)((parts->tm_hour << 8) | parts->tm_min));
    Game_DosSetDX(regs, (uint16_t)((parts->tm_sec << 8) | hundredths));
}

static int dos_call(Game_DosRegs *regs)
{
    /* the file functions: Game_dosfile.c; the keyboard (08h, 0Bh): Game_doskbd.c */
    if (Game_DosFileCall(regs, Game_DosGuestPointer) || Game_DosKbdInt21(regs)) return 1;
    switch (Game_DosAH(regs)) {
    case 0x25: {
        /* Set vector: AL = vector, EDX = handler.  WATCOM's setvect and
         * getvect, under the DOS/4GW 2.x their start-up detected (a flag of 2
         * to 8), use the extended forms instead: AX = 2504h, CL = vector, EDX
         * = handler, and AX = 2502h, CL = vector, the handler back in EBX. */
        const uint16_t function = Game_DosAX(regs);
        if (function == 0x2502u) {
            regs->ebx = vectors[(uint8_t)regs->ecx];
        } else {
            const uint8_t vector = (function == 0x2504u) ? (uint8_t)regs->ecx : Game_DosAL(regs);
            if (trace_int()) fprintf(stderr, "set vector %02x = %08x\n", vector, regs->edx);
            vectors[vector] = regs->edx;
        }
        break;
    }
    case 0x2c:
        dos_get_time(regs);
        break;
    case 0x30:
        /* DOS 7.0 and, in the upper half of EAX, the DOS/4GW signature "DX".
         * BL is the extender's major version in ASCII: WATCOM's start-up
         * stores it minus '0' as the flag that picks its heap code, and 2
         * selects DOS/4GW 2.x. */
        regs->eax = 0x44580700u;
        regs->ebx = (regs->ebx & 0xffffff00u) | '2';
        break;
    case 0x35:
        /* Get vector: AL = vector, the handler in EBX (WATCOM's getvect takes
         * all 32 bits of it). */
        regs->ebx = vectors[Game_DosAL(regs)];
        break;
    case 0x48: {
        /* Allocate memory: BX paragraphs, the segment in AX.  It is also how
         * much is free: callers ask for too much and read BX back. */
        uint16_t segment;
        if (Game_DosMemAlloc(Game_DosBX(regs), &segment) == 0) {
            Game_DosSetBX(regs, (uint16_t)Game_DosMemFreeParagraphs());
            Game_DosError(regs, 8);
            return 1;
        }
        Game_DosSetAX(regs, segment);
        break;
    }
    case 0x49:
        /* Free memory: the pool never takes a block back.  A game leaks a few
         * DOS blocks at most, and a segment the game passes is not trusted as
         * a pointer. */
        break;
    case 0x4c:
        Game_ExitMain_Asm(Game_DosAL(regs));
        break;
    default:
        return 0;
    }
    Game_DosSetCarry(regs, 0);
    return 1;
}

/* INT 31h, DPMI 0.9, as far as a flat game in one address space asks it:
 * descriptors do not exist, the DOS memory is Game_dosmem.c's pool. */
static int dpmi_call(Game_DosRegs *regs)
{
    switch (Game_DosAX(regs)) {
    case 0x0000:
        /* Allocate descriptors: a selector the flat model never loads. */
        Game_DosSetAX(regs, 0x0053);
        break;
    case 0x0001:    /* free a descriptor */
    case 0x0201:    /* set a real-mode interrupt vector */
        Game_DosSetAX(regs, 0);
        break;
    case 0x0002:
        /* Segment to descriptor */
        Game_DosSetAX(regs, 0x0008);
        break;
    case 0x0003:
    case 0x0400:
        regs->ecx = 0;
        regs->edx = 0;
        break;
    case 0x0006:
        /* Get segment base: CX:DX = 0, the flat model's. */
        Game_DosSetCX(regs, 0);
        Game_DosSetDX(regs, 0);
        break;
    case 0x0007:    /* set segment base */
    case 0x0008:    /* set segment limit */
    case 0x0009:    /* set access rights */
    case 0x0101:    /* free a DOS block (see INT 21h AH=49h) */
    case 0x0501:    /* the page services: the game's memory is the host's */
    case 0x0600:
    case 0x0601:
    case 0x0800:
    case 0x0801:
        break;
    case 0x0100: {
        /* Allocate DOS memory: BX paragraphs, AX = the real-mode segment, DX
         * = the selector, the same number here. */
        uint16_t segment;
        if (Game_DosMemAlloc(Game_DosBX(regs), &segment) == 0) {
            Game_DosSetBX(regs, (uint16_t)Game_DosMemFreeParagraphs());
            Game_DosError(regs, 0x0008);
            return 1;
        }
        Game_DosSetAX(regs, segment);
        Game_DosSetDX(regs, segment);
        break;
    }
    case 0x0102:
        /* Resize a DOS block: the pool cannot. */
        Game_DosError(regs, 0x0008);
        return 1;
    case 0x0200:    /* get a real-mode vector */
    case 0x0204:    /* get a protected-mode vector */
        /* BL = vector, CX:EDX = the handler. */
        regs->ecx = 0;
        regs->edx = vectors[(uint8_t)regs->ebx];
        break;
    default:
        return 0;
    }
    Game_DosSetCarry(regs, 0);
    return 1;
}

void Game_DosDispatch(uint8_t number, Game_DosRegs *regs)
{
    /* the window was closed: the next timer check leaves the guest code */
    if (Thread_Exit) {
        Game_DosSetCarry(regs, 1);
        return;
    }
#ifdef GAME_DOS_INTERRUPT
    if (GAME_DOS_INTERRUPT(number, regs)) return;
#endif

    switch (number) {
    case 0x08:
        Game_CallGuestInterrupt(0x08);
        Game_DosSetCarry(regs, 0);
        return;
    case 0x16:
        if (Game_DosKbdInt16(regs)) return;
        break;
    case 0x1a:
        if (Game_DosAH(regs) == 0x00) {
            /* BIOS clock: ticks since midnight in CX:DX, AL = 0 (no midnight
             * passed) */
            const uint32_t ticks = Game_DosPitBiosTicks();
            regs->ecx = ticks >> 16;
            regs->edx = ticks & 0xffffu;
            Game_DosSetAL(regs, 0);
            Game_DosSetCarry(regs, 0);
            return;
        }
        break;
    case 0x21:
        if (dos_call(regs)) return;
        fprintf(stderr, GAME_NAME ": unimplemented DOS function %02x\n", Game_DosAH(regs));
        Game_DosError(regs, 1);
        return;
    case 0x2f:
        /* MSCDEX (AH=15h); every other multiplex service answers AL = 00h,
         * not installed */
        if (Game_DosCdCall(regs, 0)) return;
        regs->eax &= 0xffffff00u;
        Game_DosSetCarry(regs, 0);
        return;
    case 0x31:
        if (dpmi_call(regs)) return;
        fprintf(stderr, GAME_NAME ": unimplemented DPMI function %04x\n", Game_DosAX(regs));
        Game_DosError(regs, 0x8001);    /* unsupported function */
        return;
    default:
        break;
    }
    fprintf(stderr, GAME_NAME ": unimplemented interrupt %02x (AX=%04x)\n", number, Game_DosAX(regs));
    Game_DosSetCarry(regs, 1);
}

/* Every translated `int N` calls this (llasm-support/llasm_int.c). */
extern "C" void CCALL X86_InterruptProcedure(uint8_t number, void *registers)
{
    Game_DosRegs *regs = (Game_DosRegs *)registers;
    if (trace_int()) {
        fprintf(stderr, "int %02x eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n",
                number, regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
    }
    Game_DosDispatch(number, regs);
    if (trace_int()) {
        fprintf(stderr, "    -> eax=%08x ebx=%08x ecx=%08x edx=%08x cf=%u\n",
                regs->eax, regs->ebx, regs->ecx, regs->edx, regs->eflags & GAME_DOS_CF);
    }
}
