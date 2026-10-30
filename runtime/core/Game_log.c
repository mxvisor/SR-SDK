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
 * stopped, and the report of a crash (Game_log.h).  On other systems there is
 * nothing to do. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_log.h"

#if defined(_WIN32)

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

/* An exception nobody handles ends the process without a word.  Write what
 * is known (code, address with its module, registers, the faulting access,
 * the stack) to the log, then let Windows go on. */
static LONG WINAPI Crash_Filter(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    uintptr_t address = (uintptr_t)record->ExceptionAddress;

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
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void Game_CrashHandlerInstall(void)
{
    SetUnhandledExceptionFilter(Crash_Filter);
}

int Game_LogHasStdout(void)
{
    return Log_Has_Stdout;
}
#endif
