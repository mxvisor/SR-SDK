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

/* The frame of a Win32 game's port: the start (display, audio, input, the guest
 * allocator, the winapi layer) and the end, and on Windows the log file of a
 * program without a console and a report of a crash (Game_log.c).  The window
 * is display/pc.c's.  The game's own parts come through Game_hooks.h:
 *   GAME_CONFIG_FILENAME     the port's configuration file in the game directory (required)
 *   GAME_LOG_FILE            the name of the Windows log file (default "game.log")
 *   GAME_WIN_START()         optional: after the winapi layer is up, before the game runs
 *                            (a registry the game reads, a line to the log) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "Game_android.h"
#include "Game_defs.h"
#include "Game_vars.h"
#include "audio.h"
#include "display.h"
#include "Game_config.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_hooks.h"
#include "Game_log.h"
#include "Game_runtime.h"
#include "WinApi-kernel32.h"

void Game_SetMainArgs(int argc, char *argv[]);

#ifdef __cplusplus
extern "C"
#endif
void Winapi_SetCommandLine(int argc, char *argv[]);

/* The port's own options, as in SR's ports: --config-file=FILE.  They are taken out
 * of argv, which goes on to the game's switch parser (/D, /T, ...). */
static int Game_ReadArgs(int argc, char *argv[])
{
    int count = 1;

    Game_ConfigFilename[0] = 0;
    for (int index = 1; index < argc; index++)
    {
        if (strncmp(argv[index], "--config-file=", 14) == 0)
        {
            if (strlen(argv[index] + 14) < sizeof(Game_ConfigFilename)) strcpy(Game_ConfigFilename, argv[index] + 14);
            continue;
        }
        argv[count++] = argv[index];
    }
    argv[count] = NULL;
    return count;
}

/* Under Wine SDL's default renderer is Direct3D 11 (then 9), which a prefix with DXVK
 * runs through Vulkan: on some drivers the device creation hangs in the kernel.  The
 * port draws with OpenGL or with SDL's software path, so the renderer is OpenGL
 * (software when there is none) unless SDL_RENDER_DRIVER says otherwise. */
static void Game_WineRenderer(void)
{
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if ((ntdll != NULL) && (GetProcAddress(ntdll, "wine_get_version") != NULL) && (getenv("SDL_RENDER_DRIVER") == NULL))
    {
        /* the environment, not the hint: SDL reads this one before anything of its own */
        SetEnvironmentVariableA("SDL_RENDER_DRIVER", "opengl,software");
        _putenv("SDL_RENDER_DRIVER=opengl,software");
        fprintf(stderr, "Wine: SDL renderer opengl\n");
    }
#endif
}

static int Game_Run(int argc, char *argv[])
{
    argc = Game_ReadArgs(argc, argv);

    Game_Sound = 1;
    Game_Music = 1;

    Init_Display();
    Init_Audio();
    Init_Input();

    Game_ReadConfig();

    Winapi_InitTicks();

    Game_WineRenderer();

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
#endif

    if (Game_RuntimeInit() != 0)
    {
        fprintf(stderr, "Could not initialize the runtime: %s\n", SDL_GetError());
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

#ifdef GAME_WIN_START
    GAME_WIN_START();
#endif

    Game_SetMainArgs(argc, argv);
    Winapi_SetCommandLine(argc, argv);
    Game_Event_Loop();

    Game_RuntimeDeinit();

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
    /* Quit ends the game with ExitProcess(0); the port returns 1 when it
     * cannot start, and anything else is the game stopping on an error.
     * Closing the window stops the game too, and says nothing. */
    if (Game_LogHasStdout() && !Game_QuitRequested && code != 0) Game_LogShow();
    return code;
}
