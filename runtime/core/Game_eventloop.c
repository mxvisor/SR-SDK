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
 *   GAME_KEYBOARD_BLOCKED()  non-zero while the game reads keys by itself
 *   GAME_MOUSE_BLOCKED()     the same for the mouse
 *   GAME_CLOSE_REQUEST()     non-zero when the game takes the close of the
 *                            window (SDL_QUIT) itself and quits on its own
 * The port's panel (Game_osd.h) sees the input first: its items are GAME_OSD_ITEMS().
 * and Game_Display_Flip() in the game's main.c draws one frame. */

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "Game_osd.h"
#include "Game_hooks.h"

#if !defined(GAME_KEYBOARD_BLOCKED)
#define GAME_KEYBOARD_BLOCKED() 0
#endif
#if !defined(GAME_MOUSE_BLOCKED)
#define GAME_MOUSE_BLOCKED() 0
#endif
#if !defined(GAME_CLOSE_REQUEST)
#define GAME_CLOSE_REQUEST() 0
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
    if (event->key.repeat) return 0;
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

/* SDL's X11 driver waits for the server's answer to showing or hiding a window by
 * reading the connection itself.  A pump on the event loop's thread at that moment
 * takes the answer away, and the guest's thread, which opens the game's window,
 * waits for ever.  The pump stays off while another thread does that. */
static SDL_mutex *Game_PumpMutex;
/* On Android the pump also parks the program on a pause and hands the GL
 * context over, which works on the thread that has the context: the guest's
 * thread draws there, and the message functions pump on it, so this one must
 * not (the surface would die under the renderer: black screen on resume). */

/* On Windows (and Wine) it is the other way round: the window belongs to the thread that
 * made it, this one, and showing, hiding or resizing it from another thread waits until this
 * thread's pump takes the messages; with the pump held off both threads would wait for ever
 * (a game whose driver changes the mode from a thread of its own). */
#ifdef _WIN32
#define PUMP_HELD_FOR_WINDOW 0
#else
#define PUMP_HELD_FOR_WINDOW 1
#endif

void Game_ShowWindow(SDL_Window *window)
{
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_LockMutex(Game_PumpMutex);
    SDL_ShowWindow(window);
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_UnlockMutex(Game_PumpMutex);
}

void Game_HideWindow(SDL_Window *window)
{
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_LockMutex(Game_PumpMutex);
    SDL_HideWindow(window);
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_UnlockMutex(Game_PumpMutex);
}

/* The window of the picture: the last one of Game_WindowCreate, and the size it
 * last got from here; a window of another size was resized by the player. */
static SDL_Window *port_window;
static int port_width, port_height;

SDL_Window *Game_WindowCreate(int width, int height, unsigned int flags)
{
    SDL_Window *window;

    window = SDL_CreateWindow(GAME_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              flags | SDL_WINDOW_RESIZABLE | (Game_Fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (window == NULL) return NULL;
    SDL_SetWindowMinimumSize(window, width, height);
    port_window = window;
    port_width = width;
    port_height = height;
    return window;
}

void Game_WindowFit(SDL_Window *window, int width, int height)
{
    int w = 0, h = 0, resized;

    if (window == NULL) return;
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_LockMutex(Game_PumpMutex);
    SDL_GetWindowSize(window, &w, &h);
    resized = (window != port_window) || (w != port_width) || (h != port_height) ||
              (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED));
    /* a smaller minimum first: the window may get smaller */
    SDL_SetWindowMinimumSize(window, width, height);
    if (!resized && (w != width || h != height)) SDL_SetWindowSize(window, width, height);
    port_window = window;
    if (!resized)
    {
        port_width = width;
        port_height = height;
    }
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_UnlockMutex(Game_PumpMutex);
}

#if !defined(__ANDROID__)
static int fullscreen_item_active(void)
{
    return (port_window != NULL) && (SDL_GetWindowFlags(port_window) & SDL_WINDOW_SHOWN);
}

static void fullscreen_item_press(void)
{
    Game_Fullscreen = !Game_Fullscreen;
    if (port_window == NULL) return;
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_LockMutex(Game_PumpMutex);
    SDL_SetWindowFullscreen(port_window, Game_Fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (PUMP_HELD_FOR_WINDOW && Game_PumpMutex != NULL) SDL_UnlockMutex(Game_PumpMutex);
}

static const char *fullscreen_item_state(void)
{
    return Game_Fullscreen ? "on" : "off";
}

static const Game_OsdItem fullscreen_item = { "Fullscreen", fullscreen_item_active, fullscreen_item_press, fullscreen_item_state, "Display_Fullscreen" };
#endif

void Game_WindowOsdItems(void)
{
#if !defined(__ANDROID__)
    Game_OsdAddItem(&fullscreen_item);
#endif
}

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

    /* The close of the window arrives twice (SDL_WINDOWEVENT_CLOSE, then SDL_QUIT),
     * and the winapi message pump may have taken the first and set Thread_Exit: the
     * time counts from the first request seen here.  Only a repeat after a second
     * means that the game does not stop: leave at once. */
    uint32_t QuitTick = 0;
#define REQUEST_PROGRAM_EXIT() \
    do { \
        if (QuitTick == 0) QuitTick = SDL_GetTicks(); \
        else if (SDL_GetTicks() - QuitTick > 1000) exit(1); \
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

    Game_PumpMutex = SDL_CreateMutex();

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
        {
            NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_LASTEVENT);
#ifdef GAME_WINAPI
            /* the winapi layer's own user events (its timer callbacks, wake-ups of its message
             * queue) are for the guest's message pump: they go back, or the timer of a game
             * that is started again by its callback dies when this loop wins the race */
            if ((NumEvents > 0) && (event.type == SDL_USEREVENT) && ((event.user.code == EC_WINAPI_WAKE)
#if GAME_WINAPI_TIMER_EVENTS
                || (event.user.code == EC_WINAPI_TIMER)
#endif
                ))
            {
                SDL_PushEvent(&event);
                NumEvents = 0;
            }
#endif
        }
#endif
        if (NumEvents <= 0)
        {
            if (PumpEvents)
            {
                PumpEvents = 0;
#if !(defined(GAME_WINAPI) && defined(__ANDROID__))
                if (Game_PumpMutex != NULL) SDL_LockMutex(Game_PumpMutex);
                SDL_PumpEvents();
                if (Game_PumpMutex != NULL) SDL_UnlockMutex(Game_PumpMutex);
#endif
            }
            else
            {
                SDL_Delay(1);
                PumpEvents = 1;
            }
            continue;
        }

#if GAME_CORE_CONSUMES_INPUT
        /* the port's panel first: its key, and all input while it is open */
        if (Game_OsdEvent(&event)) continue;
#endif
        if (Handle_Input_Event(&event)) continue;

        switch (event.type)
        {
            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                        /* Without a WM, xdotool windowclose / XDestroyWindow never
                         * becomes SDL_QUIT — only this event. Same shutdown path. */
                        if (GAME_CLOSE_REQUEST()) break;
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
                if (((AppActive && AppInputFocus) || event.key.windowID == 0)
                    && !GAME_KEYBOARD_BLOCKED())
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
                if (GAME_CLOSE_REQUEST()) break;
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
