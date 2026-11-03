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

/* The frame of a DOS game's port: the start (display, audio, input, the guest
 * allocator, the DOS runtime) and the end, and on Windows the log file of a
 * program without a console and a report of a crash (Game_log.c).  The game's own parts come through Game_hooks.h, all
 * optional:
 *   GAME_ARGS(argc, argv)    takes the port's own options out of argv, returns the new argc (default
 *                            Game_ReadArgs: --config-file=)
 *   GAME_CONFIG()            reads the port's configuration, after the options (default
 *                            Game_ReadConfig: <program>.cfg)
 *   GAME_CONFIG_KEY(key, value)  a key of the settings file that is the game's: nonzero if it took it
 *   GAME_RUNTIME_INIT()      the game's own part of the runtime, after the DOS world; nonzero: failed
 *   GAME_RUNTIME_DEINIT()    its end, before the DOS world's
 *   GAME_DOS_ENV             the variables of the environment, "NAME=VALUE" strings separated by commas
 *                            (the BLASTER of GAME_DOS_SOUNDBLASTER comes first)
 *   GAME_LOG_FILE            the name of the Windows log file (default <program>.log)
 * The window is display/pc.c's, unless the game brings its own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_android.h"
#include "Game_defs.h"

#include "Game_vars.h"

#include "audio.h"
#include "display.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_hooks.h"
#include "Game_config.h"
#include "Game_log.h"
#include "Game_cdrom.h"
#include "Game_dosmem.h"
#include "Game_dosrt.h"
#ifdef GAME_DOS_SOUNDBLASTER
#include "Game_dossb.h"
#endif
#include "Game_music.h"
#include "Game_vfs.h"

void Game_SetMainArgs(int argc, char *argv[]);

/* The DOS world of every game: the conventional memory at its own addresses
 * (Game_dosmem.h: the PSP with the command line, its environment behind it,
 * the DPMI 0100h blocks above), the VGA window at A0000h, the disc and the
 * music. */
#define DOS_ENV_OFFSET 0x100u
#define DOS_ENV_SIZE 0x100u

/* The environment as the C library of a DOS4GW program reads it (the guest's
 * getenv of the extender reads a copy the extender's server built, which the
 * port has not got): the array of the addresses of the variables, a zero at
 * its end, and the guest address of the array. */
static uint32_t Game_DosEnvironArray[16];
extern "C" { uint32_t Game_DosEnviron; }

static int Game_DosWorldInit(int argc, char *argv[])
{
    uint8_t *psp, *env;
    uint16_t segment;
    const char *name = GAME_MAIN_FILENAME;
    const char *const variables[] = {
#ifdef GAME_DOS_SOUNDBLASTER
        Game_DosBlaster(),
#endif
#ifdef GAME_DOS_ENV
        GAME_DOS_ENV,
#endif
        NULL
    };
    size_t length, pos = 0, used = 0, count = 0;
    int index;

    if (Game_DosRuntimeInit(DOS_ENV_OFFSET + DOS_ENV_SIZE) != 0) return -1;
    psp = Game_DosMemBase();

    /* PSP: INT 20h, the segment of the end of memory (at 2, and at 5Ch where
     * DOS4GW's start-up looks for it), the environment, the command tail */
    psp[0] = 0xcd;
    psp[1] = 0x20;
    segment = (uint16_t)(DOS_MEMORY_END >> 4);
    memcpy(psp + 2, &segment, sizeof(segment));
    memcpy(psp + 0x5c, &segment, sizeof(segment));
    segment = (uint16_t)((DOS_PSP_ADDRESS + DOS_ENV_OFFSET) >> 4);
    memcpy(psp + 0x2c, &segment, sizeof(segment));
    /* the environment: the variables, an empty string, the count of the
     * strings that follow (1) and the program's name */
    env = psp + DOS_ENV_OFFSET;
    for (index = 0; index < (int)(sizeof(variables) / sizeof(variables[0])) && variables[index] != NULL; index++) {
        length = strlen(variables[index]);
        if (used + length + 1 > DOS_ENV_SIZE - 6 - strlen(name) || count >= sizeof(Game_DosEnvironArray) / sizeof(Game_DosEnvironArray[0]) - 1) break;
        memcpy(env + used, variables[index], length + 1);
        Game_DosEnvironArray[count++] = GAME_PTR_TO_GUEST32(env + used);
        used += length + 1;
    }
    Game_DosEnvironArray[count] = 0;
    Game_DosEnviron = GAME_PTR_TO_GUEST32(Game_DosEnvironArray);
    if (used == 0) used = 1;    /* an empty environment is two zero bytes */
    env[used + 1] = 1;
    length = strlen(name);
    memcpy(env + used + 3, name, length);
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
#ifdef GAME_RUNTIME_INIT
    return GAME_RUNTIME_INIT();
#else
    return 0;
#endif
}

static void Game_DosWorldDeinit(void)
{
#ifdef GAME_RUNTIME_DEINIT
    GAME_RUNTIME_DEINIT();
#endif
    Game_MusicDeinit();
    Game_CdDeinit();
    Game_DosRuntimeDeinit();
}

static int Game_Run(int argc, char *argv[])
{
#ifdef GAME_ARGS
    argc = GAME_ARGS(argc, argv);
#else
    argc = Game_ReadArgs(argc, argv);
#endif

    Game_Sound = 1;
    Game_Music = 1;

#ifdef GAME_CONFIG
    GAME_CONFIG();
#else
    Game_ReadConfig();
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

    if (Game_DosWorldInit(argc, argv) != 0)
    {
        fprintf(stderr, "Could not initialize the %s runtime: %s\n", GAME_NAME, SDL_GetError());
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

    Game_SetMainArgs(argc, argv);
    Game_Event_Loop();

    Game_DosWorldDeinit();
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

    Game_LogOpen();
    Game_CrashHandlerInstall();
#if defined(__ANDROID__)
    Game_AndroidSetup();
#endif
    code = Game_Run(argc, argv);
    /* The game stops on an error with exit(-1), 255, or exit(1) after "File
     * Error"; the port returns 1 when it cannot start.  A normal exit returns
     * what the game's last printf returned, 38.  Closing the window stops the
     * game with 1 or 255 too, and says nothing. */
    if (Game_LogHasStdout() && !Game_QuitRequested && (code == 1 || code == 255)) Game_LogShow();
    return code;
}
