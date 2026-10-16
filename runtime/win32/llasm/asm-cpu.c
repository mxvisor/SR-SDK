/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

/* As the asm-cpu.c of SR's ports (SR/games/): the CPU init/deinit lifecycle is generic. */
#include "../Game_memory.h"
#include "llasm_cpu.h"
#include <stdlib.h>

static _cpu *global_cpu_ptr = NULL;
static void *global_cpu_block = NULL;
static void *global_stack_block = NULL;

EXTERNCVAR uint32_t X86_InterruptFlag;
EXTERNCVAR uint8_t stack_start;

#ifdef PTROFS_64BIT
extern uint64_t pointer_offset;
#endif

EXTERNC _cpu *x86_initialize_cpu(void)
{
    _cpu *cpu;
    void *cpu_block;
    void *stack_block;

    cpu = global_cpu_ptr;
    if (cpu != NULL) return cpu;

    cpu_block = x86_malloc(4088);
    if (cpu_block == NULL) exit(2);

    cpu = (_cpu *)((2048 + (uintptr_t)cpu_block) & ~(uintptr_t)127);
    global_cpu_block = cpu_block;

    /* The guest stack lives in its own 1 MB guest allocation: pointing
     * stack_top at a small host global (as SR's template did) makes the
     * stack grow down through the host binary's .data/.got. */
    stack_block = x86_malloc(1024 * 1024 - 8);
    if (stack_block == NULL) exit(2);
    global_stack_block = stack_block;

    cpu->stack_bottom = stack_block;
    cpu->stack_top = (void *)(1024 * 1020 + (uintptr_t)stack_block);

    cpu->_st_top = 0;
    cpu->_st_sw_cond = 0;
    cpu->_st_cw = 0x037f;

#ifdef PTROFS_64BIT
    cpu->_pointer_offset = pointer_offset;
#else
    cpu->_reserved1 = 0;
#endif

    esp = PTR2REG(cpu->stack_top);
    eflags = 0x3202;
    X86_InterruptFlag = 1;

    global_cpu_ptr = cpu;
    return cpu;
}

EXTERNC void x86_deinitialize_cpu(void)
{
    _cpu *cpu;
    void *cpu_block;

    cpu = global_cpu_ptr;
    if (cpu == NULL) return;

    if (global_cpu_block != NULL)
    {
        cpu_block = global_cpu_block;
        global_cpu_block = NULL;
        x86_free(cpu_block);
    }

    if (global_stack_block != NULL)
    {
        x86_free(global_stack_block);
        global_stack_block = NULL;
    }

    X86_InterruptFlag = 0;

    global_cpu_ptr = NULL;
}

