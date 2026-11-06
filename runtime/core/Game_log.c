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

/* The log file of a Windows program without a console, the report of why it
 * stopped, and the report of a crash (Game_log.h).  Elsewhere only the report
 * of a crash. */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_log.h"
#include "Game_memory.h"

/* the registers of the guest code of this thread (Game-asm.c) */
#ifdef __cplusplus
extern "C"
#endif
int Game_GuestRegisters(uint32_t regs[8]);

/* copies guest memory without faulting again: 0 when it is not readable */
static int Crash_Copy(void *dst, uintptr_t src, size_t size);

/* The guest's side of a crash: an address in the guest's window as the guest
 * sees it, the registers of the guest code that ran on the thread and the top
 * of its stack. */
static void Crash_GuestAddress(FILE *f, const char *what, uintptr_t address)
{
    if (address >= (uintptr_t)pointer_offset && address - (uintptr_t)pointer_offset < 0x100000000ull)
    {
        fprintf(f, "  %s: guest address %08" PRIx32 "\n", what, (uint32_t)(address - (uintptr_t)pointer_offset));
    }
}

static void Crash_Guest(FILE *f)
{
    uint32_t regs[8], stack[16];
    unsigned int index;

    if (!Game_GuestRegisters(regs)) return;
    fprintf(f, "  guest: eax=%08" PRIx32 " ecx=%08" PRIx32 " edx=%08" PRIx32 " ebx=%08" PRIx32
               " esp=%08" PRIx32 " ebp=%08" PRIx32 " esi=%08" PRIx32 " edi=%08" PRIx32 "\n",
            regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]);
    if (Crash_Copy(stack, (uintptr_t)pointer_offset + regs[4], sizeof(stack)))
    {
        fprintf(f, "  guest stack:");
        for (index = 0; index < 16; index++) fprintf(f, "%s %08" PRIx32, (index == 8) ? "\n              " : "", stack[index]);
        fprintf(f, "\n");
    }
}

#if !defined(_WIN32)

#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>
#if defined(__ANDROID__)
#include <SDL.h>
#endif

#include "Game_defs.h"

/* guest memory is read through a pipe: write() fails with EFAULT on an
 * address that is not readable, where a read would fault again */
static int Crash_Pipe[2] = { -1, -1 };

static int Crash_Copy(void *dst, uintptr_t src, size_t size)
{
    if (Crash_Pipe[1] < 0 || write(Crash_Pipe[1], (const void *)src, size) != (ssize_t)size) return 0;
    return read(Crash_Pipe[0], dst, size) == (ssize_t)size;
}

/* A signal nobody handles ends the process without a word.  Write what is
 * known (the signal, the address, the registers, the module and offset of the
 * code that ran) to stderr, and on Android also to crash-native.txt in
 * Android/data/<package>/files/ (the log pump may not get to the end of
 * stderr before the process is gone), then let the signal take its course. */
#if defined(__ANDROID__)
static char Crash_Path[1024];
#endif

static void Crash_Symbol(FILE *f, uintptr_t value)
{
    Dl_info info;

    if (value != 0 && dladdr((void *)value, &info) != 0 && info.dli_fbase != NULL)
    {
        fprintf(f, "  %p %s+0x%" PRIxPTR "\n", (void *)value, info.dli_fname, value - (uintptr_t)info.dli_fbase);
    }
    else
    {
        fprintf(f, "  %p ?\n", (void *)value);
    }
}

static void Crash_Report(FILE *f, int sig, uintptr_t addr, uintptr_t pc, const ucontext_t *context)
{
    fprintf(f, "\nCRASH: signal %d (%s) at %p, pc %p", sig, strsignal(sig), (void *)addr, (void *)pc);
    fprintf(f, ", pointer_offset %p", (void *)(uintptr_t)pointer_offset);
    fprintf(f, "\n");
    /* a jump to an address outside of the code is not a read of zeros */
    Crash_GuestAddress(f, (pc == addr) ? "a jump to the address" : "the address", addr);
    Crash_Symbol(f, pc);
#if defined(__x86_64__) && defined(REG_RIP)
    {
        const greg_t *g = context->uc_mcontext.gregs;

        fprintf(f, "  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n"
                   "  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
                (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RBX], (unsigned long long)g[REG_RCX], (unsigned long long)g[REG_RDX],
                (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI], (unsigned long long)g[REG_RBP], (unsigned long long)g[REG_RSP],
                (unsigned long long)g[REG_R8], (unsigned long long)g[REG_R9], (unsigned long long)g[REG_R10], (unsigned long long)g[REG_R11],
                (unsigned long long)g[REG_R12], (unsigned long long)g[REG_R13], (unsigned long long)g[REG_R14], (unsigned long long)g[REG_R15]);
    }
#elif defined(__aarch64__)
    {
        int index;

        for (index = 0; index < 31; index++) fprintf(f, "  x%02d=%016llx%s", index, (unsigned long long)context->uc_mcontext.regs[index], (index % 4 == 3) ? "\n" : "");
        fprintf(f, "  sp =%016llx\n  caller:\n", (unsigned long long)context->uc_mcontext.sp);
        Crash_Symbol(f, (uintptr_t)context->uc_mcontext.regs[30]);
    }
#else
    (void)context;
#endif
    Crash_Guest(f);
    fflush(f);
}

