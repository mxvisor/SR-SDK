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
#include <string.h>

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

/* Who takes keyboard and mouse events out of SDL's queue: the loop below, except
 * on win32 with the winapi layer.  There the loop reads the whole queue too and
 * hands what is the game's (keys, mouse, touch, the window's events, the user
 * events of the winapi layer) to the input queue of the window's thread
 * (Winapi_RouteEvent, WinApi-user32.c), whose message pump makes window messages
 * of them; the port's panel takes its own first.  On Android the game's thread
 * pumps (GAME_WINAPI_GUEST_PUMP) and this loop does not, but it is still the one
 * that reads. */
#ifndef GAME_CORE_CONSUMES_INPUT
#ifdef GAME_WINAPI
#define GAME_CORE_CONSUMES_INPUT 0
#else
#define GAME_CORE_CONSUMES_INPUT 1
#endif
#endif
#if defined(GAME_WINAPI)
#define WINAPI_ROUTES_INPUT 1
#ifdef __cplusplus
extern "C" {
#endif
int Winapi_RouteEvent(const SDL_Event *event);
int Winapi_DialogOpen(void);
int Winapi_MenuBarOpen(void);
#ifdef __cplusplus
}
#endif
#else
#define WINAPI_ROUTES_INPUT 0
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

/* Game_RunOnMain (Game_thread.h): the requests of the other threads, each on the stack of
 * the thread that waits for it.  run_open: the loop takes them; outside it the caller runs
 * its own (before the loop only the main thread runs; after it, a thread of the game that
 * is still on its way out must not wait for ever). */
typedef struct run_request {
    void (*fn)(void *arg);
    void *arg;
    int done;
    struct run_request *next;
} run_request;

static SDL_mutex *run_lock;
static SDL_cond *run_cond;
static run_request *run_head, *run_tail;
static int run_open;
static SDL_threadID run_main_thread;

void Game_RunOnMain(void (*fn)(void *arg), void *arg)
{
    run_request request;

#if !defined(__ANDROID__)
    if ((run_lock != NULL) && (SDL_ThreadID() != run_main_thread))
    {
        SDL_LockMutex(run_lock);
        if (run_open)
        {
            request.fn = fn;
            request.arg = arg;
            request.done = 0;
            request.next = NULL;
            if (run_tail != NULL) run_tail->next = &request; else run_head = &request;
            run_tail = &request;
            while (!request.done) SDL_CondWait(run_cond, run_lock);
            SDL_UnlockMutex(run_lock);
            return;
        }
        SDL_UnlockMutex(run_lock);
    }
#endif
    (void)request;
    fn(arg);
}

/* on the main thread: the requests waiting now; open = 0 closes the door after them */
static void run_requests(int open)
{
    if (run_lock == NULL) return;
    SDL_LockMutex(run_lock);
    while (run_head != NULL)
    {
        run_request *request = run_head;

        run_head = request->next;
        if (run_head == NULL) run_tail = NULL;
        SDL_UnlockMutex(run_lock);
        request->fn(request->arg);
        SDL_LockMutex(run_lock);
        request->done = 1;
        SDL_CondBroadcast(run_cond);
    }
    run_open = open;
    SDL_UnlockMutex(run_lock);
}

static void show_window(void *window)
{
    SDL_ShowWindow((SDL_Window *)window);
}

static void hide_window(void *window)
{
    SDL_HideWindow((SDL_Window *)window);
}

typedef struct {
    SDL_Window *window;
    SDL_Cursor *cursor;
    int x, y, result;
} pointer_args;

static void show_cursor(void *data)
{
    pointer_args *args = (pointer_args *)data;
    args->result = SDL_ShowCursor(args->x);
}

int Game_ShowCursor(int toggle)
{
    pointer_args args;

    args.x = toggle;
    args.result = -1;
    Game_RunOnMain(show_cursor, &args);
    return args.result;
}

static void set_cursor(void *data)
{
    SDL_SetCursor(((pointer_args *)data)->cursor);
}

void Game_SetCursor(SDL_Cursor *cursor)
{
    pointer_args args;

    args.cursor = cursor;
    Game_RunOnMain(set_cursor, &args);
}

static void relative_mouse(void *data)
{
    pointer_args *args = (pointer_args *)data;
    args->result = SDL_SetRelativeMouseMode(args->x ? SDL_TRUE : SDL_FALSE);
}

int Game_SetRelativeMouseMode(int enabled)
{
    pointer_args args;

    args.x = enabled;
    args.result = -1;
    Game_RunOnMain(relative_mouse, &args);
    return args.result;
}

static void warp_mouse(void *data)
{
    pointer_args *args = (pointer_args *)data;
    SDL_WarpMouseInWindow(args->window, args->x, args->y);
}

void Game_WarpMouse(SDL_Window *window, int x, int y)
{
    pointer_args args;

    args.window = window;
    args.x = x;
    args.y = y;
    Game_RunOnMain(warp_mouse, &args);
}

void Game_ShowWindow(SDL_Window *window)
{
    Game_RunOnMain(show_window, window);
}

void Game_HideWindow(SDL_Window *window)
{
    Game_RunOnMain(hide_window, window);
}

/* The window of the picture: the last one of Game_WindowCreate, and the size it
 * last got from here; a window of another size was resized by the player. */
