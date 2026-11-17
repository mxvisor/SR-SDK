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
#include "Game_dosvga.h"
#include "Game_dosexec.h"
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
extern "C" void CCALL c_RunProcStack(CPU);
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

/* The extender WATCOM's start-up is told it runs under (INT 21h AH=30h with
 * EBX = "PHAR"): the flat one of Intel's Code Builder, "CB" in the upper half
 * of EAX.  It is the one its start-up addresses everything of linearly, as the
 * translated code does: the PSP at the address of [EDX+10h], the environment
 * at the segment of [PSP+2Ch] shifted, the program's block resized (AH=4Ah) to
 * ESP minus [EDX+1Ch] bytes, its heap grown with EAX=80004800h.  (Under the
 * others, PharLap's "DX" and DOS/4G, it reads both through selectors that the
 * translated code drops: from the addresses 81h and 0.) */
enum { CB_INFO_PSP = 0x10 / 4, CB_INFO_BASE = 0x1c / 4, CB_INFO_SIZE = 0x20 / 4 };
static uint32_t cb_info[CB_INFO_SIZE];
static int code_builder;
/* the PSP of the program running (another one while a program of the game's
 * runs: Game_dosexec.c) */
static uint32_t psp_address = DOS_PSP_ADDRESS;

uint32_t Game_DosPsp(void)
{
    return psp_address;
}

void Game_DosSetPsp(uint32_t address)
{
    psp_address = address;
}

void Game_DosVectors(uint32_t *copy, int restore)
{
    if (restore) memcpy(vectors, copy, sizeof(vectors));
    else memcpy(copy, vectors, sizeof(vectors));
}

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

static void warn_not_code(uint32_t target)
{
    static uint32_t reported[8];
    static unsigned int count;

    for (unsigned int index = 0; index < count; index++) if (reported[index] == target) return;
    if (count < sizeof(reported) / sizeof(reported[0])) reported[count++] = target;
    fprintf(stderr, GAME_NAME ": the handler at 0x%X is not translated code and will not run; "
            "SR does not find a handler that the program only hands to the system "
            "(INT 21h AH=25h, a timer, the mouse's event handler): list it in fixup_interpret_as_code.sci\n", (unsigned int)target);
}

/* Runs the guest code at target with `words` dwords pushed (frame[0] ends up
 * at [esp]), eflags with IF set, and eax, or with regs the registers of regs.
 * A hardware interrupt leaves no trace in the code it interrupts, so all of
 * the CPU state is restored afterwards. */
static int run_guest(uint32_t target, const uint32_t *frame, unsigned int words, uint32_t eax, const Game_DosRegs *regs)
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
    cpu->_esp -= 4u * words;
    memcpy(Game_DosGuestPointer(cpu->_esp), frame, 4u * words);
    isr_running = 1;
    if (regs == NULL) {
        cpu->_eax = eax;
        cpu->_ecx = target;
        c_RunProcECX(cpu);
    } else {
        /* ecx is an argument: the target goes on the stack, for the glue to pop */
        cpu->_eax = regs->eax;
        cpu->_ebx = regs->ebx;
        cpu->_ecx = regs->ecx;
        cpu->_edx = regs->edx;
        cpu->_esi = regs->esi;
        cpu->_edi = regs->edi;
        cpu->_esp -= 4u;
        memcpy(Game_DosGuestPointer(cpu->_esp), &target, 4u);
        c_RunProcStack(cpu);
    }
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
    return run_guest(target, frame, 3, x86_initialize_cpu()->_eax, NULL);
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
    return run_guest(target, frame, 2, user, NULL);
}

int Game_DosCallGuestFar(uint32_t target, const Game_DosRegs *regs)
{
    /* what its retf pops: the return address (0, back to the host) and CS */
    const uint32_t frame[2] = { 0, 0 };
    return run_guest(target, frame, 2, regs->eax, regs);
}

/* AH=2Ch: the local time, CH hour, CL minute, DH second, DL hundredths.
 * DOS counts it in the BIOS's ticks (18.2 a second, those of INT 1Ah), so the
 * hundredths go up by 5 or 6 at a time: a game that waits for a number of
 * them (the frame of an animation) waits whole ticks, as under DOS.  The
 * host's time of day at the first call starts the count.  WATCOM's sleep()
 * polls it until the second changes. */
