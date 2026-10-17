/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer (main.c)
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

/* The frame of a DOS game's port: the variables of Game_vars.h, Game_CleanState,
 * the start (display, audio, input, the guest allocator, the DOS runtime) and
 * the end, and on Windows the log file of a program without a console and a
 * report of a crash.  The game's own parts come through Game_hooks.h, all
 * optional:
 *   GAME_ARGS(argc, argv)    takes the port's own options out of argv, returns the new argc
 *   GAME_CONFIG()            reads the port's configuration, after the options
 *   GAME_RUNTIME_INIT()      the game's runtime, after the guest allocator; nonzero: failed
 *   GAME_RUNTIME_DEINIT()    its end
 *   GAME_LOG_FILE            the name of the Windows log file (default "game.log")
 * The window is Game_dosdisplay.c's, unless the game brings its own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_android.h"
#include "Game_defs.h"

#define DEFINE_VARIABLES
    #include "Game_vars.h"
#undef DEFINE_VARIABLES

#include "audio.h"
#include "display.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_dosmain.h"
#include "Game_hooks.h"
#include "Game_cdrom.h"
#include "Game_dosmem.h"
#include "Game_dosrt.h"
#include "Game_music.h"
#include "Game_vfs.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef GAME_LOG_FILE
#define GAME_LOG_FILE "game.log"
#endif

void Game_SetMainArgs(int argc, char *argv[]);

/* Game_CleanState of SR's DOS ports minus display/semaphore pieces the port does not own. */
void Game_CleanState(int imm)
{
    int i;

    (void)imm;

    for (i = 0; i < 256; i++)
    {
        if (Game_AllocatedMemory[i] != NULL)
        {
            x86_free(Game_AllocatedMemory[i]);
            Game_AllocatedMemory[i] = NULL;
        }
    }

    Game_ScreenWindow = Game_FrameBuffer;
    Game_ScreenWindowNum = 0;
    Game_DisplayStart = 0;
    Game_NextMemory = 0;

    Game_MQueueWrite = 0;
    Game_MQueueRead = 0;
    Game_KQueueWrite = 0;
    Game_KQueueRead = 0;
    Game_KBufferWrite = 0;
    Game_KBufferRead = 0;
    Game_LastKeyStroke = 0;

    memset(&Game_InterruptTable, 0, sizeof(Game_InterruptTable));
    memset(&Game_MouseTable, 0, sizeof(Game_MouseTable));
    memset(&Game_Palette_Or, 0, sizeof(Game_Palette_Or));
}

#if defined(_WIN32)
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

static void Log_Open(void)
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
static void Log_Show(void)
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
#endif

#ifndef GAME_RUNTIME_INIT
/* The DOS world of a game that says nothing about its own: the conventional
 * memory at its own addresses (PSP with the command line at 0, the DPMI 0100h
 * blocks above), the VGA window at A0000h, the disc and the music. */
#define DOS_LOW_SIZE 0xA0000u
#define DOS_VGA_BASE 0xA0000u
#define DOS_VGA_SIZE 0x10000u
#define DOS_ENV_OFFSET 0x100u
#define DOS_ENV_SIZE 0x100u