static SDL_Window *port_window;
static int port_width, port_height;

/* Under gamescope (the game mode of Steam on SteamOS, or gamescope started by hand): it
 * shows a window on the whole screen only when the window asks for it, and scales any
 * other at most twice (--max-scale 2 of Steam), so a 320x240 window stays small; nor
 * does it give a window a frame to resize.  There the window is always on the whole
 * screen, whatever Display_Fullscreen says, and the panel has no item for it.  gamescope
 * sets XDG_CURRENT_DESKTOP=gamescope and GAMESCOPE_WAYLAND_DISPLAY for what it starts; Wine
 * (Proton) passes them on to the Windows build. */
static int under_gamescope(void)
{
#if !defined(__ANDROID__)
    static int mode = -1;
    if (mode < 0)
    {
        const char *desktop = getenv("XDG_CURRENT_DESKTOP");
        mode = (desktop != NULL && strstr(desktop, "gamescope") != NULL) || getenv("GAMESCOPE_WAYLAND_DISPLAY") != NULL;
        if (mode) fprintf(stderr, GAME_NAME ": under gamescope: the window on the whole screen\n");
    }
    return mode;
#else
    return 0;
#endif
}

static unsigned int fullscreen_flag(void)
{
    return (Game_Fullscreen || under_gamescope()) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
}

typedef struct {
    SDL_Window *window;
    int width, height;
    unsigned int flags;
} window_args;

static void window_create(void *data)
{
    window_args *args = (window_args *)data;

    args->window = SDL_CreateWindow(GAME_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, args->width, args->height,
                                    args->flags | SDL_WINDOW_RESIZABLE | fullscreen_flag());
    if (args->window == NULL) return;
    SDL_SetWindowMinimumSize(args->window, args->width, args->height);
    port_window = args->window;
    port_width = args->width;
    port_height = args->height;
}

SDL_Window *Game_WindowCreate(int width, int height, unsigned int flags)
{
    window_args args;

    args.window = NULL;
    args.width = width;
    args.height = height;
    args.flags = flags;
    Game_RunOnMain(window_create, &args);
    return args.window;
}

static void window_fit(void *data)
{
    window_args *args = (window_args *)data;
    SDL_Window *window = args->window;
    int width = args->width, height = args->height;
    int w = 0, h = 0, resized;

    SDL_GetWindowSize(window, &w, &h);
    resized = (window != port_window) || (w != port_width) || (h != port_height) ||
              (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED));
    /* a smaller minimum first: the window may get smaller */
    SDL_SetWindowMinimumSize(window, width, height);
    /* centred again, as it was created: a larger mode would leave the screen */
    if (!resized && (w != width || h != height))
    {
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    port_window = window;
    if (!resized)
    {
        port_width = width;
        port_height = height;
    }
}

void Game_WindowFit(SDL_Window *window, int width, int height)
{
    window_args args;

    if (window == NULL) return;
    args.window = window;
    args.width = width;
    args.height = height;
    args.flags = 0;
    Game_RunOnMain(window_fit, &args);
}

static void window_fullscreen(void *data)
{
    (void)data;
    if (port_window != NULL) SDL_SetWindowFullscreen(port_window, fullscreen_flag());
}

void Game_WindowSetFullscreen(int on)
{
#if !defined(__ANDROID__)
    Game_Fullscreen = on ? 1 : 0;
    Game_RunOnMain(window_fullscreen, NULL);
#else
    (void)on;
#endif
}

#if !defined(__ANDROID__)
static int fullscreen_item_active(void)
{
    return (port_window != NULL) && (SDL_GetWindowFlags(port_window) & SDL_WINDOW_SHOWN);
}

static void fullscreen_item_press(void)
{
    Game_WindowSetFullscreen(!Game_Fullscreen);
}

static const char *fullscreen_item_state(void)
{
    return Game_Fullscreen ? "on" : "off";
}

static const Game_OsdItem fullscreen_item = { "Fullscreen", fullscreen_item_active, fullscreen_item_press, fullscreen_item_state, "Display_Fullscreen", NULL };
#endif

void Game_WindowOsdItems(void)
{
#if !defined(__ANDROID__)
    if (!under_gamescope()) Game_OsdAddItem(&fullscreen_item);
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

    run_main_thread = SDL_ThreadID();
    run_lock = SDL_CreateMutex();
    run_cond = SDL_CreateCond();
    run_open = (run_lock != NULL) && (run_cond != NULL);

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
        run_requests(1);
        NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        if (NumEvents <= 0)
        {
            if (PumpEvents)
            {
                PumpEvents = 0;
#if !(defined(GAME_WINAPI) && GAME_WINAPI_GUEST_PUMP)
                SDL_PumpEvents();
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
#elif WINAPI_ROUTES_INPUT
        /* the panel first, unless a dialog or the window menu of the game is open:
         * those keep their input (they run on the window's thread) */
        if (!Winapi_DialogOpen() && !Winapi_MenuBarOpen() && Game_OsdEvent(&event)) continue;
#endif
        if (Handle_Input_Event(&event)) continue;
#if WINAPI_ROUTES_INPUT
        if (Winapi_RouteEvent(&event)) continue;
#endif

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

    /* the threads of the game that are still on their way out make their own window calls */
    run_requests(0);
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
