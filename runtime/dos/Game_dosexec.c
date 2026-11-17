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

/* INT 21h AH=4Bh: the other programs of a DOS game.  A program the game runs
 * (its intro, say) is recompiled into the same binary as a module of its own
 * (exe_modules= of game.cfg: its global names have the prefix <module>_, the
 * build lists them in exe_modules.h), and runs here as DOS would run it: on
 * the CPU of the game, with a PSP of its own (its command tail, a copy of the
 * environment), a program block of its own above the game's, from its entry
 * point; its INT 21h AH=4Ch comes back here.  Then the game's registers, its
 * block, its PSP and the interrupt vectors are back as they were, and AH=4Dh
 * gives the exit code.
 *
 * The program is found by its name without the extension, also behind
 * COMMAND.COM: "/C NAME args" runs NAME (WATCOM's system()); redirections
 * (>nul:) are dropped.  A program that is not a module is not found (error
 * 2), as on a disk without it. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <ctype.h>

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_dosmem.h"
#include "Game_dosint.h"
#include "Game_dosregs.h"
#include "Game_dosexec.h"
#include "Game_dosail2.h"
#include "Game_hooks.h"
#include "llasm_cpu.h"
/* the CPU is reached by its fields here: Game_DosRegs has the same names */
#undef eax
#undef ebx
#undef ecx
#undef edx
#undef esi
#undef edi
#undef ebp
#undef esp
#undef eflags
#ifdef GAME_HAVE_EXE_MODULES
#include "exe_modules.h"
#endif

#ifndef GAME_DOS_EXEC_MEMORY
#define GAME_DOS_EXEC_MEMORY (16u << 20)
#endif
#ifndef GAME_DOS_EXEC_STACK
#define GAME_DOS_EXEC_STACK (256u << 10)
#endif

extern "C" _cpu *x86_current_cpu(void);

#ifdef GAME_EXE_MODULES
#define EXE_MODULE_EXTERN(NAME, prefix) \
    extern "C" void CCALL c_##prefix##_start_(CPU); \
    extern "C" unsigned char prefix##_stack_start;
GAME_EXE_MODULES(EXE_MODULE_EXTERN)

struct exe_module { const char *name; void (CCALL *entry)(CPU); unsigned char *stack_start; };
#define EXE_MODULE_ENTRY(NAME, prefix) { #NAME, &c_##prefix##_start_, &prefix##_stack_start },
static const struct exe_module modules[] = { GAME_EXE_MODULES(EXE_MODULE_ENTRY) };
#endif

/* the program running, its way back and its exit code */
#if defined(__GNUC__) && defined(_WIN64)
static intptr_t exec_env[5];
#else
static jmp_buf exec_env;
#endif
static int exec_running;
static int exec_status;
static uint8_t exec_code;

static int trace_exec(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_INT")) != NULL;
    return enabled;
}

/* strncasecmp of the C library is not on every host */
static int names_differ(const char *a, const char *b, size_t length)
{
    size_t index;
    for (index = 0; index < length; index++)
        if (toupper((unsigned char)a[index]) != toupper((unsigned char)b[index])) return 1;
    return 0;
}

#ifdef GAME_EXE_MODULES
/* the module of a program's path (C:\DIR\NAME.EXE, name), NULL for none */
static const struct exe_module *module_of(const char *path, size_t length)
{
    const char *name = path;
    size_t index, size;

    for (index = 0; index < length; index++)
        if (path[index] == '\\' || path[index] == '/' || path[index] == ':') name = path + index + 1;
    size = (size_t)(path + length - name);
    for (index = 0; index < size && name[index] != '.'; index++) {}
    size = index;
    for (index = 0; index < sizeof(modules) / sizeof(modules[0]); index++)
        if (strlen(modules[index].name) == size && !names_differ(modules[index].name, name, size)) return &modules[index];
    return NULL;
}

/* The PSP of the program in a DOS block: the command tail (the args, the
 * redirections dropped), the environment of the game after it, then the
 * program's name. */
static uint32_t make_psp(const char *name, const char *args)
{
    const uint8_t *parent = Game_DosMemBase();
    uint16_t segment, env_segment;
    uint32_t address, used = 0, length = 0;
    uint8_t *psp, *env;
    const uint8_t *parent_env;
    const char *arg = args;

    address = Game_DosMemAlloc(0x400u / 16u, &segment);
    if (address == 0) return 0;
    psp = (uint8_t *)GAME_GUEST32_TO_PTR(address);
    memcpy(psp, parent, 0x80);
    while (*arg != 0 && length < 126) {
        while (*arg == ' ') arg++;
        if (*arg == 0) break;
        const char *end = arg;
        while (*end != 0 && *end != ' ') end++;
        if (*arg != '>' && *arg != '<') {
            if (length != 0) psp[0x81 + length++] = ' ';
            while (arg < end && length < 126) psp[0x81 + length++] = (uint8_t)*arg++;
        }
        arg = end;
    }
    psp[0x80] = (uint8_t)length;
    psp[0x81 + length] = 0x0d;

    /* the environment: the game's variables, the count 1, the name */
    env = psp + 0x100;
    env_segment = (uint16_t)(segment + 0x10);
    memcpy(psp + 0x2c, &env_segment, sizeof(env_segment));
    {
        uint16_t parent_segment;
        memcpy(&parent_segment, parent + 0x2c, sizeof(parent_segment));
        parent_env = (const uint8_t *)GAME_GUEST32_TO_PTR((uint32_t)parent_segment << 4);
    }
    while (used < 0x200 && parent_env[used] != 0) {
        while (used < 0x200 && parent_env[used] != 0) used++;
        used++;
    }
    if (used > 0x200) used = 0x200;
    memcpy(env, parent_env, used);
    if (used == 0) env[used++] = 0;
    env[used++] = 0;
    env[used++] = 1;
    env[used++] = 0;
    snprintf((char *)env + used, 0x300 - used, "C:\\%s.EXE", name);
    return address;
}
#endif

