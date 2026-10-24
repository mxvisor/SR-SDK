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

/* The main thread's SDL event loop, the same in every game: it starts the
 * timer, flip and main threads, then handles the window's events, passes
 * input on (input.h), queues keyboard and mouse events for the guest, and runs
 * the flip on the guest's request.  It returns when the game's main thread has
 * finished and a stop was requested (the window closed, SDL_QUIT,
 * EC_PROGRAM_QUIT).
 *
 * What a game may add in its Game_hooks.h, all optional:
 *   GAME_KEY_FILTER(key)     non-zero for a key event (const SDL_KeyboardEvent *)
 *                            that the port takes for itself, e.g. a toggle key
 *   GAME_KEYBOARD_BLOCKED()  non-zero while the game reads keys by itself
 *   GAME_MOUSE_BLOCKED()     the same for the mouse
 * and Game_Display_Flip() in the game's main.c draws one frame. */

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_hooks.h"

#if !defined(GAME_KEY_FILTER)
#define GAME_KEY_FILTER(key) 0
#endif
#if !defined(GAME_KEYBOARD_BLOCKED)
#define GAME_KEYBOARD_BLOCKED() 0
#endif
#if !defined(GAME_MOUSE_BLOCKED)
#define GAME_MOUSE_BLOCKED() 0
#endif

/* Who takes keyboard and mouse events out of SDL's queue: on win32 with the
 * winapi layer its message pump (find_event in WinApi-user32.c), which runs
 * on the guest's thread; two readers would split the events between them.
 * The loop below then leaves everything from SDL_KEYDOWN up to the user events
 * (keys, mouse, joystick, controller, finger, text, drops) alone. */
#ifndef GAME_CORE_CONSUMES_INPUT
#ifdef GAME_WINAPI
#define GAME_CORE_CONSUMES_INPUT 0
#else
#define GAME_CORE_CONSUMES_INPUT 1
#endif
#endif

int Game_QueueKeyboardEvent(const SDL_Event *event)
{
    if (GAME_KEY_FILTER(&event->key) || event->key.repeat) return 0;
    if (((Game_KQueueWrite + 1) & (GAME_KQUEUE_LENGTH - 1)) == Game_KQueueRead)
    {
        /* keyboard event queue overflow */
        return 0;
    }
    Game_EventKQueue[Game_KQueueWrite] = *event;
    Game_KQueueWrite = (Game_KQueueWrite + 1) & (GAME_KQUEUE_LENGTH - 1);
    return 1;
}

/* the player closed the window: the game's stop is not an error to report */
int Game_QuitRequested;