static int Game_DefaultRuntimeInit(int argc, char *argv[])
{
    uint8_t *psp, *env;
    const char *name = GAME_MAIN_FILENAME;
    size_t length, pos = 0;
    int index;

    if (Game_DosRuntimeInit(0, DOS_LOW_SIZE, DOS_ENV_OFFSET + DOS_ENV_SIZE) != 0) return -1;
    psp = Game_DosMemBase();

    /* the VGA window: real memory at A0000h when the guest can have it */
    Game_FrameBuffer = Game_DosMapLow(DOS_VGA_BASE, DOS_VGA_SIZE);
    if (Game_FrameBuffer == NULL) Game_FrameBuffer = (uint8_t *)x86_malloc(DOS_VGA_SIZE);
    if (Game_FrameBuffer == NULL) return -1;
    memset(Game_FrameBuffer, 0, DOS_VGA_SIZE);
    Game_ScreenWindow = Game_FrameBuffer;
    Game_ScreenWindowNum = 0;

    /* PSP: INT 20h, the end of memory, the environment, the command tail */
    psp[0] = 0xcd;
    psp[1] = 0x20;
    psp[2] = (uint8_t)(DOS_LOW_SIZE >> 12);
    psp[3] = (uint8_t)(DOS_LOW_SIZE >> 20);
    psp[0x2c] = (uint8_t)(DOS_ENV_OFFSET >> 4);
    psp[0x2d] = (uint8_t)(DOS_ENV_OFFSET >> 12);
    /* the environment: no variables, then the program's name */
    env = psp + DOS_ENV_OFFSET;
    env[2] = 1;
    length = strlen(name);
    if (length > DOS_ENV_SIZE - 5) length = DOS_ENV_SIZE - 5;
    memcpy(env + 4, name, length);
    for (index = 1; index < argc && pos < 126; index++) {
        const char *arg = argv[index];
        if (pos != 0) psp[0x81 + pos++] = ' ';
        while (*arg != 0 && pos < 126) psp[0x81 + pos++] = (uint8_t)*arg++;
    }
    psp[0x80] = (uint8_t)pos;
    psp[0x81 + pos] = 0x0d;

    /* Game_CdInit says what it did not find; a game that needs no disc goes on */
    Game_CdInit(Game_DataRoot());
    Game_MusicInit();
    return 0;
}

static void Game_DefaultRuntimeDeinit(void)
{
    Game_MusicDeinit();
    Game_CdDeinit();
    Game_DosRuntimeDeinit();
}
#endif

static int Game_Run(int argc, char *argv[])
{
#ifdef GAME_ARGS
    argc = GAME_ARGS(argc, argv);
#endif

    Game_Sound = 1;
    Game_Music = 1;

#ifdef GAME_CONFIG
    GAME_CONFIG();
#endif

    Init_Display();
    Init_Audio();
    Init_Input();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    Init_Display2();
    Init_Audio2();
    Init_Input2();

    if (Game_Display_Create() != 0)
    {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

    if (x86_init_malloc() != 0)
    {
        fprintf(stderr, "Could not initialize the 32-bit guest allocator\n");
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

#ifdef PTROFS_64BIT
    if (initialize_pointer_offset() != 0)
    {
        fprintf(stderr, "Could not initialize the guest pointer offset\n");
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }
    fprintf(stderr, "Guest pointer offset: 0x%llx\n", (unsigned long long)pointer_offset);
#endif

#ifdef GAME_RUNTIME_INIT
    if (GAME_RUNTIME_INIT() != 0)
#else
    if (Game_DefaultRuntimeInit(argc, argv) != 0)
#endif
    {
        fprintf(stderr, "Could not initialize the %s runtime: %s\n", GAME_NAME, SDL_GetError());
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

    Game_SetMainArgs(argc, argv);
    Game_Event_Loop();

#ifdef GAME_RUNTIME_DEINIT
    GAME_RUNTIME_DEINIT();
#else
    Game_DefaultRuntimeDeinit();
#endif
    x86_deinit_malloc();
    Cleanup_Input();
    Cleanup_Audio();
    Cleanup_Display();
    Game_Display_Destroy();
    SDL_Quit();
    return Game_ExitCode;
}

int main(int argc, char *argv[])
{
    int code;

#if defined(_WIN32)
    Log_Open();
    SetUnhandledExceptionFilter(Crash_Filter);
#endif
#if defined(__ANDROID__)
    Game_AndroidSetup();
#endif
    code = Game_Run(argc, argv);
#if defined(_WIN32)
    /* The game stops on an error with exit(-1), 255, or exit(1) after "File
     * Error"; the port returns 1 when it cannot start.  A normal exit returns
     * what the game's last printf returned, 38.  Closing the window stops the
     * game with 1 or 255 too, and says nothing. */
    if (Log_Has_Stdout && !Game_QuitRequested && (code == 1 || code == 255)) Log_Show();
#endif
    return code;
}
