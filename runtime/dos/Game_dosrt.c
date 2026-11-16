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
#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "Game_vfs.h"
#include "Game_sound.h"
#include "Game_dosrt.h"
#include "Game_dosint.h"
#include "Game_dosmem.h"
#include "Game_dosfile.h"
#include "Game_doskbd.h"
#include "Game_dosjoy.h"
#include "Game_dospit.h"
#include "Game_dosvga.h"
#include "Game_hooks.h"
#ifdef GAME_DOS_SOUNDBLASTER
#include "Game_dossb.h"
#endif

/* SR turns a segment-override access into a call with the offset alone, the
 * segment dropped.  A DOS4GW game addresses two segments this way, one after
 * the other: before it has a DOS block, WATCOM's start-up reads its PSP (the
 * environment segment at 2Ch, the memory top at 5Ch); afterwards it builds
 * its real-mode requests (MSCDEX) in the block DPMI 0100h gave it last.
 * Offsets in them stay below 100h, and no other access is that low. */
#define PSP_SIZE 0x100u

enum { TRACE_PORTS, TRACE_MEM, TRACE_COUNT };

/* <GAME>_TRACE_PORTS: every port access; <GAME>_TRACE_MEM: every segment-override access */
static int trace_enabled(int which)
{
    static int enabled[TRACE_COUNT] = { -1, -1 };
    static const char *const names[TRACE_COUNT] = { GAME_ENV("TRACE_PORTS"), GAME_ENV("TRACE_MEM") };
    if (enabled[which] < 0) enabled[which] = getenv(names[which]) != NULL;
    return enabled[which];
}

static uint32_t port_in(uint16_t port)
{
    uint32_t value;
#ifdef GAME_DOS_PORT_IN
    if (GAME_DOS_PORT_IN(port, &value)) return value;
#endif
#ifdef GAME_DOS_SOUNDBLASTER
    if (Game_DosSbIn(port, &value)) return value;
#endif
    if (port == 0x40) return Game_DosPitIn(port);
    if (port == 0x60 || port == 0x64) return Game_DosKbdPort(port);
    /* the game port: FFh without a pad (the bus floats high, so the axes never
     * time out and no button is pressed; zero would be a joystick whose four
     * buttons are all held down) */
    if (port == 0x201) return Game_DosJoyIn();
    if (Game_DosVgaIn(port, &value)) return value;
    return 0;
}

/* The FS segment: the translated code reaches it through llasm_fs_mem.c.  A
 * DOS4GW game keeps nothing there, so it reads 0 and writes go nowhere. */
extern "C" uint32_t CCALL X86_ReadFsDword(uint32_t addr)
{
    (void)addr;
    return 0;
}

extern "C" void CCALL X86_WriteFsDword(uint32_t addr, uint32_t value)
{
    (void)addr;
    (void)value;
}

extern "C" void Game_DumpGuestMemory(void)
{
#ifdef GAME_DUMP_GUEST_MEMORY
    GAME_DUMP_GUEST_MEMORY();
#endif
}

extern "C" uint32_t CCALL X86_InPortProcedure(uint16_t port, uint32_t size)
{
    const uint32_t value = port_in(port);
    (void)size;
    if (trace_enabled(TRACE_PORTS)) fprintf(stderr, "in %04x = %02x\n", port, value);
    return value;
}

extern "C" void CCALL X86_OutPortProcedure(uint16_t port, uint32_t size, uint32_t value)
{
    if (trace_enabled(TRACE_PORTS)) fprintf(stderr, "out %04x, %02x\n", port, value);
#ifdef GAME_DOS_PORT_OUT
    if (GAME_DOS_PORT_OUT(port, value)) return;
#endif
#ifdef GAME_DOS_SOUNDBLASTER
    if (Game_DosSbOut(port, value)) return;
#endif
    if (port == 0x201) Game_DosJoyOut();
    else if (port == 0x40 || port == 0x43) Game_DosPitOut(port, (uint8_t)value);
    else {
        /* OUT DX, AX: AL to the port, AH to the next (an index, then its data) */
        Game_DosVgaOut(port, (uint8_t)value);
        if (size == 2) Game_DosVgaOut((uint16_t)(port + 1), (uint8_t)(value >> 8));
    }
}

