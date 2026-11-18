/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
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

/* Minimal C<->llasm glue: just enough to call into the recompiled entry point.
   Unlike the Game-asm.c of SR's ports (SR/games/; this is modelled on it), there is no timer-ISR reentry
   point or function-pointer-table call bridge (Game_RunTimer_Asm, Game_MouseMove, etc.):
   nothing calls into the game that way, and each would need its own alias in
   the llasm global_aliases.sci of the port (which has only main_). */

#include <stdio.h>
#include <stdlib.h>
#if !(defined(__GNUC__) && defined(_WIN64))
#include <setjmp.h>
#if defined(_MSC_VER) && defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif
#include "Game_hooks.h"
#include "llasm_cpu.h"

/* the symbol the game starts at: the program's entry point, which SR exports
 * as c_start_ (the whole startup code runs), or a procedure the port's input
 * names, such as the game's C main (the alias main_ of global_aliases.sci,
 * c_main_); GAME_ENTRY in Game_hooks.h */
/* runtime/dos is on the include path of a DOS game only; the win32 ports start at c_main_ */
#if __has_include("Game_dosmem.h")
#include "Game_dosmem.h"
#define GAME_DOS_ENTRY
#endif
#ifndef GAME_ENTRY
#ifdef GAME_DOS_ENTRY
#define GAME_ENTRY c_start_
/* the program starts as the extender starts it: with the stack and the heap
 * in a memory block of its own (Game_DosProgramInit) */
#define GAME_ENTRY_IS_START
#else
#define GAME_ENTRY c_main_
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern _cpu *x86_initialize_cpu(void);
extern _cpu *x86_current_cpu(void);
extern void x86_deinitialize_cpu(void);

extern void CCALL GAME_ENTRY(CPU);
#ifdef GAME_HAVE_DLL_MODULES
/* WinApi-process.c: the DllMain of the DLL modules the program imports */
extern void Winapi_AttachStaticModules(void);
#endif
#ifdef GAME_ENTRY_IS_START
/* the end of the stack object of the program, exported by SR */
extern unsigned char stack_start;
#ifndef GAME_PROGRAM_MEMORY
#define GAME_PROGRAM_MEMORY (64u << 20)
#endif
#ifndef GAME_PROGRAM_STACK
#define GAME_PROGRAM_STACK (1u << 20)
#endif
#endif

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) && defined(_WIN64)
static intptr_t exit_env[5];
#else
static jmp_buf exit_env;
#if defined(_MSC_VER) && defined(_WIN64)
static int (*dyn_intrinsic_setjmp)(jmp_buf, void *);
static void (*dyn_longjmp)(jmp_buf, int);
#endif
#endif

static int main_return_value;


EXTERNC void CCALL Game_ExitMain_Asm(int32_t status)
{
    main_return_value = status;
#if defined(__GNUC__) && defined(_WIN64)
    __builtin_longjmp(exit_env, 1);
#elif defined(_MSC_VER) && defined(_WIN64)
    (dyn_longjmp != NULL) ? dyn_longjmp(exit_env, 1) : longjmp(exit_env, 1);
#else
    longjmp(exit_env, 1);
#endif
}

void CCALL Game_StopMain_Asm(void)
{
    main_return_value = 1;
#if defined(__GNUC__) && defined(_WIN64)
    __builtin_longjmp(exit_env, 1);
#elif defined(_MSC_VER) && defined(_WIN64)
    (dyn_longjmp != NULL) ? dyn_longjmp(exit_env, 1) : longjmp(exit_env, 1);
#else
    longjmp(exit_env, 1);
#endif
}

int CCALL Game_Main_Asm(int argc, char *argv[])
{
#if defined(__GNUC__) && defined(_WIN64)
    if (__builtin_setjmp(exit_env) == 0)
#elif defined(_MSC_VER) && defined(_WIN64)
    HMODULE hLib;

    hLib = GetModuleHandleW(L"ucrtbase.dll");
    if (hLib != NULL)
    {
        dyn_intrinsic_setjmp = (int (*)(jmp_buf, void *))GetProcAddress(hLib, "__intrinsic_setjmp");
        dyn_longjmp = (void (*)(jmp_buf, int))GetProcAddress(hLib, "longjmp");
    }
    if (hLib == NULL || dyn_intrinsic_setjmp == NULL || dyn_longjmp == NULL)
    {
        dyn_intrinsic_setjmp = NULL;
        dyn_longjmp = NULL;
    }

    if (((dyn_intrinsic_setjmp != NULL) ? dyn_intrinsic_setjmp(exit_env, NULL) : setjmp(exit_env)) == 0)
#else
    if (setjmp(exit_env) == 0)
#endif
    {
        _cpu *cpu;
        int retval;

        cpu = x86_initialize_cpu();

#ifdef GAME_HAVE_DLL_MODULES
        Winapi_AttachStaticModules();
#endif

        eax = argc;
        edx = PTR2REG(argv);
#ifdef GAME_ENTRY_IS_START
        esp = Game_DosProgramInit(GAME_PROGRAM_MEMORY, GAME_PROGRAM_STACK, PTR2REG(&stack_start));
        if (esp == 0)
        {
            fprintf(stderr, GAME_NAME ": no room for the program's memory (%u MiB) above its image\n", GAME_PROGRAM_MEMORY >> 20);
            x86_deinitialize_cpu();
            return 1;
        }
#endif

        GAME_ENTRY(cpu);

        retval = eax;

        x86_deinitialize_cpu();

        return retval;
    }
    else
    {
        x86_deinitialize_cpu();

        return main_return_value;
    }
}

/* The guest's registers for a crash report (Game_log.c): eax, ecx, edx, ebx,
 * esp, ebp, esi, edi of the CPU of this thread (of the game on DOS); 0 when
 * there is none. */
EXTERNC int Game_GuestRegisters(uint32_t regs[8])
{
    _cpu *cpu = x86_current_cpu();

    if (cpu == NULL) return 0;
    regs[0] = eax; regs[1] = ecx; regs[2] = edx; regs[3] = ebx;
    regs[4] = esp; regs[5] = ebp; regs[6] = esi; regs[7] = edi;
    return 1;
}