void Game_Event_Loop(void)
{
    SDL_Thread *MainThread;
    SDL_Thread *FlipThread;
    SDL_Thread *TimerThread;
    SDL_Event event;
    uint32_t AppMouseFocus;
    uint32_t AppInputFocus;
    uint32_t AppActive;
    int FlipActive, NumEvents, PumpEvents;

    /* The close of the window arrives twice (SDL_WINDOWEVENT_CLOSE, then SDL_QUIT);
     * only a repeat after a second means that the game does not stop: leave at once. */
    uint32_t QuitTick = 0;
#define REQUEST_PROGRAM_EXIT() \
    do { \
        if (Thread_Exit && SDL_GetTicks() - QuitTick > 1000) exit(1); \
        if (!Thread_Exit) QuitTick = SDL_GetTicks(); \
        Thread_Exit = 1; \
        if (Game_FlipSem != NULL) SDL_SemPost(Game_FlipSem); \
    } while (0)

    Thread_Exited = 0;
    Thread_Exit = 0;
    Game_ExitCode = 0;
    Game_TimerRunning = 0;
    Game_TimerTick = 0;
    Game_TimerRun = 0;
    Game_VSyncTick = 0;

    Game_FlipSem = SDL_CreateSemaphore(0);
    if (Game_FlipSem == NULL)
    {
        fprintf(stderr, "Error: Unable to create semaphore\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        return;
    }

    TimerThread = SDL_CreateThread(Game_TimerThread, "timer", NULL);
    if (TimerThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start timer thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    FlipThread = SDL_CreateThread(Game_FlipThread, "flip", NULL);
    if (FlipThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start flip thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_WaitThread(TimerThread, NULL);
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    MainThread = SDL_CreateThread(Game_MainThread, "main", NULL);
    if (MainThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start main thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_SemPost(Game_FlipSem);
        SDL_WaitThread(FlipThread, NULL);
        SDL_WaitThread(TimerThread, NULL);
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    AppMouseFocus = 1;
    AppInputFocus = 1;
    AppActive = 1;
    /* read only where the core consumes input (GAME_CORE_CONSUMES_INPUT) */
    (void)AppMouseFocus; (void)AppInputFocus; (void)AppActive;
    FlipActive = 0;
    PumpEvents = 1;

    while (!Thread_Exited || !Thread_Exit)
    {
#if GAME_CORE_CONSUMES_INPUT
        NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
#else
        /* SDL_FIRSTEVENT..SDL_WINDOWEVENT-1: quit, app.  The window events go to the
         * winapi message pump (WM_ACTIVATEAPP, SC_MINIMIZE, close). */
        NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_WINDOWEVENT - 1);
        if (NumEvents <= 0)
            NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_LASTEVENT);
#endif
        if (NumEvents <= 0)
        {
            if (PumpEvents)
            {
                PumpEvents = 0;
                SDL_PumpEvents();
            }
            else
            {
                SDL_Delay(1);
                PumpEvents = 1;
            }
            continue;
        }

        if (Handle_Input_Event(&event)) continue;

        switch (event.type)
        {
            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                        /* Without a WM, xdotool windowclose / XDestroyWindow never
                         * becomes SDL_QUIT — only this event. Same shutdown path. */
                        Game_QuitRequested = 1;
                        REQUEST_PROGRAM_EXIT();
                        break;
                    case SDL_WINDOWEVENT_ENTER:
                        AppMouseFocus = 1;
                        break;
                    case SDL_WINDOWEVENT_LEAVE:
                        AppMouseFocus = 0;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        AppInputFocus = 1;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        AppInputFocus = 0;
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                        AppActive = 0;
                        break;
                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        AppActive = 1;
                        break;
                }
                break;

#if GAME_CORE_CONSUMES_INPUT
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                /* windowID 0: a key of <GAME>_AUTO_KEYS (Game_keys.c), meant
                 * for a window without the focus (Xvfb) */
                if (((AppActive && AppInputFocus) || event.key.windowID == 0) && !GAME_KEYBOARD_BLOCKED())
                {
                    Game_QueueKeyboardEvent(&event);
                }
                break;

            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEWHEEL:
                if (AppActive && AppInputFocus && AppMouseFocus && !GAME_MOUSE_BLOCKED())
                {
                    if (((Game_MQueueWrite + 1) & (GAME_MQUEUE_LENGTH - 1)) == Game_MQueueRead)
                    {
                        /* mouse event queue overflow */
                    }
                    else
                    {
                        Game_EventMQueue[Game_MQueueWrite] = event;
                        Game_MQueueWrite = (Game_MQueueWrite + 1) & (GAME_MQUEUE_LENGTH - 1);
                    }
                }
                break;

#endif

            case SDL_QUIT:
                Game_QuitRequested = 1;
                REQUEST_PROGRAM_EXIT();
                break;

            case SDL_USEREVENT:
                switch (event.user.code)
                {
                    case EC_DISPLAY_FLIP_START:
                        if (!FlipActive && Game_FlipSem != NULL)
                        {
                            FlipActive = 1;
                            SDL_SemPost(Game_FlipSem);
                        }
                        break;

                    case EC_DISPLAY_FLIP_FINISH:
                        if (FlipActive)
                        {
                            Game_Display_Flip();
                            FlipActive = 0;
                        }
                        break;

                    case EC_PROGRAM_QUIT:
                        if (!Thread_Exit)
                        {
                            REQUEST_PROGRAM_EXIT();
                        }
                        break;
                }
                break;

            default:
                Handle_Input_Event2(&event);
                break;
        }
    }

    if (Game_FlipSem != NULL)
    {
        SDL_SemPost(Game_FlipSem);
    }
    if (MainThread != NULL)
    {
        SDL_WaitThread(MainThread, NULL);
        MainThread = NULL;
    }
    (void)TimerThread;
    (void)FlipThread;

#undef REQUEST_PROGRAM_EXIT
}
