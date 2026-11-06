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
#include "Game_dosrt.h"
#include "Game_dosmouse.h"
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

static uint32_t default_isr;

void Game_DosIntInit(uint32_t null_isr)
{
    default_isr = null_isr;
    for (unsigned int vector = 0; vector < MAX_VECTORS; vector++) vectors[vector] = null_isr;
    isr_running = 0;
    if (code_end == 0) find_code_bounds();
    if (code_end == 0) fprintf(stderr, GAME_NAME ": the translated code was not found; the game's interrupt handlers will not run\n");
}

uint32_t Game_DosGetVector(uint8_t vector)
{
    return vectors[vector];
}

int Game_DosGuestInterruptsEnabled(void)
{
    return X86_InterruptFlag != 0 && !isr_running;
}

/* Runs the guest code at target with `words` dwords pushed (frame[0] ends up
 * at [esp]) and eflags with IF set.  A hardware interrupt leaves no trace in
 * the code it interrupts, so all of the CPU state is restored afterwards. */
static void warn_not_code(uint32_t target)
{
    static uint32_t reported[8];
    static unsigned int count;

    for (unsigned int index = 0; index < count; index++) if (reported[index] == target) return;
    if (count < sizeof(reported) / sizeof(reported[0])) reported[count++] = target;
    fprintf(stderr, GAME_NAME ": the handler at 0x%X is not translated code and will not run; "
            "SR does not find a handler that the program only hands to the system "
            "(INT 21h AH=25h, a timer): list it in fixup_interpret_as_code.sci\n", (unsigned int)target);
}