static void Crash_Handler(int sig, siginfo_t *info, void *uc)
{
    const ucontext_t *context = (const ucontext_t *)uc;
    uintptr_t addr = (uintptr_t)info->si_addr;
#if defined(__x86_64__) && defined(REG_RIP)
    uintptr_t pc = (uintptr_t)context->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    uintptr_t pc = (uintptr_t)context->uc_mcontext.pc;
#else
    uintptr_t pc = 0;
#endif

    Crash_Report(stderr, sig, addr, pc, context);
#if defined(__ANDROID__)
    if (Crash_Path[0] != 0)
    {
        FILE *f = fopen(Crash_Path, "a");

        if (f != NULL)
        {
            Crash_Report(f, sig, addr, pc, context);
            fclose(f);
        }
    }
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}

void Game_CrashHandlerInstall(void)
{
    static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    struct sigaction action;
    unsigned int index;

#if defined(__ANDROID__)
    const char *external = SDL_AndroidGetExternalStoragePath();

    if (external != NULL) snprintf(Crash_Path, sizeof(Crash_Path), "%s/crash-native.txt", external);
#endif
    if (pipe(Crash_Pipe) != 0) Crash_Pipe[0] = Crash_Pipe[1] = -1;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = Crash_Handler;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for (index = 0; index < sizeof(signals) / sizeof(signals[0]); index++) sigaction(signals[index], &action, NULL);
}

#else

#include <windows.h>

#include "Game_defs.h"
#include "Game_vfs.h"
#include "Game_hooks.h"

/* <program>.log: the port's files in the game directory are <program>, its
 * settings and its log, so they never take a name of the game's (one file on
 * Windows); a game names another file with GAME_LOG_FILE (Game_hooks.h) */
#ifndef GAME_LOG_FILE
#define GAME_LOG_FILE GAME_PROGRAM_NAME ".log"
#endif

/* A GUI program (-mwindows) starts without a console.  Unless the caller
 * gave it somewhere to write, the game's messages (stdout) and the port's
 * (stderr) go to GAME_LOG_FILE in the write root; the game's own
 * listing of C:\ skips the name, which is not 8.3. */
static char Log_Path[MAX_PATH];
static int Log_Has_Stdout;