/* The host pointer of a segment-override access, NULL if there is none. */
static uint8_t *segment_access(uint32_t address, uint32_t size)
{
    if (address < PSP_SIZE) {
        if (Game_DosMemLastBlock != 0 && address != 0x2cu && address != 0x5cu)
            return (uint8_t *)GAME_GUEST32_TO_PTR(Game_DosMemLastBlock + address);
        return Game_DosMemBase() + address;
    }
    /* the rest is a DOS address: the memory and the VGA window at A0000h */
    return Game_DosMemPtr(address, size);
}

extern "C" uint32_t CCALL X86_ReadMemProcedure(uint32_t address, uint32_t size)
{
    const uint8_t *pointer = segment_access(address, size);
    uint32_t value = 0;
    if (pointer != NULL) memcpy(&value, pointer, size);
    if (trace_enabled(TRACE_MEM)) fprintf(stderr, "read  %08x/%u = %08x%s\n", address, size, value, pointer != NULL ? "" : " (nothing there)");
    return value;
}

extern "C" void CCALL X86_WriteMemProcedure(uint32_t address, uint32_t size, uint32_t value)
{
    uint8_t *pointer = segment_access(address, size);
    if (trace_enabled(TRACE_MEM)) fprintf(stderr, "write %08x/%u = %08x%s\n", address, size, value, pointer != NULL ? "" : " (nothing there)");
    if (pointer != NULL) memcpy(pointer, &value, size);
}

/* The time of the last present (SDL_GetTicks), for the timer's (SR_CheckTimer) */
static SDL_atomic_t present_at;

void Game_DosRequestPresent(void)
{
    SDL_AtomicSet(&present_at, (int)SDL_GetTicks());
    Game_RequestFlip();
}

uint32_t Game_DosSincePresent(void)
{
    const int32_t since = (int32_t)(SDL_GetTicks() - (uint32_t)SDL_AtomicGet(&present_at));
    return since > 0 ? (uint32_t)since : 0;
}

/* The shadows of the privileged registers and the descriptor table that the
 * translated `mov cr0`, `lsl`, `smsw` and `sidt` load and store
 * (instruction_replacements.sci, llasm/dos.llinc).  The IDT is a fake one,
 * never dispatched through; DS spans the flat 4 GB minus the top (the limit
 * `lsl` reports). */
extern "C" {
uint32_t Game_DosCr0 = 0x13;
uint32_t Game_DosDsLimit = 0x00ffffffu;
uint32_t Game_DosIdtBase;
/* The handler every vector starts with (llasm/Game-llasm.llasm). */
extern void CCALL c_Game_DosNullIsr(void *cpu);
}
static uint8_t idt[2048];

int Game_DosRuntimeInit(uint32_t first_free)
{
    if (Game_FilesInit() != 0) return -1;
    if (Game_DosMemInit(first_free) != 0) {
        fprintf(stderr, GAME_NAME ": no DOS memory at the guest's addresses 0-%Xh (the guest window has no low memory of its own)\n", DOS_VGA_ADDRESS + DOS_VGA_SIZE);
        return -1;
    }
    Game_FrameBuffer = Game_DosMemPtr(DOS_VGA_ADDRESS, DOS_VGA_SIZE);
    Game_ScreenWindow = Game_FrameBuffer;
    Game_ScreenWindowNum = 0;
    Game_DosIdtBase = GAME_PTR_TO_GUEST32(idt);
    Game_DosIntInit(GAME_PTR_TO_GUEST32(&c_Game_DosNullIsr));
    Game_DosFileInit();
    Game_DosKbdInit();
    Game_DosPitInit();
    /* no audio device only silences the game */
    Game_AudioInit();
    return 0;
}

void Game_DosRuntimeDeinit(void)
{
#ifdef GAME_DOS_SOUNDBLASTER
    Game_DosSbShutdown();
#endif
    Game_DosProgramDeinit();
    Game_AudioDeinit();
    Game_DosFileDeinit();
    Game_FilesDeinit();
    Game_DosMemDeinit();
}
