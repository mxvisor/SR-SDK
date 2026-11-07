/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer (Game-asm.c, asm-llasm.c)
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

/*
 * Game runtime glue (Win32 port scaffolding)
 */

#include "llasm_cpu.h"
#include "WinApi-guest.h"

#ifdef __cplusplus
extern "C" {
#endif

extern _cpu *x86_initialize_cpu(void);
extern void x86_deinitialize_cpu(void);

/* Generic llasm bridges, defined in the hand-written .llasm files (llasm is a
   separate compilation unit, hence external). c_RunWndProc_c2asm just tcall's
   the WndProc pointer passed as the last argument; c_run_thread_c2asm likewise
   tcall's the thread start routine. Both are game-agnostic. */
extern void CCALL c_RunWndProc_c2asm(CPU);
extern void CCALL c_run_thread_c2asm(CPU);
extern void CCALL c_RunProcECX(CPU);

/* stack_start - used by asm-cpu.c to init cpu->stack_top */
uint32_t stack_start = 0xF0000000u;

/* Referenced by CLIB-asm.llasm's _check_security_cookie_asm2c. The original
   game's /GS cookie lives in its .data; the port keeps its own. */
uint32_t security_cookie_ = 0;

/* Referenced by WinApi-user32.c for controller input debouncing. */
uint32_t SoundEngine_Counter = 0;

EXTERNC uint32_t CCALL Winapi_CallGuest(uint32_t func, unsigned int nargs, const uint32_t *args);


/* The window procedure is a guest function: enter it through the guest
   procedure table (Winapi_CallGuest), a plain tcall to its address would jump
   into the data mapping. */
EXTERNC uint32_t CCALL RunWndProc_asm(void *hwnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam, uint32_t (*WndProc)(void *, uint32_t, uint32_t, uint32_t))
{
    uint32_t args[4];
    _cpu *cpu;

    cpu = x86_initialize_cpu();

    args[0] = PTR2REG(hwnd);
    args[1] = uMsg;
    args[2] = wParam;
    args[3] = lParam;

    return Winapi_CallGuest(PTR2REG(WndProc), 4, args);
}

/* Calls a guest (stdcall) procedure from C: pushes the arguments right to left
   and enters it through c_RunProcECX (tcall ecx), which -- like the other
   c_*_c2asm bridges -- supplies the return address slot itself, so the callee
   finds its arguments at [esp+4]. `func` and the arguments are guest values,
   i.e. pointers already converted with PTR2REG. Returns eax. */
EXTERNC uint32_t CCALL Winapi_CallGuest(uint32_t func, unsigned int nargs, const uint32_t *args)
{
    _cpu *cpu;
    uint32_t old_esp, old_ebx, old_ecx, old_edx, old_ebp, old_esi, old_edi, old_eflags, result;

    cpu = x86_initialize_cpu();

    old_esp = esp;
    old_ebx = ebx;
    old_ecx = ecx;
    old_edx = edx;
    old_ebp = ebp;
    old_esi = esi;
    old_edi = edi;
    old_eflags = cpu->_eflags;

    while (nargs != 0)
    {
        nargs--;
        esp -= 4;
        *((uint32_t *)REG2PTR(esp)) = args[nargs];
    }

    cpu->_eflags = 0x3202;
    ecx = func;
    c_RunProcECX(cpu);
    result = eax;

    esp = old_esp;
    ebx = old_ebx;
    ecx = old_ecx;
    edx = old_edx;
    ebp = old_ebp;
    esi = old_esi;
    edi = old_edi;
    cpu->_eflags = old_eflags;

    return result;
}

/* A host pointer into the guest's memory mapping and the guest value of the
 * same byte: a buffer of x86_malloc lies in the mapping, and a guest callback
 * gets its guest value and writes through it. */
EXTERNC uint32_t Winapi_HostToGuestPtr(void *p)
{
    _cpu *cpu = x86_initialize_cpu();

    if (p == 0) return 0;
    return PTR2REG(p);
}

EXTERNC void CCALL run_thread_asm(void *arglist, void (*start_address)(void *))
{
    _cpu *cpu;

    cpu = x86_initialize_cpu();

    esp -= 4;
    *((uint32_t *)REG2PTR(esp)) = PTR2REG(start_address);
    esp -= 4;
    *((uint32_t *)REG2PTR(esp)) = PTR2REG(arglist);

    // ccall
    c_run_thread_c2asm(cpu);

    // no need to restore esp, because cpu will be deleted
    //esp += 2*4;

    x86_deinitialize_cpu();
}

#ifdef __cplusplus
}
#endif