static int Has_Std_Handle(DWORD which)
{
    HANDLE handle = GetStdHandle(which);
    return handle != NULL && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

void Game_LogOpen(void)
{
    int has_stdout = Has_Std_Handle(STD_OUTPUT_HANDLE);
    int has_stderr = Has_Std_Handle(STD_ERROR_HANDLE);
    FILE *log;
    int length;

    if ((has_stdout && has_stderr) || Game_SelectWriteRoot() != 0) return;
    CreateDirectoryA(Game_WriteRoot(), NULL);
    length = snprintf(Log_Path, sizeof(Log_Path), "%s\\" GAME_LOG_FILE, Game_WriteRoot());
    if (length < 0 || length >= (int)sizeof(Log_Path) || (log = fopen(Log_Path, "w")) == NULL)
    {
        Log_Path[0] = 0;
        return;
    }
    fclose(log);
    for (char *separator = Log_Path; *separator != 0; separator++)
    {
        if (*separator == '/') *separator = '\\';
    }

    /* both in append mode and unbuffered, so their lines keep their order */
    if (!has_stdout && freopen(Log_Path, "a", stdout) != NULL)
    {
        setvbuf(stdout, NULL, _IONBF, 0);
        Log_Has_Stdout = 1;
    }
    if (!has_stderr && freopen(Log_Path, "a", stderr) != NULL) setvbuf(stderr, NULL, _IONBF, 0);
}

/* The last lines of the log, which say why the game stopped: its message
 * ("Insert the CD-ROM.") and the port's. */
void Game_LogShow(void)
{
    char buffer[4096];
    char text[sizeof(buffer) + MAX_PATH + 64];
    char *lines[256];
    int count = 0, first;
    size_t length;
    long size;
    FILE *log = fopen(Log_Path, "rb");

    if (log == NULL) return;
    fseek(log, 0, SEEK_END);
    size = ftell(log);
    fseek(log, (size >= (long)sizeof(buffer)) ? size - (long)(sizeof(buffer) - 1) : 0, SEEK_SET);
    length = fread(buffer, 1, sizeof(buffer) - 1, log);
    fclose(log);
    buffer[length] = 0;

    for (char *line = strtok(buffer, "\r\n"); line != NULL && count < 256; line = strtok(NULL, "\r\n"))
    {
        lines[count++] = line;
    }
    first = (size >= (long)sizeof(buffer)) ? 1 : 0;   /* a cut line */
    if (count - first > 12) first = count - 12;

    text[0] = 0;
    for (int index = first; index < count; index++)
    {
        strncat(text, lines[index], sizeof(text) - strlen(text) - 3);
        strcat(text, "\r\n");
    }
    snprintf(text + strlen(text), sizeof(text) - strlen(text), "\r\nThe whole output is in %s", Log_Path);
    MessageBoxA(NULL, text, GAME_NAME, MB_OK | MB_ICONERROR);
}

static int Crash_Copy(void *dst, uintptr_t src, size_t size)
{
    SIZE_T done = 0;

    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)src, dst, size, &done) && done == size;
}

/* An exception nobody handles ends the process without a word, or leaves a
 * frozen window behind Windows' dialog.  Write what is known (code, address
 * with its module, registers, the faulting access, the stack, the guest's
 * registers) to the log and end the process.  What is not an error (a debug
 * message, a thread's name, a breakpoint) goes on to the next handler. */
static LONG WINAPI Crash_Filter(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    uintptr_t address = (uintptr_t)record->ExceptionAddress;

    if (record->ExceptionCode < 0x80000000u || record->ExceptionCode == EXCEPTION_BREAKPOINT ||
        record->ExceptionCode == EXCEPTION_SINGLE_STEP)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module))
    {
        GetModuleFileNameA(module, name, sizeof(name));
    }
    fprintf(stderr, "\nCRASH: exception %08lx at %p (%s+%#llx)\n", (unsigned long)record->ExceptionCode,
            record->ExceptionAddress, name, (unsigned long long)(address - (uintptr_t)module));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
    {
        fprintf(stderr, "  %s address %p\n", record->ExceptionInformation[0] == 0 ? "read of" : (record->ExceptionInformation[0] == 1 ? "write to" : "execute of"),
                (void *)record->ExceptionInformation[1]);
        Crash_GuestAddress(stderr, "the address", (uintptr_t)record->ExceptionInformation[1]);
    }
#if defined(_M_X64) || defined(__x86_64__)
    fprintf(stderr, "  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
            (unsigned long long)context->Rax, (unsigned long long)context->Rbx, (unsigned long long)context->Rcx, (unsigned long long)context->Rdx,
            (unsigned long long)context->Rsi, (unsigned long long)context->Rdi, (unsigned long long)context->Rbp, (unsigned long long)context->Rsp,
            (unsigned long long)context->R8, (unsigned long long)context->R9, (unsigned long long)context->R10, (unsigned long long)context->R11,
            (unsigned long long)context->R12, (unsigned long long)context->R13, (unsigned long long)context->R14, (unsigned long long)context->R15);
    {
        const uintptr_t *stack = (const uintptr_t *)context->Rsp;
        fprintf(stderr, "  stack (module+offset where the value points into a module):\n");
        for (int index = 0; index < 64; index++)
        {
            HMODULE owner = NULL;
            uintptr_t value;
            if (IsBadReadPtr(&stack[index], sizeof(uintptr_t))) break;
            value = stack[index];
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)value, &owner))
            {
                char owner_name[MAX_PATH] = "?";
                GetModuleFileNameA(owner, owner_name, sizeof(owner_name));
                fprintf(stderr, "    [%2d] %s+%#llx\n", index, owner_name, (unsigned long long)(value - (uintptr_t)owner));
            }
        }
    }
#endif
    Crash_Guest(stderr);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

void Game_CrashHandlerInstall(void)
{
    SetUnhandledExceptionFilter(Crash_Filter);
}

int Game_LogHasStdout(void)
{
    return Log_Has_Stdout;
}
#endif /* _WIN32 */