#define BIOS_TICKS_PER_DAY 0x1800b0u

static void dos_get_time(Game_DosRegs *regs)
{
    static int counting;
    static uint32_t midnight;           /* the BIOS ticks of the last midnight, mod a day */
    uint32_t ticks, hundredths;

    if (!counting) {
        struct tm *parts;
        uint32_t ms;
#if defined(_WIN32)
        struct timeb now;
        ftime(&now);
        parts = localtime(&now.time);
        ms = now.millitm;
#else
        struct timeval now;
        gettimeofday(&now, NULL);
        time_t seconds = now.tv_sec;
        parts = localtime(&seconds);
        ms = (uint32_t)(now.tv_usec / 1000);
#endif
        if (parts != NULL) ms += ((uint32_t)parts->tm_hour * 3600u + (uint32_t)parts->tm_min * 60u + (uint32_t)parts->tm_sec) * 1000u;
        ticks = (uint32_t)(((uint64_t)ms * 1193182u) / (65536u * 1000u)) % BIOS_TICKS_PER_DAY;
        midnight = (BIOS_TICKS_PER_DAY - Game_DosPitBiosTicks() % BIOS_TICKS_PER_DAY + ticks) % BIOS_TICKS_PER_DAY;
        counting = 1;
    }
    ticks = (midnight + Game_DosPitBiosTicks() % BIOS_TICKS_PER_DAY) % BIOS_TICKS_PER_DAY;
    hundredths = (uint32_t)(((uint64_t)ticks * 6553600u) / 1193182u);
    Game_DosSetCX(regs, (uint16_t)(((hundredths / 360000u) << 8) | (hundredths / 6000u % 60u)));
    Game_DosSetDX(regs, (uint16_t)(((hundredths / 100u % 60u) << 8) | (hundredths % 100u)));
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
         * getvect use PharLap's forms when their start-up found PharLap (a
         * game that starts at its main sets that flag itself): AX = 2504h, CL
         * = vector, EDX = handler, and AX = 2502h, CL = vector, the handler
         * back in EBX. */
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
    case 0x2b:
    case 0x2d:
        /* Set date, set time: a game that reprogrammed the timer (the BIOS
         * clock stood or raced meanwhile) puts the RTC's time back into DOS
         * on its way out.  The host keeps its clock: AL = 00h, accepted. */
        Game_DosSetAL(regs, 0);
        break;
    case 0x30:
        /* DOS 7.0; WATCOM's start-up asks for its extender (above) */
        if (regs->ebx == 0x50484152u) {
            cb_info[CB_INFO_PSP] = psp_address;
            cb_info[CB_INFO_BASE] = Game_DosProgramBase();
            regs->edx = GAME_PTR_TO_GUEST32(cb_info);
            regs->eax = 0x42430700u;
            code_builder = 1;
        } else {
            regs->eax = 0x00000007u;
        }
        break;
    case 0x35:
        /* Get vector: AL = vector, the handler in EBX (WATCOM's getvect takes
         * all 32 bits of it). */
        regs->ebx = vectors[Game_DosAL(regs)];
        break;
    case 0x37:
        /* The switch character: AL=0 reads it into DL ('/', AL=0), AL=1 sets
         * it (DOS 5 ignores that). */
        if (Game_DosAL(regs) == 0) regs->edx = (regs->edx & 0xffffff00u) | '/';
        regs->eax &= 0xffffff00u;
        break;
    case 0x48: {
        /* Allocate memory: BX paragraphs, the segment in AX.  It is also how
         * much is free: callers ask for too much and read BX back.  Code
         * Builder's EAX=80004800h takes EBX bytes of the program's block, the
         * address in EAX. */
        uint16_t segment;
        if (code_builder && regs->eax == 0x80004800u) {
            const uint32_t address = Game_DosProgramAlloc(regs->ebx);
            if (address == 0) {
                Game_DosError(regs, 8);
                return 1;
            }
            regs->eax = address;
            break;
        }
        if (Game_DosMemAlloc(Game_DosBX(regs), &segment) == 0) {
            Game_DosSetBX(regs, (uint16_t)Game_DosMemFreeParagraphs());
            Game_DosError(regs, 8);
            return 1;
        }
        Game_DosSetAX(regs, segment);
        break;
    }
    case 0x49:
        /* Free memory: the block is at ES, which the translated code drops;
         * it stays taken (DPMI 0101h, with the segment in DX, frees). */
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
    case 0x4a: {
        /* Resize a block.  The program's own block (see Game_DosProgramInit)
         * ends where the program asks: in 4 KiB pages from address 0, under
         * Code Builder in bytes from its start.  The pool's blocks never move,
         * and a block is as large as the game was told. */
        const uint32_t end = code_builder ? Game_DosProgramBase() + regs->ebx : regs->ebx << 12;
        if (Game_DosProgramEnd() != 0 && ((!code_builder && regs->ebx > 0xfffffu) || !Game_DosProgramResize(end))) {
            regs->ebx = code_builder ? Game_DosProgramEnd() - Game_DosProgramBase() : Game_DosProgramEnd() >> 12;
            Game_DosError(regs, 8);
            return 1;
        }
        break;
    }
    case 0x4b:
        return Game_DosExec(regs);
    case 0x4c:
        /* the end of a program the game ran (Game_dosexec.c), or of the game */
        Game_DosExecExit(Game_DosAL(regs));
        Game_ExitMain_Asm(Game_DosAL(regs));
        break;
    case 0x4d:
        /* the exit code of the program that ran last, AH = 0: it ended normally */
        Game_DosSetAX(regs, Game_DosExecCode());
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
    case 0x0101:
        /* Free a DOS block: DX = its selector, its segment here. */
        if (!Game_DosMemFree(Game_DosDX(regs))) {
            Game_DosError(regs, 0x0009);
            return 1;
        }
        break;
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
        /* Resize a DOS block: DX = its selector, BX paragraphs, where it is. */
        if (!Game_DosMemResize(Game_DosDX(regs), Game_DosBX(regs))) {
            Game_DosSetBX(regs, (uint16_t)Game_DosMemFreeParagraphs());
            Game_DosError(regs, 0x0008);
            return 1;
        }
        break;
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
            Game_DosVgaSetMode(video_mode, (Game_DosAL(regs) & 0x80) == 0);
            Game_DosRequestPresent();
            Game_DosSetCarry(regs, 0);
            return;
        case 0x0f:
            /* AL = the mode, AH = columns, BH = the page */
            regs->eax = (regs->eax & 0xffff0000u) | ((video_mode <= 1 || video_mode == 0x0d || video_mode == 0x13 ? 40u : 80u) << 8) | video_mode;
            regs->ebx &= 0xffff00ffu;
            Game_DosSetCarry(regs, 0);
            return;
        case 0x10:
            /* AL = 00h-03h, 07h, the attribute controller (a palette register,
             * the border, all of them, blinking; reading a register): its 16
             * palette registers map the colours of mode 12h into the DAC
             * (Game_dosvga.c); the table of AL = 02h is at ES, which the
             * translated code drops.  The DAC functions (AL = 10h on) are not
             * here. */
            if (Game_DosAL(regs) == 0x00) Game_DosVgaSetAttribute((uint8_t)regs->ebx, (uint8_t)(regs->ebx >> 8));
            else if (Game_DosAL(regs) == 0x07) regs->ebx = (regs->ebx & 0xffff00ffu) | ((uint32_t)Game_DosVgaAttribute((uint8_t)regs->ebx) << 8);
            else if (Game_DosAL(regs) > 0x03) break;
            Game_DosSetCarry(regs, 0);
            return;
        case 0x12:
            /* alternate select: BL = 36h turns the refresh off (AL = 1) or on
             * (AL = 0), a faster VRAM while a game draws; the picture goes on
             * being presented.  AL = 12h: supported. */
            if ((uint8_t)regs->ebx != 0x36) break;
            Game_DosSetAL(regs, 0x12);
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