static int run_guest(uint32_t target, const uint32_t *frame, unsigned int words, uint32_t eax)
{
    if (!code_address_valid(target)) {
        if (target != 0) warn_not_code(target);
        return 0;
    }
    if (!Game_DosGuestInterruptsEnabled()) return 0;

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

int Game_DosTimerTick(void)
{
    /* The BIOS handler of INT 8 ends with INT 1Ch, the user timer tick.
     *
     * A program that has its own INT 8 handler (the DOS/4GW extender has
     * one) chains it to the old one -- the runtime's null ISR, which does
     * not do the INT 1Ch itself.  So the tick goes to the user's INT 1Ch
     * handler here as well.  A program without its own INT 8 handler gets
     * the tick at its INT 1Ch handler alone. */
    int called = 0;
    if (vectors[0x08] != default_isr) called |= Game_CallGuestInterrupt(0x08);
    if (vectors[0x1c] != default_isr) called |= Game_CallGuestInterrupt(0x1c);
    return called;
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

static uint8_t video_mode = 3;

static int dos_call(Game_DosRegs *regs)
{
    /* the file functions: Game_dosfile.c; the keyboard (08h, 0Bh): Game_doskbd.c */
    if (Game_DosFileCall(regs, Game_DosGuestPointer) || Game_DosKbdInt21(regs)) return 1;
    switch (Game_DosAH(regs)) {
    case 0x09: {
        /* Print the string at EDX up to its '$' to the standard output */
        const char *text = (const char *)Game_DosGuestPointer(regs->edx);
        for (; text != NULL && *text != '$'; text++) fputc(*text, stdout);
        fflush(stdout);
        Game_DosSetAL(regs, '$');
        break;
    }
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
    case 0x44:
        /* IOCTL.  The standard handles are the console, everything else a
         * file: AL = 0 gives DX = the device information (bit 7: a device,
         * bits 0 and 1: standard input and output), 6 and 7 the input and
         * output status (ready). */
        switch (Game_DosAL(regs)) {
        case 0x00:
            Game_DosSetDX(regs, Game_DosBX(regs) <= 2 ? 0x80d3u : 0x0002u);
            break;
        case 0x01:
            break;
        case 0x06:
        case 0x07:
            Game_DosSetAL(regs, 0xff);
            break;
        default:
            Game_DosError(regs, 1);
            return 1;
        }
        break;
    case 0x4a:
        /* Resize a block.  The program's own block (see Game_DosProgramInit)
         * ends at its size; the pool's blocks never move, and a block is as
         * large as the game was told. */
        if (Game_DosProgramEnd() != 0 && !Game_DosProgramResize(regs->ebx)) {
            regs->ebx = Game_DosProgramEnd() >> 12;
            Game_DosError(regs, 8);
            return 1;
        }
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
    case 0x0300: {
        /* Simulate a real-mode interrupt: BL = vector, ES:EDI = the register
         * structure of DPMI 0.9 (EDI 00, ESI 04, EBP 08, EBX 10, EDX 14, ECX
         * 18, EAX 1C, flags 20, ES 22).  The call runs on a copy.  MSCDEX
         * (INT 2Fh AH=15h) takes its request header from ES:BX; a vector
         * nobody serves fails, with CF set in the flags.  The game's
         * GAME_DOS_REALMODE_INTERRUPT(vector, regs, es) comes after MSCDEX
         * (es: the structure's ES, the segment of a driver's buffer). */
        uint8_t *structure = (uint8_t *)Game_DosGuestPointer(regs->edi);
        const uint8_t vector = (uint8_t)regs->ebx;
        Game_DosRegs nested;
        uint16_t flags, es;

        if (structure == NULL) {
            Game_DosError(regs, 0x8021);
            return 1;
        }
        memset(&nested, 0, sizeof(nested));
        memcpy(&nested.edi, structure + 0x00, 4);
        memcpy(&nested.esi, structure + 0x04, 4);
        memcpy(&nested.ebp, structure + 0x08, 4);
        memcpy(&nested.ebx, structure + 0x10, 4);
        memcpy(&nested.edx, structure + 0x14, 4);
        memcpy(&nested.ecx, structure + 0x18, 4);
        memcpy(&nested.eax, structure + 0x1c, 4);
        memcpy(&flags, structure + 0x20, 2);
        memcpy(&es, structure + 0x22, 2);
        nested.eflags = flags;
        if (trace_int()) fprintf(stderr, "  real-mode int %02x ax=%04x bx=%04x es=%04x\n", vector, Game_DosAX(&nested), Game_DosBX(&nested), es);
        if (vector == 0x2f && Game_DosCdCall(&nested, es)) {
            /* served */
#ifdef GAME_DOS_REALMODE_INTERRUPT
        } else if (GAME_DOS_REALMODE_INTERRUPT(vector, &nested, es)) {
            /* the game's */
#endif
        } else if (vector == 0x10 || vector == 0x1a || vector == 0x21 || vector == 0x2f) {
            Game_DosDispatch(vector, &nested);
        } else {
            Game_DosSetCarry(&nested, 1);
        }
        flags = (uint16_t)nested.eflags;
        memcpy(structure + 0x00, &nested.edi, 4);
        memcpy(structure + 0x04, &nested.esi, 4);
        memcpy(structure + 0x08, &nested.ebp, 4);
        memcpy(structure + 0x10, &nested.ebx, 4);
        memcpy(structure + 0x14, &nested.edx, 4);
        memcpy(structure + 0x18, &nested.ecx, 4);
        memcpy(structure + 0x1c, &nested.eax, 4);
        memcpy(structure + 0x20, &flags, 2);
        break;
    }
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
    case 0x33:
        if (Game_DosMouseCall(regs)) return;
        break;
    case 0x10:
        /* The video BIOS: the mode is the port's picture, the text functions
         * have nowhere to draw. */
        switch (Game_DosAH(regs)) {
        case 0x00:
            video_mode = Game_DosAL(regs) & 0x7f;
            Game_DosRequestPresent();
            Game_DosSetCarry(regs, 0);
            return;
        case 0x0f:
            /* AL = the mode, AH = columns, BH = the page */
            regs->eax = (regs->eax & 0xffff0000u) | ((video_mode <= 3 ? 80u : 40u) << 8) | video_mode;
            regs->ebx &= 0xffff00ffu;
            Game_DosSetCarry(regs, 0);
            return;
        case 0x01:      /* cursor shape */
        case 0x02:      /* cursor position */
        case 0x05:      /* active page */
        case 0x06:      /* scroll */
        case 0x07:
        case 0x09:      /* characters */
        case 0x0a:
        case 0x0e:
            Game_DosSetCarry(regs, 0);
            return;
        default:
            break;
        }
        break;
    case 0x1a:
        if (Game_DosAH(regs) == 0x02 || Game_DosAH(regs) == 0x04) {
            /* the real-time clock: the time (BCD) or the date */
            const time_t seconds = time(NULL);
            const struct tm *parts = localtime(&seconds);
            if (parts != NULL) {
                const uint32_t year = (uint32_t)parts->tm_year + 1900u;
#define BCD(value) ((((value) / 10) << 4) | ((value) % 10))
                if (Game_DosAH(regs) == 0x02) {
                    Game_DosSetCX(regs, (uint16_t)((BCD(parts->tm_hour) << 8) | BCD(parts->tm_min)));
                    Game_DosSetDX(regs, (uint16_t)(BCD(parts->tm_sec) << 8));
                } else {
                    Game_DosSetCX(regs, (uint16_t)((BCD(year / 100) << 8) | BCD(year % 100)));
                    Game_DosSetDX(regs, (uint16_t)((BCD(parts->tm_mon + 1) << 8) | BCD(parts->tm_mday)));
                }
#undef BCD
                Game_DosSetCarry(regs, 0);
                return;
            }
        }
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
