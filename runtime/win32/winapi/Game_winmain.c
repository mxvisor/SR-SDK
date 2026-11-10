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
 *   GAME_CONFIG_FILENAME     the port's configuration file in the game directory
 *                            (default <program>.cfg; the program's name is also the
 *                            log's, so the port's files never take a name of the game's)
 *   GAME_LOG_FILE            the name of the Windows log file (default <program>.log)
 *   GAME_WIN_START()         optional: after the winapi layer is up, before the game runs
 *                            (a registry the game reads, a line to the log)
 *   GAME_GUEST_PAGE0         defined: the guest's page at address 0 reads as zeros and takes
 *                            writes (Windows 9x maps the DOS memory there)
 * A game with GAME_DIRECT3D gets the renderer in the log. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>

#include "Game_android.h"
#include "Game_defs.h"
#include "Game_vars.h"
#include "audio.h"
#include "display.h"
#include "Game_config.h"
#include "Game_memory.h"
#include "Game_guestdata.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_hooks.h"
#include "Game_log.h"
#include "Game_runtime.h"
#include "WinApi-kernel32.h"
#include "WinApi-d3d.h"

void Game_SetMainArgs(int argc, char *argv[]);

#ifdef __cplusplus
extern "C"
#endif
void Winapi_SetCommandLine(int argc, char *argv[]);
#ifdef GAME_EARLY_GL_WINDOW
#ifdef __cplusplus
extern "C"
#endif
void Winapi_EarlyWindow(uint32_t width, uint32_t height);
#endif

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

#if !defined(__ANDROID__) && defined(SDL_HINT_ENABLE_SCREEN_KEYBOARD)
    /* No screen keyboard of the system: the port never asks for text, yet an sdl2-compat older than
     * 2.32.70 opens one (Steam Deck) for the text input SDL2 starts by itself; the environment wins */
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
#endif

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

    if (initialize_pointer_offset() != 0)
    {
        fprintf(stderr, "Could not initialize the guest pointer offset\n");
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }
#ifdef GAME_GUEST_PAGE0
    if (commit_guest_low(4096) != 0) fprintf(stderr, "Could not map the guest's page 0\n");
#endif
#ifdef GAME_GUEST_DATA_IN_MEMORY
    Game_GuestDataStart();
#endif

    if (Game_RuntimeInit() != 0)
    {
        fprintf(stderr, "Could not initialize the runtime: %s\n", SDL_GetError());
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

#ifdef GAME_EARLY_GL_WINDOW
    /* Game_hooks.h: show the Direct3D window at once (see Winapi_EarlyWindow) */
    Winapi_EarlyWindow(640, 480);
#endif

#if defined(GAME_DIRECT3D) && GAME_DIRECT3D
    if (Winapi_Direct3DEnabled())
        fprintf(stderr, "Renderer: Direct3D (%s)\n", Winapi_Direct3DGL() ? "OpenGL" : "software");
    else
        fprintf(stderr, "Renderer: DirectDraw (software)\n");
#endif

#ifdef GAME_WIN_START
    GAME_WIN_START();
#endif

    Game_SetMainArgs(argc, argv);
    Winapi_SetCommandLine(argc, argv);
    Game_Event_Loop();

    /* The threads of the game still run on its memory: as with ExitProcess on Windows they
     * end with the process, before that memory goes away under them. */
    if (Winapi_ThreadsRunning() != 0)
    {
        fflush(NULL);
        _exit(Game_ExitCode);
    }

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
