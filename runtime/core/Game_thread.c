/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "Game_gyro.h"
#include "Game_hooks.h"
#include "input.h"
#include "main.h"

static int saved_argc;
static char **saved_argv;
static SDL_threadID main_thread_id;

void Game_SetMainArgs(int argc, char *argv[])
{
    saved_argc = argc;
    saved_argv = argv;
}

/* main_ gets argv as DOS/4GW passed it: an array of 32-bit pointers to strings
 * the game can address.  The host's argv is 64-bit and outside guest memory,
 * so it is copied, as SR's DOS ports do.  argv[0] is the program's DOS name,
 * GAME_MAIN_FILENAME of Game_hooks.h. */
int Game_Main(void)
{
    static const char main_filename[] = GAME_MAIN_FILENAME;
    unsigned int size;
    uint8_t *argv_local;
    char *str;
    int argc, index, ret;

    if (Thread_Exit)
    {
        return 1;
    }

    argc = (saved_argc > 1) ? saved_argc : 1;

    size = (argc + 1) * sizeof(uint32_t) + sizeof(main_filename);
    for (index = 1; index < argc; index++)
    {
        size += strlen(saved_argv[index]) + 1;
    }

    argv_local = (uint8_t *)x86_malloc(size);
    if (argv_local == NULL)
    {
        fprintf(stderr, "Error: Not enough memory\n");
        return 1;
    }

    str = (char *)(argv_local + (argc + 1) * sizeof(uint32_t));
    for (index = 0; index < argc; index++)
    {
        const char *arg = (index == 0) ? main_filename : saved_argv[index];

        ((PTR32(char) *)argv_local)[index] = str;
        strcpy(str, arg);
        str += strlen(arg) + 1;
    }
    ((PTR32(char) *)argv_local)[argc] = NULL;

    ret = Game_Main_Asm(argc, (char **)argv_local);

    x86_free(argv_local);

    return ret;
}

void Game_StopMain(void)
{
    Game_TimerTick += 2;
    if (SDL_ThreadID() != main_thread_id)
    {
        /* the longjmp of Game_StopMain_Asm only works on the thread that did the
         * setjmp: a game's own thread waits for it to notice Thread_Exit */
        unsigned int i;

        for (i = 0; i < 300; i++) SDL_Delay(10);
        fflush(NULL);
        _exit(0);
    }
    Game_StopMain_Asm();
}

int Game_MainThread(void *data)
{
    (void)data;

    main_thread_id = SDL_ThreadID();
    Game_CleanState(Thread_Exit);

    {
        int ret;

        ret = Game_Main();
        /* Game_StopMain() ends the game with 1; when the user asked for the exit
         * (window close, SIGINT: Thread_Exit) that is a normal exit. */
        if (Thread_Exit) ret = 0;
        Game_ExitCode = ret;
    }

    Thread_Exited = 1;

    {
        SDL_Event event;

        event.type = SDL_USEREVENT;
        event.user.code = EC_PROGRAM_QUIT;
        event.user.data1 = NULL;
        event.user.data2 = NULL;

        SDL_PushEvent(&event);
    }

    return 0;
}

/* Nothing of the game to time here: channel 0 interrupts are scheduled in
 * the game thread (SR_CheckTimer, in the game's runtime), which the
 * retrace-locked tick needs to sub-millisecond precision.  The thread is kept for main.c's
 * lifecycle and for the input frontend's periodic work (a controller's mouse
 * motion in the other games; input/pc.c has none). */
int Game_TimerThread(void *data)
{
    (void)data;

    while (!(Thread_Exit && Thread_Exited))
    {
        Handle_Timer_Input_Event();
        SDL_Delay(10);
    }

    return 0;
}

/* 1 from a request of a frame until the flip thread has shown it: the requests
 * in between are left out, as a frame the screen would not show anyway */
static SDL_atomic_t flip_active;

void Game_RequestFlip(void)
{
    if ((Game_FlipSem != NULL) && SDL_AtomicCAS(&flip_active, 0, 1)) SDL_SemPost(Game_FlipSem);
}

/* The thread of the frames: the game's frame through the palette into 32-bit
 * pixels (Display_Flip_Procedure), then on the screen (Game_Display_Flip: the
 * OpenGL of display/pc.c, made and used only here).  The host's main thread
 * keeps the window and the input.  Where the thread that draws pumps SDL's
 * events (GAME_GUEST_PUMP, Android) and the game is not a Win32 one (there the
 * game's thread draws and pumps), it is this one, at least every 10 ms. */
#if !defined(GAME_WINAPI) && GAME_GUEST_PUMP
#define FLIP_PUMPS 1
#else
#define FLIP_PUMPS 0
#endif

int Game_FlipThread(void *data)
{
    (void)data;

    for (;;)
    {
#if FLIP_PUMPS
        Game_GyroPump();
        SDL_PumpEvents();
        if (SDL_SemWaitTimeout(Game_FlipSem, 10) != 0)
        {
            if (Thread_Exit) break;
            continue;
        }
#else
        SDL_SemWait(Game_FlipSem);
#endif
        if (Thread_Exit) break;

#ifdef GAME_FLIP_HOOK
        GAME_FLIP_HOOK();
#endif
        Display_Flip_Procedure(Game_FrameBuffer, Game_TextureData);
        Game_Display_Flip();
        SDL_AtomicSet(&flip_active, 0);

        if (Thread_Exit) break;
    }

    Game_Display_FlipEnd();
    return 0;
}