int Game_DosExec(Game_DosRegs *regs)
{
#ifdef GAME_EXE_MODULES
    const char *path = (const char *)GAME_GUEST32_TO_PTR(regs->edx);
    const uint8_t *block = (const uint8_t *)GAME_GUEST32_TO_PTR(regs->ebx);
    const struct exe_module *module;
    char tail[128], program[128];
    const char *args;
    uint32_t tail_address, psp, parent_psp, esp_child;
    const uint8_t *tail_data;
    size_t length;
    _cpu *cpu = x86_current_cpu(), saved_cpu;
    uint32_t vectors[256];

    if ((uint8_t)regs->eax != 0 || exec_running) {
        Game_DosError(regs, 1);
        return 1;
    }
    /* the parameter block of a 32-bit extender: the environment (offset,
     * selector), the command tail (offset, selector) */
    memcpy(&tail_address, block + 6, 4);
    tail_data = (const uint8_t *)GAME_GUEST32_TO_PTR(tail_address);
    length = tail_data[0] < sizeof(tail) - 1 ? tail_data[0] : sizeof(tail) - 1;
    memcpy(tail, tail_data + 1, length);
    tail[length] = 0;
    if (trace_exec()) fprintf(stderr, "exec %s [%s]\n", path, tail);

    module = module_of(path, strlen(path));
    args = tail;
    if (module == NULL) {
        /* COMMAND.COM /C NAME args */
        const char *name = path, *end;
        for (const char *p = path; *p != 0; p++) if (*p == '\\' || *p == '/' || *p == ':') name = p + 1;
        if (strlen(name) >= 7 && !names_differ(name, "COMMAND", 7)) {
            const char *p = tail;
            while (*p == ' ') p++;
            if ((p[0] == '/' || p[0] == '-') && toupper((unsigned char)p[1]) == 'C') {
                p += 2;
                while (*p == ' ') p++;
                for (end = p; *end != 0 && *end != ' ' && *end != '>' && *end != '<'; end++) {}
                length = (size_t)(end - p) < sizeof(program) - 1 ? (size_t)(end - p) : sizeof(program) - 1;
                memcpy(program, p, length);
                program[length] = 0;
                module = module_of(program, length);
                args = end;
            }
        }
    }
    if (module == NULL) {
        fprintf(stderr, GAME_NAME ": exec %s [%s]: not a program of the game\n", path, tail);
        Game_DosError(regs, 2);
        return 1;
    }

    psp = make_psp(module->name, args);
    if (psp == 0) {
        Game_DosError(regs, 8);
        return 1;
    }
    memcpy(&saved_cpu, cpu, sizeof(saved_cpu));
    Game_DosVectors(vectors, 0);
    parent_psp = Game_DosPsp();
    Game_DosProgramPush();
    esp_child = Game_DosProgramInit(GAME_DOS_EXEC_MEMORY, GAME_DOS_EXEC_STACK, PTR2REG(module->stack_start));
    if (esp_child == 0) {
        Game_DosProgramPop();
        Game_DosMemFree((uint16_t)(psp >> 4));
        Game_DosError(regs, 8);
        return 1;
    }
    Game_DosSetPsp(psp);
    exec_running = 1;
    exec_status = 0;
#if defined(__GNUC__) && defined(_WIN64)
    if (__builtin_setjmp(exec_env) == 0)
#else
    if (setjmp(exec_env) == 0)
#endif
    {
        cpu->_esp = esp_child;
        cpu->_eflags = 0x202u;
        module->entry(cpu);
    }
    /* what the program left running goes with its memory */
    Game_AIL2_ProgramEnded();
    exec_running = 0;

    memcpy(cpu, &saved_cpu, sizeof(saved_cpu));
    Game_DosProgramPop();
    Game_DosSetPsp(parent_psp);
    Game_DosVectors(vectors, 1);
    Game_DosMemFree((uint16_t)(psp >> 4));
    exec_code = (uint8_t)exec_status;
    if (trace_exec()) fprintf(stderr, "exec %s: exit %u\n", module->name, exec_code);
    regs->eax &= 0xffff0000u;
    Game_DosSetCarry(regs, 0);
    return 1;
#else
    fprintf(stderr, GAME_NAME ": exec %s: the game has no other programs\n", (const char *)GAME_GUEST32_TO_PTR(regs->edx));
    Game_DosError(regs, 2);
    return 1;
#endif
}

void Game_DosExecExit(uint8_t status)
{
    if (!exec_running) return;
    exec_status = status;
#if defined(__GNUC__) && defined(_WIN64)
    __builtin_longjmp(exec_env, 1);
#else
    longjmp(exec_env, 1);
#endif
}

int Game_DosExecRunning(void)
{
    return exec_running;
}

uint8_t Game_DosExecCode(void)
{
    return exec_code;
}
