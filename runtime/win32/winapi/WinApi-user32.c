/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#ifdef DEBUG_USER32
#include <inttypes.h>
#endif
#include "WinApi-user32.h"
#include "WinApi-ui.h"
#include "Game_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "WinApi-config.h"
#include "WinApi-guest.h"
#include "WinApi-kernel32.h"
#include "Game_memory.h"
#include "Game_defs.h"
#include "Game_hooks.h"
#include "main.h"

/* Game_hooks.h may set: GAME_WINAPI_KEY_MESSAGES, the message pump delivers key events
 * (default 1); GAME_KEYBOARD_BLOCKED(), the game reads keys by itself, so none go to
 * its keyboard queue (default 0); and
 * GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption): a button to answer a message box with,
 * without showing it (default 0: show it) */
#ifndef GAME_WINAPI_KEY_MESSAGES
#define GAME_WINAPI_KEY_MESSAGES 1
#endif
#ifndef GAME_KEYBOARD_BLOCKED
#define GAME_KEYBOARD_BLOCKED() 0
#endif
#ifndef GAME_WINAPI_MESSAGEBOX_ANSWER
#define GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption) 0
#endif
/* GAME_WINAPI_PAINT_WHEN_IDLE: the game draws its picture in its window
 * procedure and waits for the repaint request; the port has no window of its
 * own and makes the request itself when the program has no other message
 * (default 0: no request, the program draws on its own) */
#ifndef GAME_WINAPI_PAINT_WHEN_IDLE
#define GAME_WINAPI_PAINT_WHEN_IDLE 0
#endif
/* GAME_WINAPI_ACTIVATE_APP_ON_IDLE: the program expects to be told that it
 * is not active (WM_ACTIVATEAPP with wParam 0) and then that it is active
 * again (wParam 1); it makes its picture buffer on the first message and
 * draws the scene on the second (default 0: no such messages) */
#ifndef GAME_WINAPI_ACTIVATE_APP_ON_IDLE
#define GAME_WINAPI_ACTIVATE_APP_ON_IDLE 0
#endif
/* GAME_WINAPI_TIMER_EVENTS 1: the callback of timeSetEvent runs, from the game's message
 * pump (default 0: a game that sets a timer never hears from it) */
#ifndef GAME_WINAPI_TIMER_EVENTS
#define GAME_WINAPI_TIMER_EVENTS 0
#endif
/* GAME_WINAPI_WINDOW_MESSAGES 1: the game's message loop looks at the window of a message:
 * input messages carry the window's handle, and PostMessageA delivers the WM_USER range to it
 * (default 0: a handle of zero, and the post fails: the games of SR post these for nothing) */
#ifndef GAME_WINAPI_WINDOW_MESSAGES
#define GAME_WINAPI_WINDOW_MESSAGES 0
#endif
/* GAME_WINAPI_TOUCH_GAMEPAD 1: the game draws the on-screen gamepad
 * (touchgamepad.h) over its picture, and the touches are the gamepad's, not
 * the mouse's (default 0) */
#ifndef GAME_WINAPI_TOUCH_GAMEPAD
#define GAME_WINAPI_TOUCH_GAMEPAD 0
#endif
/* GAME_WINAPI_DESTROY_MESSAGE 1: DestroyWindow sends the window WM_DESTROY, as
 * Windows does (a game that ends its message loop there, with PostQuitMessage,
 * and writes its settings after the loop); default 0: it only succeeds */
#ifndef GAME_WINAPI_DESTROY_MESSAGE
#define GAME_WINAPI_DESTROY_MESSAGE 0
#endif
/* GAME_CLOSE_REQUEST() (Game_eventloop.c): non-zero when the game takes the
 * close of the window itself; Winapi_CloseRequest makes it WM_CLOSE */
#ifndef GAME_CLOSE_REQUEST
#define GAME_CLOSE_REQUEST() 0
#endif

static int paint_pending = 0;

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#define WINAPI_NODEF_DEFINITIONS
#endif
#include "WinApi.h"
#include "WinApi-winmm.h"
#include "touchgamepad.h"
#include "Game_keys.h"
#include "Game_osd.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#else
#include <time.h>
#include "CLIB.h"
#include "WinApi-kernel32.h"
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)


#ifndef _WIN32
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_SWAPBUTTON 23

#define MB_TYPEMASK          0x0000000FL
#define MB_ICONMASK          0x000000F0L
#define MB_DEFMASK           0x00000F00L
#define MB_MODEMASK          0x00003000L

#define MB_OK                0x00000000L
#define MB_OKCANCEL          0x00000001L
#define MB_ABORTRETRYIGNORE  0x00000002L
#define MB_YESNOCANCEL       0x00000003L
#define MB_YESNO             0x00000004L
#define MB_RETRYCANCEL       0x00000005L
#define MB_CANCELTRYCONTINUE 0x00000006L

#define MB_ICONHAND          0x00000010L
#define MB_ICONEXCLAMATION   0x00000030L
#define MB_ICONASTERISK      0x00000040L

#define MB_DEFBUTTON2        0x00000100L
#define MB_DEFBUTTON3        0x00000200L

#define MB_SYSTEMMODAL       0x00001000L

#define IDOK        1
#define IDCANCEL    2
#define IDABORT     3
#define IDRETRY     4
#define IDIGNORE    5
#define IDYES       6
#define IDNO        7
#define IDTRYAGAIN 10
#define IDCONTINUE 11


#define WM_NULL 0x0000
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ACTIVATEAPP 0x001C
#define WM_PAINT 0x000F
#define WM_COMMAND 0x0111
#define PM_NOREMOVE 0
#define PM_REMOVE 1
#define WM_DESTROY 0x0002
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_USER 0x0400
#define WM_APP 0x8000

#define SC_MINIMIZE 0xF020
#define SC_CLOSE 0xF060
#define SC_RESTORE 0xF120


#define MK_CONTROL 0x0008
#define MK_LBUTTON 0x0001
#define MK_MBUTTON 0x0010
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040


#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_CAPITAL 0x14
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7a
#define VK_F12 0x7b
#define VK_F13 0x7c
#define VK_F14 0x7d
#define VK_F15 0x7e
#define VK_F16 0x7f
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91



#define IMAGE_BITMAP        0
#define LR_LOADFROMFILE     0x0010
#endif

#define PSEUDO_HANDLE_MAIN_WINDOW ((void *)(pointer_offset + 1ul))


typedef struct {
    uint32_t x;
    uint32_t y;
} point, *lppoint;

typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} rect, *lprect;

typedef struct {
    PTR32(void) hwnd;
    uint32_t message;
    uint32_t wParam;
    int32_t  lParam;
    uint32_t time;
    point    pt;
} msg, *lpmsg;

typedef struct {
    uint32_t style;
    PTR32(void) lpfnWndProc;
    int32_t cbClsExtra;
    int32_t cbWndExtra;
    PTR32(void) hInstance;
    PTR32(void) hIcon;
    PTR32(void) hCursor;
    PTR32(void) hbrBackground;
    PTR32(const char) lpszMenuName;
    PTR32(const char) lpszClassName;
} wndclassa;



int sdl_versionnum = 0;
static void *lpfnWndProc = NULL;
static uint32_t class_menu_name = 0;
static SDL_Cursor *wait_cursor = NULL;
static int cursor_visibility = 0;
static unsigned int mouse_buttons = 0;
static unsigned int keyboard_mods = 0;
static int mouse_x = 0;
static int mouse_y = 0;
static unsigned int message_time = 0;
static int mouse_right_button_mod = 0;
// doubleclick detection - BEGIN
static const int mouse_doubleclick_max_distance = 10;
static const unsigned int mouse_doubleclick_max_time = 500;

static int mouse_doubleclick = 0;
static uint32_t mouse_last_time[3], mouse_current_time[3];
static int mouse_last_x[3], mouse_last_y[3], mouse_current_x[3], mouse_current_y[3];
static int mouse_last_peep[3];
// doubleclick detection - END

static SDL_Window *mouse_window = NULL;

extern uint32_t winapi_get_dd_window_id(void);
extern int winapi_get_dd_mode(int *width, int *height);

static SDL_Event priority_event;
static uint32_t window_thread_id;    /* the guest thread that created the pseudo window */
#if GAME_WINAPI_ACTIVATE_APP_ON_IDLE
/* how many of the messages of GAME_WINAPI_ACTIVATE_APP_ON_IDLE are sent */
static int activate_app_state = 0;
#endif


#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t CCALL RunWndProc_asm(void *hwnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam, uint32_t (*WndProc)(void *, uint32_t, uint32_t, uint32_t));

/* A game that draws its picture itself (a WinG game) can show the frame it
 * has drawn; the game of the port does it in Game_Wing_Tick. */
#if defined(__GNUC__)
#define WINAPI_WEAK __attribute__((weak))
#else
#define WINAPI_WEAK
#endif
extern void Game_Wing_Tick(void) WINAPI_WEAK;

/* Calls the window procedure the guest registered, for the games that draw
 * their picture in a WM_PAINT handler and wait for the message (a game that
 * uses WinG does). The guest's own message loop is not the one that delivers
 * it: the port does. */
uint32_t CCALL Winapi_CallWndProc(uint32_t hwnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam)
{
    if (lpfnWndProc == NULL)
    {
        return 0;
    }


    return RunWndProc_asm((void *)(uintptr_t)hwnd, uMsg, wParam, lParam,
                          (uint32_t (*)(void *, uint32_t, uint32_t, uint32_t))lpfnWndProc);
}
#ifdef __cplusplus
}
#endif


#ifdef _WIN32
static LONG(__stdcall *NtDelayExecution)(BOOL Alertable, PLARGE_INTEGER DelayInterval);
static LONG(__stdcall *NtSetTimerResolution)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution);
static LONG(__stdcall *NtQueryTimerResolution)(OUT PULONG CoarsestResolution, OUT PULONG FinestResolution, OUT PULONG ActualResolution);

static int use_nt_delay;
static ULONG original_timer_resolution;

static UINT timer_period;

static void restore_timer_resolution(void)
{
    ULONG actual_resolution;

    NtSetTimerResolution(original_timer_resolution, TRUE, &actual_resolution);
}

static void end_timer_period(void)
{
    timeEndPeriod_c(timer_period);
}

#ifdef __cplusplus
extern "C"
#endif
void init_sleepmode(void)
{
    use_nt_delay = 0;

    if (CPU_SleepMode)
    {
        HMODULE ntdll_handle;

        ntdll_handle = GetModuleHandleA("ntdll.dll");
        if (ntdll_handle != NULL)
        {
            NtDelayExecution = (LONG(__stdcall*)(BOOL, PLARGE_INTEGER)) (void *)GetProcAddress(ntdll_handle, "NtDelayExecution");
            NtSetTimerResolution = (LONG(__stdcall*)(ULONG, BOOLEAN, PULONG)) (void *)GetProcAddress(ntdll_handle, "NtSetTimerResolution");
            NtQueryTimerResolution = (LONG(__stdcall*)(PULONG, PULONG, PULONG)) (void *)GetProcAddress(ntdll_handle, "NtQueryTimerResolution");

            if ((NtDelayExecution != NULL) && (NtSetTimerResolution != NULL) && (NtQueryTimerResolution != NULL))
            {
                ULONG coarsest_resolution, finest_resolution, actual_resolution, requested_resolution;

                if (0 <= NtQueryTimerResolution(&coarsest_resolution, &finest_resolution, &actual_resolution))
                {
                    use_nt_delay = 1;

                    requested_resolution = (finest_resolution < 5000)?5000:finest_resolution;

                    if (requested_resolution < actual_resolution)
                    {
                        original_timer_resolution = actual_resolution;

                        if (0 <= NtSetTimerResolution(requested_resolution, TRUE, &actual_resolution))
                        {
                            if (actual_resolution != original_timer_resolution)
                            {
                                atexit(restore_timer_resolution);
                            }
                        }
                    }
                }
            }
        }
    }

    if (CPU_SleepMode && !use_nt_delay)
    {
        TIMECAPS_c tc;

        if (MMSYSERR_NOERROR == timeGetDevCaps_c(&tc, sizeof(tc)))
        {
            timer_period = tc.wPeriodMin;
            if (MMSYSERR_NOERROR == timeBeginPeriod_c(tc.wPeriodMin))
            {
                atexit(end_timer_period);
            }
        }
    }
}
#endif

static void warp_mouse(int x, int y)
{
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    /* the pointer is held by the relative mode; the game puts its cursor back every frame */
    (void)x; (void)y;
    return;
#endif
    if (mouse_window != NULL)
    {
        /* from the display mode's pixels into the picture (mouse_to_mode) */
        SDL_Rect picture;
        int window_w, window_h, mode_w, mode_h;

        SDL_GetWindowSize(mouse_window, &window_w, &window_h);
        if (winapi_get_dd_mode(&mode_w, &mode_h) && (mode_w > 0) && (mode_h > 0))
        {
            Game_UiPictureRect(window_w, window_h, mode_w, mode_h, &picture);
            x = picture.x + (int)(((int64_t)x * 2 + 1) * picture.w / (2 * mode_w));
            y = picture.y + (int)(((int64_t)y * 2 + 1) * picture.h / (2 * mode_h));
        }
        SDL_WarpMouseInWindow(mouse_window, x, y);
    }
}

/* The window (OpenGL) has no logical size to move the mouse into the
 * picture, as SDL_RenderSetLogicalSize would: its motion,
 * buttons and touches come in window pixels, and go to the game in the
 * display mode's, from where Game_GlPresentTexture shows the picture
 * (Game_UiPictureRect). The window is the DirectDraw one: an OpenGL window
 * may be shown before it is that (Winapi_ScreenOpenGL), and a mode change of
 * a shown window sends no SDL_WINDOWEVENT_SHOWN. 0: not such a window. */
static int gl_picture(uint32_t window_id, SDL_Rect *picture, int *mode_w, int *mode_h)
{
    SDL_Window *window;
    int window_w, window_h;

    if ((window_id == 0) || (window_id != winapi_get_dd_window_id())) return 0;
    if (!winapi_get_dd_mode(mode_w, mode_h) || (*mode_w <= 0) || (*mode_h <= 0)) return 0;
    window = SDL_GetWindowFromID(window_id);
    if (window == NULL) return 0;
    mouse_window = window;

    SDL_GetWindowSize(window, &window_w, &window_h);
    Game_UiPictureRect(window_w, window_h, *mode_w, *mode_h, picture);
    if (picture->w <= 0 || picture->h <= 0)
    {
        picture->x = picture->y = 0;
        picture->w = window_w;
        picture->h = window_h;
    }
    return 1;
}

static void mouse_to_mode(SDL_Event *event)
{
    SDL_Rect picture;
    int mode_w, mode_h;
    uint32_t window_id;
    int32_t *x, *y;

    if (event->type == SDL_MOUSEMOTION)
    {
        window_id = event->motion.windowID;
        x = &event->motion.x;
        y = &event->motion.y;
    }
    else if ((event->type == SDL_MOUSEBUTTONDOWN) || (event->type == SDL_MOUSEBUTTONUP))
    {
        window_id = event->button.windowID;
        x = &event->button.x;
        y = &event->button.y;
    }
    else return;
    if (!gl_picture(window_id, &picture, &mode_w, &mode_h)) return;
    if ((picture.x == 0) && (picture.y == 0) && (picture.w == mode_w) && (picture.h == mode_h)) return;

    if (getenv(GAME_ENV("DEBUG_INPUT"))) fprintf(stderr, "[mouse window %d,%d", *x, *y);
    *x = (int32_t)SDL_floor((double)(*x - picture.x) * mode_w / picture.w);
    *y = (int32_t)SDL_floor((double)(*y - picture.y) * mode_h / picture.h);
    if (getenv(GAME_ENV("DEBUG_INPUT"))) fprintf(stderr, " -> mode %d,%d of %dx%d]\n", *x, *y, mode_w, mode_h);
}

static void handle_mouse_button(int pressed, int mouse_button, int event_button, int force_detect, int remove)
{
    if (pressed)
    {
        mouse_buttons |= SDL_BUTTON(event_button);

#if SDL_VERSION_ATLEAST(2,0,2)
        if (force_detect || (sdl_versionnum < SDL_VERSIONNUM(2,0,2)))
#endif
        {
            if (mouse_button >= 0)
            {
                if (!mouse_last_peep[mouse_button])
                {
#ifdef _WIN32
                    mouse_current_time[mouse_button] = GetTickCount();
#else
                    mouse_current_time[mouse_button] = Winapi_GetTicks();
#endif
                    mouse_current_x[mouse_button] = mouse_x;
                    mouse_current_y[mouse_button] = mouse_y;
                }

                if ((((uint32_t)(mouse_current_time[mouse_button] - mouse_last_time[mouse_button])) < mouse_doubleclick_max_time) &&
                    (abs(mouse_current_x[mouse_button] - mouse_last_x[mouse_button]) < mouse_doubleclick_max_distance) &&
                    (abs(mouse_current_y[mouse_button] - mouse_last_y[mouse_button]) < mouse_doubleclick_max_distance)
                   )
                {
                    mouse_doubleclick = 1;
                }

                if (remove)
                {
                    mouse_last_time[mouse_button] = mouse_current_time[mouse_button];
                    mouse_last_x[mouse_button] = mouse_current_x[mouse_button];
                    mouse_last_y[mouse_button] = mouse_current_y[mouse_button];
                }
                mouse_last_peep[mouse_button] = (remove)?0:1;
            }
        }
    }
    else
    {
        mouse_buttons &= ~SDL_BUTTON(event_button);
    }
}

/* Posted messages and the guest's message queues.
 *
 * Windows keeps one queue per thread that has called a message function.
 * This port only needs the main thread's queue in practice, but the queue is
 * tagged with its target thread id so a worker that posts to the main thread
 * does not put its message in its own queue.  A target id of 0 is what the
 * game passes when it has not initialized its "main thread id" global; treat it
 * as the main thread, as the original Windows program effectively would. */
#define WINAPI_MSG_QUEUE_SIZE 256
#define FIND_EVENT_WAKE 2

typedef struct {
    uint32_t target;
    msg m;
} winapi_posted_message;

static winapi_posted_message posted_queue[WINAPI_MSG_QUEUE_SIZE];
static int posted_head;
static int posted_count;
static SDL_mutex *posted_lock;
static SDL_atomic_t posted_lock_state = { 0 };

static SDL_mutex *posted_get_lock(void)
{
    for (;;)
    {
        int state = SDL_AtomicGet(&posted_lock_state);

        if (state == 2) return posted_lock;
        if ((state == 0) && SDL_AtomicCAS(&posted_lock_state, 0, 1))
        {
            posted_lock = SDL_CreateMutex();
            SDL_AtomicSet(&posted_lock_state, 2);
            return posted_lock;
        }
        SDL_Delay(0);
    }
}

static uint32_t message_time_now(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    return Winapi_GetTicks();
#endif
}

static void posted_wake(void)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = SDL_USEREVENT;
    event.user.code = EC_WINAPI_WAKE;
    SDL_PushEvent(&event);
}

static int posted_put(uint32_t target, const msg *m)
{
    SDL_mutex *lock = posted_get_lock();
    int index;

    SDL_LockMutex(lock);
    if (posted_count >= WINAPI_MSG_QUEUE_SIZE)
    {
        SDL_UnlockMutex(lock);
        return 0;
    }
    index = (posted_head + posted_count) % WINAPI_MSG_QUEUE_SIZE;
    posted_queue[index].target = target;
    posted_queue[index].m = *m;
    posted_count++;
    SDL_UnlockMutex(lock);

    posted_wake();
    return 1;
}

/* remove = 0 peeks, remove = 1 takes the first message for this thread */
static int posted_take(uint32_t target, msg *out, int remove)
{
    SDL_mutex *lock = posted_get_lock();
    int index, j, found = 0;

    SDL_LockMutex(lock);
    for (j = 0; j < posted_count; j++)
    {
        index = (posted_head + j) % WINAPI_MSG_QUEUE_SIZE;
        if (posted_queue[index].target != target) continue;

        *out = posted_queue[index].m;
        if (remove)
        {
            for (; j + 1 < posted_count; j++)
            {
                int next = (posted_head + j + 1) % WINAPI_MSG_QUEUE_SIZE;
                posted_queue[index] = posted_queue[next];
                index = next;
            }
            posted_count--;
        }
        found = 1;
        break;
    }
    SDL_UnlockMutex(lock);
    return found;
}

/* main.c asks the game thread to stop by setting Thread_Exit; the message pump
 * is the one place a recompiled Windows program polls often enough to notice. */
extern volatile uint32_t Thread_Exit;
extern void Game_StopMain(void) __attribute__((noreturn));
#ifdef __cplusplus
extern "C"
#endif
void Winapi_StopMainIfExiting(void)
{
    if (Thread_Exit) Game_StopMain();
}
extern SDL_sem *Game_FlipSem;

#ifdef GAME_WINAPI_PUMP_ON_MAIN_THREAD
/* sleeps until an event is queued (at most ms): Game_Event_Loop, on the host's main
 * thread, is the one that pumps; a pump from a game thread races its window calls */
static void wait_queue(unsigned int ms)
{
    SDL_Event ev;

    while (ms-- != 0)
    {
        if (SDL_PeepEvents(&ev, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 0) return;
        SDL_Delay(1);
    }
}
#endif

/* The close of the window as WM_CLOSE to the main window, what DefWindowProc
 * makes of SC_CLOSE: for a game that quits through its own WM_CLOSE and writes
 * its settings on the way (GAME_CLOSE_REQUEST).  One close arrives as two events
 * (SDL_WINDOWEVENT_CLOSE, SDL_QUIT), here and in Game_eventloop.c: the requests
 * of the first second are that close; a later one, 0, stops the game. */
#ifdef __cplusplus
extern "C"
#endif
int Winapi_CloseRequest(void)
{
    static int asked;
    static uint32_t asked_at;

    if (!asked)
    {
        if (!PostMessageA_c(PSEUDO_HANDLE_MAIN_WINDOW, WM_CLOSE, 0, 0)) return 0;
        asked_at = SDL_GetTicks();
        asked = 1;
        return 1;
    }
    return (SDL_GetTicks() - asked_at < 1000) ? 1 : 0;
}

static int find_event(SDL_Event *event, int remove, int wait)
{
#ifndef GAME_WINAPI_PUMP_ON_MAIN_THREAD
    int pump_events;
#endif
    int can_sleep;

    if (Thread_Exit) Game_StopMain();

    if (priority_event.type)
    {
        *event = priority_event;
        if (remove)
        {
            priority_event.type = 0;
        }
        return 1;
    }

#ifndef GAME_WINAPI_PUMP_ON_MAIN_THREAD
    pump_events = 1;
#endif
    can_sleep = (CPU_SleepMode != 2)?1:0;

    while (1)
    {
        int num_events, keep_event;
        int mouse_button;
        SDL_Window *window;


        num_events = SDL_PeepEvents(event, 1, (remove)?SDL_GETEVENT:SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        if (num_events < 0) return 0; // error
        if ((num_events > 0) && (event->type == SDL_USEREVENT) && (event->user.code == EC_WINAPI_WAKE))
        {
            /* a posted message is waiting, but it is not an SDL event: consume
             * the wake-up (also in peek mode) and let GetMessage look at the
             * guest queue again */
            if (!remove) SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            return FIND_EVENT_WAKE;
        }
        if ((num_events > 0) && !Winapi_DialogOpen() && !Winapi_MenuBarOpen() && Game_OsdEvent(event))
        {
            /* the port's panel (Game_osd.h): its key, and all input while it is open;
             * an open dialog or window menu keeps its own */
            if (!remove) SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            continue;
        }
        if ((num_events > 0) && (Winapi_DialogEvent(event) || Winapi_MenuBarEvent(event)))
        {
            /* a dialog or the window menu took it (WinApi-ui.h); when it has closed,
             * the caller looks at its queue again */
            if (!remove) SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (!Winapi_DialogOpen() && !Winapi_MenuBarOpen()) return FIND_EVENT_WAKE;
            continue;
        }
        if (num_events == 0) // no events
        {
            if (Thread_Exit) Game_StopMain();

            /* an open menu or dialog holds the game: no message, no idle turn */
            if (wait || Winapi_MenuBarOpen() || Winapi_DialogOpen())
            {
                /* not for ever: a close of the window sets Thread_Exit and may send no
                 * further event to wake this thread (under Wine it does not) */
#ifdef GAME_WINAPI_PUMP_ON_MAIN_THREAD
                wait_queue(100);
#else
                SDL_WaitEventTimeout(NULL, 100);
#endif
                continue;
            }
#ifndef GAME_WINAPI_PUMP_ON_MAIN_THREAD
            else if (pump_events)
            {
                pump_events = 0;
                SDL_PumpEvents();
                continue;
            }
#endif
            else if (can_sleep)
            {
                if (Display_DelayAfterFlip || Display_VSync)
                {
                    static Uint32 last_time;
                    Uint32 current_time;

                    current_time = SDL_GetTicks();
                    if (current_time != last_time)
                    {
                        last_time = current_time;
                        can_sleep = 0;
                    }
                }

                if (can_sleep)
                {
                    // Send the original to sleep to prevent it from consuming too much cpu
#ifdef _WIN32
                    if (use_nt_delay)
                    {
                        LARGE_INTEGER delay;

                        delay.QuadPart = -5000;
                        NtDelayExecution(FALSE, &delay);
                    }
                    else
                    {
                        Sleep(1);
                    }
#else
                    struct timespec _tp;

                    _tp.tv_sec = 0;
                    _tp.tv_nsec = (CPU_SleepMode == 0)?1000000:500000;

                    nanosleep(&_tp, NULL);
#endif
                }
            }

            return 0;
        }


        keep_event = 0;
        mouse_to_mode(event);
        switch (event->type)
        {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            // key events; GAME_WINAPI_KEY_MESSAGES 0 (Game_hooks.h): the game reads
            // its keys elsewhere (DirectInput, the BIOS queue), and the message
            // pump must not deliver them too
            if (GAME_WINAPI_KEY_MESSAGES)
            {
                keep_event = 1;
            }
            // this pump is the only reader of the keys (Game_Event_Loop leaves them
            // alone): the guest's own keyboard queue (BIOS, kbhit) gets them here;
            // a peek that keeps the event is not the last look at it
            if ((remove || !keep_event) && !GAME_KEYBOARD_BLOCKED())
            {
                Game_QueueKeyboardEvent(event);
            }
            keyboard_mods = event->key.keysym.mod;
            {
                int modvalue;
                switch(event->key.keysym.sym)
                {
                case SDLK_RSHIFT:
                    modvalue = KMOD_RSHIFT;
                    break;

                case SDLK_LSHIFT:
                    modvalue = KMOD_LSHIFT;
                    break;

                case SDLK_RCTRL:
                    modvalue = KMOD_RCTRL;
                    break;

                case SDLK_LCTRL:
                    modvalue = KMOD_LCTRL;
                    break;

                case SDLK_RALT:
                    modvalue = KMOD_RALT;
                    break;

                case SDLK_LALT:
                    modvalue = KMOD_LALT;
                    break;

                case SDLK_LGUI:
                    modvalue = KMOD_LGUI;
                    break;

                case SDLK_RGUI:
                    modvalue = KMOD_RGUI;
                    break;

                default:
                    modvalue = KMOD_NONE;
                    break;
                }

                if (event->type == SDL_KEYDOWN)
                {
                    keyboard_mods |= modvalue;
                }
                else
                {
                    keyboard_mods &= ~modvalue;
                }
            }
            break;

        case SDL_MOUSEMOTION:
            // mouse motion events
            keep_event = 1;
            mouse_buttons = event->motion.state;
            if (mouse_right_button_mod && (mouse_buttons & SDL_BUTTON_LMASK))
            {
                mouse_buttons = (mouse_buttons & ~(SDL_BUTTON_LMASK)) | SDL_BUTTON_RMASK;
            }
            mouse_x = event->motion.x;
            mouse_y = event->motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            // mouse button events
            if (event->button.button == SDL_BUTTON_LEFT)
            {
                if (event->button.state != SDL_PRESSED)
                {
                    if (mouse_right_button_mod)
                    {
                        event->button.button = SDL_BUTTON_RIGHT;
                        if (remove) mouse_right_button_mod = 0;
                    }
                }
            }

            mouse_doubleclick = 0;

            if (event->button.button == SDL_BUTTON_LEFT) mouse_button = 0;
            else if (event->button.button == SDL_BUTTON_RIGHT) mouse_button = 1;
            else if (event->button.button == SDL_BUTTON_MIDDLE) mouse_button = 2;
            else mouse_button = -1;
            if (mouse_button >= 0)
            {
                keep_event = 1;
            }

            mouse_x = event->button.x;
            mouse_y = event->button.y;
            handle_mouse_button((event->button.state == SDL_PRESSED)?1:0, mouse_button, event->button.button, 0, remove);
            break;

        case SDL_JOYAXISMOTION:
        case SDL_JOYBALLMOTION:
        case SDL_JOYHATMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            // the gamepads are read by Game_pad.c; their events are of no use here
            break;

        case SDL_QUIT:
            /* The window's close button and SIGINT.  The game answers WM_SYSCOMMAND/
             * SC_CLOSE with nothing, and whichever thread dequeues this event takes
             * it from main.c's loop, so end the game thread the way main.c would. */
            if (GAME_CLOSE_REQUEST()) break;
            Thread_Exit = 1;
            SDL_SemPost(Game_FlipSem);
            Game_StopMain();

        case SDL_SYSWMEVENT:
            // system specific event
            break;

        case SDL_USEREVENT:
            // user events
#if GAME_WINAPI_TIMER_EVENTS
            if (event->user.code == EC_WINAPI_TIMER)
            {
                keep_event = 1;
            }
            else
#endif
            if ((event->user.code >= 0) && (event->user.code <= 3))
            {
                keep_event = 1;
            }
            break;

#if SDL_VERSION_ATLEAST(2,0,4)
        case SDL_AUDIODEVICEADDED:
        case SDL_AUDIODEVICEREMOVED:
            // audio device event
            break;
#endif

        case SDL_CONTROLLERAXISMOTION:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEREMAPPED:
            // Game_pad.c
            break;

        case SDL_DOLLARGESTURE:
        case SDL_DOLLARRECORD:
            // complex gesture event
            break;

        case SDL_DROPFILE:
#if SDL_VERSION_ATLEAST(2,0,5)
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:
#endif
            // event used to request a file open by the system
            break;

        case SDL_FINGERMOTION:
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
            // finger touch event
#if GAME_WINAPI_TOUCH_GAMEPAD
            if (TouchGamepad_Event(event)) break;
#endif
            if (mouse_window != NULL)
            {
                keep_event = 1;
                if (event->type == SDL_FINGERMOTION)
                {
                    SDL_Rect viewport;
                    int mode_w, mode_h, window_w, window_h;

                    if (gl_picture(SDL_GetWindowID(mouse_window), &viewport, &mode_w, &mode_h))
                    {
                        /* the touch is in the window, 0..1 */
                        SDL_GetWindowSize(mouse_window, &window_w, &window_h);
                        mouse_x = (int)SDL_floor(((double)event->tfinger.x * window_w - viewport.x) * mode_w / viewport.w);
                        mouse_y = (int)SDL_floor(((double)event->tfinger.y * window_h - viewport.y) * mode_h / viewport.h);
                    }
                }
                else if (event->type == SDL_FINGERDOWN)
                {
                    {
                        event->tfinger.timestamp &= 0xfffffffeUL; // remember which mouse button
                        mouse_buttons |= SDL_BUTTON_LMASK;
                    }
                }
                else
                {
                    if (mouse_right_button_mod)
                    {
                        event->tfinger.timestamp |= 1; // remember which mouse button
                        mouse_buttons &= ~SDL_BUTTON_RMASK;
                        if (remove) mouse_right_button_mod = 0;
                    }
                    else
                    {
                        event->tfinger.timestamp &= 0xfffffffeUL; // remember which mouse button
                        mouse_buttons &= ~SDL_BUTTON_LMASK;
                    }
                }
            }
            break;

        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
            // Game_pad.c
            break;

        case SDL_MOUSEWHEEL:
            // mouse wheel event
            break;

        case SDL_MULTIGESTURE:
            // multiple finger gesture event
            break;

        case SDL_TEXTEDITING:
            // keyboard text editing event
            break;

        case SDL_TEXTINPUT:
            // keyboard text input event
            break;

        case SDL_WINDOWEVENT:
            // window state change event
            switch (event->window.event)
            {
            case SDL_WINDOWEVENT_MINIMIZED:
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_FOCUS_GAINED:
            case SDL_WINDOWEVENT_FOCUS_LOST:
                keep_event = 1;
                break;
            case SDL_WINDOWEVENT_CLOSE:
                /* without a WM the close of the window is only this event */
                if (GAME_CLOSE_REQUEST()) break;
                Thread_Exit = 1;
                SDL_SemPost(Game_FlipSem);
                Game_StopMain();
                break;
            case SDL_WINDOWEVENT_SHOWN:
                window = SDL_GetWindowFromID(event->motion.windowID);
                if ((window != NULL) && (event->motion.windowID == winapi_get_dd_window_id()))
                {
                    /* the DirectDraw window: the game's coordinates are the mode's */
                    mouse_window = window;
                }
                break;
            default:
                break;
            }
            break;

        default:
            // user events
            break;
        }

        if (keep_event) return 1;

        if (!remove)
        {
            num_events = SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (num_events <= 0) return 0; // error or no events
        }
    };
}

#ifdef __cplusplus
extern "C"
#endif
void Winapi_PostActivateApp(int active)
{
    uint32_t window_id = winapi_get_dd_window_id();

    SDL_zero(priority_event);
    priority_event.type = SDL_WINDOWEVENT;
    priority_event.window.event = (active)?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
    priority_event.window.windowID = window_id;

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_WINDOWEVENT;
    ev.window.event = (active)?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
    ev.window.windowID = window_id;
    SDL_PushEvent(&ev);
}

static void translate_key(lpmsg lpMsg, int released, int vkey, int scancode, int repeat)
{
    if (getenv(GAME_ENV("DEBUG_INPUT"))) fprintf(stderr, "[message key %s vkey=%d]\n", released ? "up" : "down", vkey);
    lpMsg->message = (released)?WM_KEYUP:WM_KEYDOWN;
    lpMsg->wParam = vkey;
    lpMsg->lParam = scancode << 16;
    if (released)
    {
        lpMsg->lParam |= 1 | (1 << 30) | (1 << 31);
        // missing bits (unused by the original):
        // bit 24 - Indicates whether the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard. The value is 1 if it is an extended key; otherwise, it is 0.
    }
    else
    {
        // missing bits (unused by the original):
        // bits 0-15 - The repeat count for the current message.
        // bit 24 - Indicates whether the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard. The value is 1 if it is an extended key; otherwise, it is 0.
        // bit 30 - The previous key state.
        if (repeat)
        {
            lpMsg->lParam |= 1 | (1 << 30);
        }
    }
}

static void translate_mouse_motion(lpmsg lpMsg)
{
    lpMsg->message = WM_MOUSEMOVE;

    if (mouse_buttons & SDL_BUTTON_LMASK) lpMsg->wParam |= MK_LBUTTON;
    if (mouse_buttons & SDL_BUTTON_RMASK) lpMsg->wParam |= MK_RBUTTON;
    if (mouse_buttons & SDL_BUTTON_MMASK) lpMsg->wParam |= MK_MBUTTON;
#if defined(MK_XBUTTON1)
    if (mouse_buttons & SDL_BUTTON_X1MASK) lpMsg->wParam |= MK_XBUTTON1;
    if (mouse_buttons & SDL_BUTTON_X2MASK) lpMsg->wParam |= MK_XBUTTON2;
#endif
    if (keyboard_mods & KMOD_SHIFT) lpMsg->wParam |= MK_SHIFT;
    if (keyboard_mods & KMOD_CTRL) lpMsg->wParam |= MK_CONTROL;

    lpMsg->lParam = (mouse_x & 0xffff) | ((mouse_y & 0xffff) << 16);
}

static void translate_mouse_button(lpmsg lpMsg, int pressed, int mouse_button, int doubleclick)
{
    switch (mouse_button)
    {
    case 0:
        lpMsg->message = (pressed)?(doubleclick?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN):WM_LBUTTONUP;
        break;
    case 1:
        lpMsg->message = (pressed)?(doubleclick?WM_RBUTTONDBLCLK:WM_RBUTTONDOWN):WM_RBUTTONUP;
        break;
    case 2:
        lpMsg->message = (pressed)?(doubleclick?WM_MBUTTONDBLCLK:WM_MBUTTONDOWN):WM_MBUTTONUP;
        break;
    }

    if (mouse_buttons & SDL_BUTTON_LMASK) lpMsg->wParam |= MK_LBUTTON;
    if (mouse_buttons & SDL_BUTTON_RMASK) lpMsg->wParam |= MK_RBUTTON;
    if (mouse_buttons & SDL_BUTTON_MMASK) lpMsg->wParam |= MK_MBUTTON;
#if defined(MK_XBUTTON1)
    if (mouse_buttons & SDL_BUTTON_X1MASK) lpMsg->wParam |= MK_XBUTTON1;
    if (mouse_buttons & SDL_BUTTON_X2MASK) lpMsg->wParam |= MK_XBUTTON2;
#endif
    if (keyboard_mods & KMOD_SHIFT) lpMsg->wParam |= MK_SHIFT;
    if (keyboard_mods & KMOD_CTRL) lpMsg->wParam |= MK_CONTROL;

    lpMsg->lParam = (mouse_x & 0xffff) | ((mouse_y & 0xffff) << 16);
}

static void translate_event(lpmsg lpMsg, SDL_Event *event, int remove)
{
    int doubleclick, mouse_button, vkey, scancode;

    const static uint8_t vkey_table[128] = {
           0,    0,    0,    0,    0,    0,    0,    0, 0x08, 0x09,    0,    0, 0x0c, 0x0d,    0,    0, /*   0- 15 */
           0,    0,    0, 0x13,    0,    0,    0,    0,    0,    0,    0, 0x1b,    0,    0,    0,    0, /*  16- 31 */
        0x20, 0x31, 0xde, 0x33, 0x34, 0x35, 0x37, 0xde, 0x39, 0x30, 0x38, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /*  32- 47 */
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xba, 0xba, 0xbc, 0xbb, 0xbe, 0xbf, /*  48- 63 */
        0x32, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /*  64- 79 */
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xdb, 0xdc, 0xdd, 0x36, 0xbd, /*  80- 95 */
        0xc0, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /*  96-111 */
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xdb, 0xdc, 0xdd, 0xc0, 0x2e, /* 112-127 */
    };

    const static uint8_t scancode_table[128] = {
           0,    0,    0,    0,    0,    0,    0,    0, 0x0e, 0x0f,    0,    0,    0, 0x1c,    0,    0, /*   0- 15 */
           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x01,    0,    0,    0,    0, /*  16- 31 */
        0x39, 0x02, 0x28, 0x04, 0x05, 0x06, 0x08, 0x28, 0x0a, 0x0b, 0x09, 0x0d, 0x33, 0x0c, 0x34, 0x35, /*  32- 47 */
        0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x27, 0x27, 0x33, 0x0d, 0x34, 0x35, /*  48- 63 */
        0x03, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, /*  64- 79 */
        0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x07, 0x0c, /*  80- 95 */
        0x29, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, /*  96-111 */
        0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x1a, 0x2b, 0x1b, 0x29, 0x53, /* 112-127 */
    };

    memset((void *)lpMsg, 0, sizeof(msg));
    switch (event->type)
    {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            // key events
            if (event->key.keysym.sym < 128)
            {
                vkey = vkey_table[event->key.keysym.sym];
                scancode = scancode_table[event->key.keysym.sym];

                if (Keys_SwitchWSAD)
                {
                    switch((int) event->key.keysym.sym)
                    {
                    case SDLK_a:
                        vkey = VK_LEFT;
                        scancode = 0x4b;
                        break;
                    case SDLK_d:
                        vkey = VK_RIGHT;
                        scancode = 0x4d;
                        break;
                    case SDLK_s:
                        vkey = VK_DOWN;
                        scancode = 0x50;
                        break;
                    case SDLK_w:
                        vkey = VK_UP;
                        scancode = 0x48;
                        break;
                    default:
                        break;
                    }
                }
            }
            else
            {
                vkey = 0;
                scancode = 0;

                switch((int) event->key.keysym.sym)
                {
                case SDLK_KP_PERIOD:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_DECIMAL:VK_DELETE;
                    scancode = 0x53;
                    break;

                case SDLK_KP_DIVIDE:
                    vkey = VK_DIVIDE;
                    scancode = 0x35;
                    break;

                case SDLK_KP_MULTIPLY:
                    vkey = VK_MULTIPLY;
                    scancode = 0x37;
                    break;

                case SDLK_KP_MINUS:
                    vkey = VK_SUBTRACT;
                    scancode = 0x4a;
                    break;

                case SDLK_KP_PLUS:
                    vkey = VK_ADD;
                    scancode = 0x4e;
                    break;

                case SDLK_KP_ENTER:
                    vkey = VK_RETURN;
                    scancode = 0x1c;
                    break;

                case SDLK_KP_EQUALS:
                    vkey = 0xbd;
                    scancode = 0x0d;
                    break;


                case SDLK_UP:
                    if (Keys_SwitchArrowKeys)
                    {
                        vkey = vkey_table[(unsigned char)'W'];
                        scancode = scancode_table[(unsigned char)'W'];
                    }
                    else
                    {
                        vkey = VK_UP;
                        scancode = 0x48;
                    }
                    break;

                case SDLK_DOWN:
                    if (Keys_SwitchArrowKeys)
                    {
                        vkey = vkey_table[(unsigned char)'S'];
                        scancode = scancode_table[(unsigned char)'S'];
                    }
                    else
                    {
                        vkey = VK_DOWN;
                        scancode = 0x50;
                    }
                    break;

                case SDLK_RIGHT:
                    if (Keys_SwitchArrowKeys)
                    {
                        vkey = vkey_table[(unsigned char)'D'];
                        scancode = scancode_table[(unsigned char)'D'];
                    }
                    else
                    {
                        vkey = VK_RIGHT;
                        scancode = 0x4d;
                    }
                    break;

                case SDLK_LEFT:
                    if (Keys_SwitchArrowKeys)
                    {
                        vkey = vkey_table[(unsigned char)'A'];
                        scancode = scancode_table[(unsigned char)'A'];
                    }
                    else
                    {
                        vkey = VK_LEFT;
                        scancode = 0x4b;
                    }
                    break;

                case SDLK_INSERT:
                    vkey = VK_INSERT;
                    scancode = 0x52;
                    break;

                case SDLK_HOME:
                    vkey = VK_HOME;
                    scancode = 0x47;
                    break;

                case SDLK_END:
                    vkey = VK_END;
                    scancode = 0x4f;
                    break;

                case SDLK_PAGEUP:
                    vkey = VK_PRIOR;
                    scancode = 0x49;
                    break;

                case SDLK_PAGEDOWN:
                    vkey = VK_NEXT;
                    scancode = 0x51;
                    break;


                case SDLK_F1:
                    vkey = VK_F1;
                    scancode = 0x3b;
                    break;

                case SDLK_F2:
                    vkey = VK_F2;
                    scancode = 0x3c;
                    break;

                case SDLK_F3:
                    vkey = VK_F3;
                    scancode = 0x3d;
                    break;

                case SDLK_F4:
                    vkey = VK_F4;
                    scancode = 0x3e;
                    break;

                case SDLK_F5:
                    vkey = VK_F5;
                    scancode = 0x3f;
                    break;

                case SDLK_F6:
                    vkey = VK_F6;
                    scancode = 0x40;
                    break;

                case SDLK_F7:
                    vkey = VK_F7;
                    scancode = 0x41;
                    break;

                case SDLK_F8:
                    vkey = VK_F8;
                    scancode = 0x42;
                    break;

                case SDLK_F9:
                    vkey = VK_F9;
                    scancode = 0x43;
                    break;

                case SDLK_F10:
                    vkey = VK_F10;
                    scancode = 0x44;
                    break;

                case SDLK_F11:
                    vkey = VK_F11;
                    scancode = 0x57;
                    break;

                case SDLK_F12:
                    vkey = VK_F12;
                    scancode = 0x58;
                    break;

                case SDLK_F13:
                    vkey = VK_F13;
                    break;

                case SDLK_F14:
                    vkey = VK_F14;
                    break;

                case SDLK_F15:
                    vkey = VK_F15;
                    break;


                case SDLK_CAPSLOCK:
                    vkey = VK_CAPITAL;
                    scancode = 0x3a;
                    break;

                case SDLK_RSHIFT:
                    vkey = VK_SHIFT;
                    scancode = 0x36;
                    break;

                case SDLK_LSHIFT:
                    vkey = VK_SHIFT;
                    scancode = 0x2a;
                    break;

                case SDLK_RCTRL:
                    vkey = VK_CONTROL;
                    scancode = 0x1d;
                    break;

                case SDLK_LCTRL:
                    vkey = VK_CONTROL;
                    scancode = 0x1d;
                    break;

                case SDLK_RALT:
                    vkey = VK_MENU;
                    scancode = 0x38;
                    break;

                case SDLK_LALT:
                    vkey = VK_MENU;
                    scancode = 0x38;
                    break;

                case SDLK_MODE:
                    vkey = 0xa5;
                    scancode = 0x5d;
                    break;

                case SDLK_MENU:
                    vkey = VK_APPS;
                    scancode = 0x38;
                    break;


                case SDLK_PAUSE:
                    vkey = 0x13;
                    break;

                case SDLK_DELETE:
                    vkey = VK_DELETE;
                    scancode = 0x53;
                    break;


                case SDLK_KP_1:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD1:VK_END;
                    scancode = 0x4f;
                    break;

                case SDLK_KP_2:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD2:VK_DOWN;
                    scancode = 0x50;
                    break;

                case SDLK_KP_3:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD3:VK_NEXT;
                    scancode = 0x51;
                    break;

                case SDLK_KP_4:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD4:VK_LEFT;
                    scancode = 0x4b;
                    break;

                case SDLK_KP_5:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD5:VK_CLEAR;
                    scancode = 0x4c;
                    break;

                case SDLK_KP_6:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD6:VK_RIGHT;
                    scancode = 0x4d;
                    break;

                case SDLK_KP_7:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD7:VK_HOME;
                    scancode = 0x47;
                    break;

                case SDLK_KP_8:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD8:VK_UP;
                    scancode = 0x48;
                    break;

                case SDLK_KP_9:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD9:VK_PRIOR;
                    scancode = 0x49;
                    break;

                case SDLK_KP_0:
                    vkey = (event->key.keysym.mod & KMOD_NUM)?VK_NUMPAD0:VK_INSERT;
                    scancode = 0x52;
                    break;


                case SDLK_NUMLOCKCLEAR:
                    vkey = VK_NUMLOCK;
                    scancode = 0x45;
                    break;

                case SDLK_SCROLLLOCK:
                    vkey = VK_SCROLL;
                    scancode = 0x46;
                    break;

                case SDLK_LGUI:
                    vkey = VK_LWIN;
                    scancode = 0x5b;
                    break;

                case SDLK_RGUI:
                    vkey = VK_RWIN;
                    scancode = 0x5c;
                    break;


                case SDLK_F16:
                    vkey = VK_F16;
                    break;

                case SDLK_F17:
                    vkey = VK_F17;
                    break;

                case SDLK_F18:
                    vkey = VK_F18;
                    break;

                case SDLK_F19:
                    vkey = VK_F19;
                    break;

                case SDLK_F20:
                    vkey = VK_F20;
                    break;

                case SDLK_F21:
                    vkey = VK_F21;
                    break;

                case SDLK_F22:
                    vkey = VK_F22;
                    break;

                case SDLK_F23:
                    vkey = VK_F23;
                    break;

                case SDLK_F24:
                    vkey = VK_F24;
                    break;


                case SDLK_RETURN2:
                    vkey = VK_RETURN;
                    scancode = 0x1c;
                    break;

                }
            }

            translate_key(lpMsg, (event->type == SDL_KEYUP)?1:0, vkey, scancode, event->key.repeat);

            break;

    case SDL_MOUSEMOTION:
        // mouse motion events
        translate_mouse_motion(lpMsg);
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        // mouse button events

        doubleclick = 0;
#if SDL_VERSION_ATLEAST(2,0,2)
        if (sdl_versionnum >= SDL_VERSIONNUM(2,0,2))
        {
            if (event->button.clicks > 1)
            {
                doubleclick = 1;
            }
        }
        else
#endif
        {
            if (mouse_doubleclick)
            {
                doubleclick = 1;
            }
        }

        switch (event->button.button)
        {
        case SDL_BUTTON_LEFT:
            mouse_button = 0;
            break;
        case SDL_BUTTON_MIDDLE:
            mouse_button = 2;
            break;
        case SDL_BUTTON_RIGHT:
            mouse_button = 1;
            break;
        default:
            mouse_button = -1;
            break;
        }

        translate_mouse_button(lpMsg, (event->button.state == SDL_PRESSED)?1:0, mouse_button, doubleclick);
        break;

    case SDL_QUIT:
        //lpMsg->message = WM_QUIT;
        lpMsg->message = WM_SYSCOMMAND;
        lpMsg->wParam = SC_CLOSE;
        break;

    case SDL_USEREVENT:
        switch (event->user.code)
        {
        case 0:
            lpMsg->message = WM_QUIT;
            lpMsg->wParam = (uint32_t)(uintptr_t)event->user.data1;
            break;
        case 1:
            lpMsg->message = WM_NULL;
            break;
        case 2:
        case 3:
            lpMsg->message = WM_APP + event->user.code - 2;
            break;
        case EC_WINAPI_TIMER:
            /* Timer callback: data1 = guest function address, data2 = dwUser of timeSetEvent.
               The callback is a stdcall TIMECALLBACK taking 5 arguments. */
            if (event->user.data1)
            {
                uint32_t args[5];

                args[0] = 1; /* the id timeSetEvent returned to the game */
                args[1] = 0;
                args[2] = (uint32_t)(uintptr_t)event->user.data2;
                args[3] = 0;
                args[4] = 0;
                Winapi_CallGuest((uint32_t)(uintptr_t)event->user.data1, 5, args);
            }
            break;
        }

        if (remove)
        {
            priority_event.type = 0;
        }
        break;

    case SDL_FINGERMOTION:
        // finger touch event
        translate_mouse_motion(lpMsg);
        break;
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
        translate_mouse_button(lpMsg, (event->type == SDL_FINGERDOWN)?1:0, (event->tfinger.timestamp & 1)?1:0, 0);
        break;

    case SDL_WINDOWEVENT:
        // window state change event
        switch (event->window.event)
        {
        case SDL_WINDOWEVENT_MINIMIZED:
            lpMsg->message = WM_SYSCOMMAND;
            lpMsg->wParam = SC_MINIMIZE;
            break;
        case SDL_WINDOWEVENT_RESTORED:
            lpMsg->message = WM_SYSCOMMAND;
            lpMsg->wParam = SC_RESTORE;
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            lpMsg->message = WM_ACTIVATEAPP;
            lpMsg->wParam = 1;
            lpMsg->lParam = 0;
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            lpMsg->message = WM_ACTIVATEAPP;
            lpMsg->wParam = 0;
            lpMsg->lParam = 0;
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            /* the window was uncovered: Windows asks the program to repaint
             * it, and a program that draws its picture in the window
             * procedure waits for that message */
            lpMsg->message = WM_PAINT;
            lpMsg->wParam = 0;
            lpMsg->lParam = 0;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

#if GAME_WINAPI_WINDOW_MESSAGES
    /* SDL input is a message for the program's window; WM_QUIT is a thread message */
    if (lpMsg->message != WM_QUIT)
    {
        lpMsg->hwnd.set_value(GAME_PTR_TO_GUEST32(PSEUDO_HANDLE_MAIN_WINDOW));
    }
#endif
}


uint32_t CCALL AdjustWindowRectEx_c(void */*lpRect*/, uint32_t /*dwStyle*/, uint32_t /*bMenu*/, uint32_t /*dwExStyle*/)
{
    /* the port's window has no frame, title bar or menu bar of its own: the
     * window's rectangle is its client rectangle (see AdjustWindowRect_c) */
    return 1;
//    return AdjustWindowRectEx((LPRECT)lpRect, dwStyle, bMenu, dwExStyle);
}

uint32_t CCALL ClipCursor_c(const void */*lpRect*/)
{
    return 1;
//    return ClipCursor((const RECT *)lpRect);
}

void * CCALL CreateWindowExA_c(uint32_t /*dwExStyle*/, const char */*lpClassName*/, const char */*lpWindowName*/, uint32_t /*dwStyle*/, int32_t /*x*/, int32_t /*y*/, int32_t /*nWidth*/, int32_t /*nHeight*/, void */*hWndParent*/, void *hMenu_arg, void */*hInstance*/, void */*lpParam*/)
{
#ifdef DEBUG_USER32
    eprintf("CreateWindowExA: 0x%x, %s, %s, 0x%x, %i, %i, %i, %i\n", dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight);
#endif

    SDL_version linked;

    SDL_GetVersion(&linked);
    sdl_versionnum = SDL_VERSIONNUM(linked.major, linked.minor, linked.patch);

    /* Windows posts a window message to the thread that created the window. */
    window_thread_id = GetCurrentThreadId_c();

    priority_event.type = 0;
    /* a window without its own menu gets the one of its class (WNDCLASS.lpszMenuName),
     * so the game finds it at once with GetMenu */
    if ((hMenu_arg == NULL) && (class_menu_name != 0) && (GetMenu_c(1) == 0))
    {
        uint32_t menu = LoadMenuA_c(0, class_menu_name);
        if (menu != 0) SetMenu_c(1, menu);
    }

    return PSEUDO_HANDLE_MAIN_WINDOW;
}

uint32_t CCALL DefWindowProcA_c(void *hWnd, uint32_t Msg, uint32_t wParam, uint32_t lParam)
{
#ifdef DEBUG_USER32
    eprintf("DefWindowProcA: 0x%" PRIxPTR ", 0x%x, 0x%x, 0x%x\n", (uintptr_t)hWnd, Msg, wParam, lParam);
#endif
    (void)wParam;
    (void)lParam;

    /* as on Windows: a game that quits with PostMessage(WM_CLOSE) and leaves the
     * message to DefWindowProc gets its WM_DESTROY (GAME_WINAPI_DESTROY_MESSAGE) */
    if (Msg == WM_CLOSE) DestroyWindow_c(hWnd);

    return 0;
}

uint32_t CCALL DestroyWindow_c(void *hWnd)
{
#ifdef DEBUG_USER32
    eprintf("DestroyWindow: 0x%" PRIxPTR "\n", (uintptr_t)hWnd);
#endif

    /* Every window is the one pseudo window (CreateWindowExA), whose SDL window
     * belongs to DirectDraw and goes with it; the program destroys its window on
     * the way out, so there is nothing left to do but succeed, and, for a game
     * that waits for it, to send WM_DESTROY (once). */
#if GAME_WINAPI_DESTROY_MESSAGE
    {
        static int destroyed;

        if (!destroyed && lpfnWndProc != NULL)
        {
            destroyed = 1;
            RunWndProc_asm(hWnd, WM_DESTROY, 0, 0, (uint32_t (*)(void *, uint32_t, uint32_t, uint32_t))lpfnWndProc);
        }
    }
#endif
    (void)hWnd;
    return 1;
}

uint32_t CCALL DispatchMessageA_c(void *lpMsg)
{
    if (lpMsg == NULL)
    {
        return 0;
    }

    if ((((lpmsg)lpMsg)->message == WM_TIMER) && (((lpmsg)lpMsg)->lParam != 0))
    {
        eprintf("Unimplemented: %s\n", "DispatchMessageA");
        exit(1);
    }
    else if (lpfnWndProc == NULL)
    {
        if (((lpmsg)lpMsg)->message == WM_QUIT)
        {
            exit(((lpmsg)lpMsg)->wParam);
        }

        return 0;
    }
    else
    {
        return RunWndProc_asm(((lpmsg)lpMsg)->hwnd, ((lpmsg)lpMsg)->message, ((lpmsg)lpMsg)->wParam, ((lpmsg)lpMsg)->lParam, (uint32_t (*)(void *, uint32_t, uint32_t, uint32_t))lpfnWndProc);
    }
}

/* GetKeyState / GetAsyncKeyState of a key the cases above do not know: the table
 * of GetKeyboardState (bit 15 = down; GetKeyState adds the toggle in bit 0) */
static int16_t key_state_word(int32_t vk, int toggle)
{
    uint8_t state[256];

    if (vk < 0 || vk > 255) return 0;
    GetKeyboardState_c(state);
    return (int16_t)(((state[vk] & 0x80) ? 0x8000 : 0) | (toggle ? (state[vk] & 1) : 0));
}

int16_t CCALL GetAsyncKeyState_c(int32_t vKey)
{
#ifdef DEBUG_USER32
    eprintf("GetAsyncKeyState: %i\n", vKey);
#endif
    if (vKey == VK_SHIFT)
    {
        if (Game_OsdOpen()) return 0;
        return (keyboard_mods & KMOD_SHIFT)?0x8000:0;
    }

    return key_state_word(vKey, 0);
}

#if defined(GAME_WINDOW_WIDTH) && defined(GAME_WINDOW_HEIGHT)
/* The window of a game that sizes its picture by a fixed display mode: the
 * rectangle is the mode's size at the origin, whatever the host window is
 * (the picture is scaled to it).  A RECT is four LONGs. */
static uint32_t window_rect(uint32_t rect)
{
    if (rect != 0)
    {
        int32_t *r = (int32_t *)GAME_GUEST32_TO_PTR(rect);

        r[0] = r[1] = 0;
        r[2] = GAME_WINDOW_WIDTH;
        r[3] = GAME_WINDOW_HEIGHT;
    }
    return 1;
}

uint32_t CCALL GetClientRect_c(uint32_t hwnd, uint32_t rect)
{
    (void)hwnd;
    return window_rect(rect);
}

uint32_t CCALL GetWindowRect_c(uint32_t hwnd, uint32_t rect)
{
    (void)hwnd;
    return window_rect(rect);
}
#endif

/* RECT is four LONGs */
uint32_t CCALL CopyRect_c(uint32_t lprcDst, uint32_t lprcSrc)
{
    if (lprcDst == 0 || lprcSrc == 0) return 0;
    memmove(GUEST_MEM(lprcDst), GUEST_MEM(lprcSrc), 16);
    return 1;
}

/* the port's window keeps its place and size: the player's */
uint32_t CCALL MoveWindow_c(uint32_t hWnd, uint32_t X, uint32_t Y, uint32_t nWidth, uint32_t nHeight, uint32_t bRepaint)
{
    (void)hWnd; (void)X; (void)Y; (void)nWidth; (void)nHeight; (void)bRepaint;
    return 1;
}

/* the one window has the focus and the mouse */
uint32_t CCALL GetFocus_c(void)
{
    return GAME_PTR_TO_GUEST32(PSEUDO_HANDLE_MAIN_WINDOW);
}

static uint32_t capture_window;

uint32_t CCALL SetCapture_c(uint32_t hWnd)
{
    uint32_t old = capture_window;

    capture_window = hWnd;
    return old;
}

uint32_t CCALL ReleaseCapture_c(void)
{
    capture_window = 0;
    return 1;
}

/* a window procedure of the game, called by the game (a subclassed control) */
uint32_t CCALL CallWindowProcA_c(uint32_t lpPrevWndFunc, uint32_t hWnd, uint32_t Msg, uint32_t wParam, uint32_t lParam)
{
    uint32_t args[4];

    if (lpPrevWndFunc == 0) return 0;
    args[0] = hWnd;
    args[1] = Msg;
    args[2] = wParam;
    args[3] = lParam;
    return Winapi_CallGuest(lpPrevWndFunc, 4, args);
}

/* the names of the keys of an English Windows, by the scan code of lParam (bits
 * 16-23) and its extended bit (24) */
uint32_t CCALL GetKeyNameTextA_c(uint32_t lParam, uint32_t lpString, uint32_t cchSize)
{
    static const char *const names[0x59] = {
        NULL, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S",
        "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Shift", "\\", "Z", "X", "C", "V",
        "B", "N", "M", ",", ".", "/", "Right Shift", "Num *", "Alt", "Space", "Caps Lock", "F1", "F2", "F3",
        "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Pause", "Scroll Lock", "Num 7", "Num 8", "Num 9", "Num -", "Num 4", "Num 5", "Num 6",
        "Num +", "Num 1", "Num 2", "Num 3", "Num 0", "Num Del", NULL, NULL, NULL, "F11", "F12"
    };
    unsigned int scan = (lParam >> 16) & 0xff;
    const char *name = (scan < 0x59) ? names[scan] : NULL;
    char *out;

    if (lParam & (1u << 24))
    {
        switch (scan)
        {
            case 0x1c: name = "Num Enter"; break;
            case 0x1d: name = "Right Ctrl"; break;
            case 0x35: name = "Num /"; break;
            case 0x38: name = "Right Alt"; break;
            case 0x45: name = "Num Lock"; break;
            case 0x47: name = "Home"; break;
            case 0x48: name = "Up"; break;
            case 0x49: name = "Page Up"; break;
            case 0x4b: name = "Left"; break;
            case 0x4d: name = "Right"; break;
            case 0x4f: name = "End"; break;
            case 0x50: name = "Down"; break;
            case 0x51: name = "Page Down"; break;
            case 0x52: name = "Insert"; break;
            case 0x53: name = "Delete"; break;
        }
    }
    if (lpString == 0 || cchSize == 0) return 0;
    out = (char *)GUEST_MEM(lpString);
    if (name == NULL)
    {
        out[0] = 0;
        return 0;
    }
    strncpy(out, name, cchSize - 1);
    out[cchSize - 1] = 0;
    return (uint32_t)strlen(out);
}

uint32_t CCALL GetCursorPos_c(void *lpPoint)
{
    int x, y;

#ifdef DEBUG_USER32
    eprintf("GetCursorPos: 0x%" PRIxPTR " - ", (uintptr_t)lpPoint);
#endif

    // SDL_GetMouseState returns real window coordinates instead of renderer logical coordinates
    // also last event coordinates are probably more appropriate for the original
    x = mouse_x;
    y = mouse_y;

    ((lppoint)lpPoint)->x = x;
    ((lppoint)lpPoint)->y = y;

#ifdef DEBUG_USER32
    eprintf("OK: %i x %i", x, y);
#endif

    return 1;
}

int16_t CCALL GetKeyState_c(int32_t nVirtKey)
{
#ifdef DEBUG_USER32
    eprintf("GetKeyState: %i\n", nVirtKey);
#endif

    if (nVirtKey == VK_CAPITAL)
    {
        return (keyboard_mods & KMOD_CAPS)?1:0;
    }

    return key_state_word(nVirtKey, 1);
}

/* 256 bytes: the high bit of the byte of a virtual key is set while the key is
 * down, the low bit of a toggle key while it is on.  The bytes are never left
 * unwritten, a game may poll them (to cancel a movie, for example). */
static void key_state_set(uint8_t *state, SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z') state[key - 'a' + 'A'] = 0x80;
    else if (key >= '0' && key <= '9') state[key] = 0x80;
    else if (key == SDLK_SPACE) state[0x20] = 0x80;
    else if (key == SDLK_RETURN) state[0x0D] = 0x80;
    else if (key == SDLK_ESCAPE) state[0x1B] = 0x80;
    else if (key == SDLK_BACKSPACE) state[0x08] = 0x80;
    else if (key == SDLK_TAB) state[0x09] = 0x80;
    else if (key == SDLK_LEFT) state[0x25] = 0x80;
    else if (key == SDLK_UP) state[0x26] = 0x80;
    else if (key == SDLK_RIGHT) state[0x27] = 0x80;
    else if (key == SDLK_DOWN) state[0x28] = 0x80;
}

uint32_t CCALL GetKeyboardState_c(void *lpKeyState)
{
    uint8_t *state = (uint8_t *)lpKeyState;
    const uint8_t *keys;
    SDL_Keymod mods = SDL_GetModState();
    SDL_Keycode held;
    int count = 0, sc;

    if (state == NULL) return 0;
    Game_AutoKeysStart();
    memset(state, 0, 256);
    /* the keys are the port's panel's while it is open */
    if (Game_OsdOpen()) return 1;
    keys = SDL_GetKeyboardState(&count);
    for (sc = 0; sc < count; sc++)
    {
        if ((keys[sc] && !Game_OsdKeyHidden((SDL_Scancode)sc)) || Game_AutoKeyDown((SDL_Scancode)sc)) key_state_set(state, SDL_GetKeyFromScancode((SDL_Scancode)sc));
    }
    for (sc = 0; TouchGamepad_HeldKey(sc, &held); sc++)
    {
        key_state_set(state, held);
    }
    if (mods & KMOD_SHIFT) state[0x10] = 0x80;
    if (mods & KMOD_CTRL) state[0x11] = 0x80;
    if (mods & KMOD_ALT) state[0x12] = 0x80;
    if (mods & KMOD_CAPS) state[VK_CAPITAL] = 0x01;
    return 1;
}

uint32_t CCALL GetMessageA_c(void *lpMsg, void *hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax)
{
#ifdef DEBUG_USER32
    eprintf("GetMessageA: 0x%" PRIxPTR ", %i, %i\n", (intptr_t)hWnd, wMsgFilterMin, wMsgFilterMax);
#endif

    if (lpMsg == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    if ((hWnd == NULL) && (wMsgFilterMin == 0) && (wMsgFilterMax == 0))
    {
        uint32_t thread_id = GetCurrentThreadId_c();
        msg posted;

        for (;;)
        {
            if (posted_take(thread_id, &posted, 1))
            {
                *((lpmsg)lpMsg) = posted;
                message_time = posted.time;
                if (posted.message == WM_PAINT) paint_pending = 0;
                if (getenv(GAME_ENV("MSG_LOG"))) fprintf(stderr, "[msg get tid=%u hwnd=%x msg=%x wp=%x lp=%x]\n", thread_id, posted.hwnd.get_value(), posted.message, posted.wParam, posted.lParam);
                return (posted.message == WM_QUIT)?0:1;
            }

            {
                SDL_Event event;
                int found = find_event(&event, 1, 1);

                if (found == FIND_EVENT_WAKE) continue;
                if (!found)
                {
                    Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return -1;
                }

                translate_event((lpmsg)lpMsg, &event, 1);
            }

#ifdef _WIN32
            message_time = GetTickCount();
#else
            message_time = Winapi_GetTicks();
#endif

            return (((lpmsg)lpMsg)->message == WM_QUIT)?0:1;
        }
    }

    eprintf("Unsupported method: %s\n", "GetMessageA");
    exit(1);
}

uint32_t CCALL GetMessagePos_c(void)
{
#ifdef DEBUG_USER32
    eprintf("GetMessagePos\n");
#endif

    return (mouse_x & 0xffff) | ((mouse_y & 0xffff) << 16);
}

uint32_t CCALL GetMessageTime_c(void)
{
#ifdef DEBUG_USER32
    eprintf("GetMessageTime\n");
#endif

    return message_time;
}

int32_t CCALL GetSystemMetrics_c(int32_t nIndex)
{
#ifdef DEBUG_USER32
    eprintf("GetSystemMetrics: %i\n", nIndex);
#endif

    switch (nIndex)
    {
        case SM_CXSCREEN:
            return 640;
        case SM_CYSCREEN:
            return 480;
        case SM_SWAPBUTTON:
            return 0;
        default:
            exit(1);
    }
}

void * CCALL LoadCursorA_c(void *hInstance, PTR32(const char) lpCursorName)
{
#ifdef DEBUG_USER32
    if ( (GET_PTR32_VALUE(lpCursorName) & ~(uint32_t)0xffff) == 0 )
    {
        eprintf("LoadCursorA: 0x%" PRIxPTR ", %i\n", (uintptr_t) hInstance, GET_PTR32_VALUE(lpCursorName));
    }
    else
    {
        eprintf("LoadCursorA: 0x%" PRIxPTR ", %s\n", (uintptr_t) hInstance, (const char *) lpCursorName);
    }
#endif

    if ((hInstance == NULL) && ((GET_PTR32_VALUE(lpCursorName) & ~(uint32_t)0xffff) == 0))
    {
        uint32_t predefined_cursor;
        SDL_Cursor* cursor;

        predefined_cursor = GET_PTR32_VALUE(lpCursorName);

        if (predefined_cursor == 32514) // IDC_WAIT
        {
            // the original loads and sets cursor only once and doesn't free it, so SDL cursor can be used directly
            cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
            if (cursor != NULL)
            {
                PTR32(void) result;

                wait_cursor = cursor;
                SET_PTR32_VALUE(result, 32514);
                return (void *)result;
            }
        }
    }

    Winapi_SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
}

void * CCALL LoadIconA_c(void */*hInstance*/, PTR32(const char) /*lpIconName*/)
{
#ifdef DEBUG_USER32
    if ( (GET_PTR32_VALUE(lpIconName) & ~(uint32_t)0xffff) == 0 )
    {
        eprintf("LoadIconA: 0x%" PRIxPTR ", %i\n", (uintptr_t) hInstance, GET_PTR32_VALUE(lpIconName));
    }
    else
    {
        eprintf("LoadIconA: 0x%" PRIxPTR ", %s\n", (uintptr_t) hInstance, (const char *) lpIconName);
    }
#endif

    // the original only uses icon for RegisterClassA
    Winapi_SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
}

void * CCALL LoadImageA_c(void */*hinst*/, const char *lpszName, uint32_t uType, int32_t /*cxDesired*/, int32_t /*cyDesired*/, uint32_t fuLoad)
{
#ifndef _WIN32
    char buf[8192];
#endif

#ifdef DEBUG_USER32
    eprintf("LoadImageA: 0x%" PRIxPTR ", %s, %i, %i, %i, %i\n", (uintptr_t) hinst, lpszName, uType, cxDesired, cyDesired, fuLoad);
#endif

    if ((uType == IMAGE_BITMAP) && (fuLoad == LR_LOADFROMFILE))
    {
        if (lpszName == NULL)
        {
            Winapi_SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }


#ifdef _WIN32
        if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(lpszName))
#else
        if (!CLIB_FindFile(lpszName, buf))
#endif
        {
            Winapi_SetLastError(ERROR_FILE_NOT_FOUND);
            return NULL;
        }
    }

    eprintf("Unsupported method: %s\n", "LoadImageA");
    exit(1);
}

uint32_t CCALL MessageBoxA_c(void *hWnd, const char *lpText, const char *lpCaption, uint32_t uType)
{
    SDL_MessageBoxData data;
    SDL_MessageBoxButtonData buttons[3];
    int answer = GAME_WINAPI_MESSAGEBOX_ANSWER(lpText, lpCaption);

    eprintf("MessageBoxA: \"%s\" / \"%s\" (0x%x)\n", lpText ? lpText : "", lpCaption ? lpCaption : "", uType);

    if (answer) return (uint32_t)answer;
#if defined(GAME_WINAPI_DIALOGS) && GAME_WINAPI_DIALOGS
    /* once the game shows its picture: a dialog over it (WinApi-dialog.c); the
     * keys reach the window procedure without a window, so hWnd says nothing */
    {
        int ui_w, ui_h;

        Game_UiSize(&ui_w, &ui_h);
        if (ui_w > 0 && ui_h > 0) return Winapi_MessageBoxDialog(lpText, lpCaption, uType);
    }
#endif

    data.window = NULL;
    data.title = lpCaption;
    data.message = lpText;
    data.buttons = buttons;
    data.colorScheme = NULL;

    switch (uType & MB_TYPEMASK)
    {
        case MB_OK:
            data.numbuttons = 1;
            buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[0].buttonid = IDOK;
            buttons[0].text = "OK";
            break;
        case MB_OKCANCEL:
            data.numbuttons = 2;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDOK;
            buttons[0].text = "OK";
            buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[1].buttonid = IDCANCEL;
            buttons[1].text = "Cancel";
            break;
        case MB_ABORTRETRYIGNORE:
            data.numbuttons = 3;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDABORT;
            buttons[0].text = "Abort";
            buttons[1].flags = 0;
            buttons[1].buttonid = IDRETRY;
            buttons[1].text = "Retry";
            buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[2].buttonid = IDIGNORE;
            buttons[2].text = "Ignore";
            break;
        case MB_YESNOCANCEL:
            data.numbuttons = 3;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDYES;
            buttons[0].text = "Yes";
            buttons[1].flags = 0;
            buttons[1].buttonid = IDNO;
            buttons[1].text = "No";
            buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[2].buttonid = IDCANCEL;
            buttons[2].text = "Cancel";
            break;
        case MB_YESNO:
            data.numbuttons = 2;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDYES;
            buttons[0].text = "Yes";
            buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[1].buttonid = IDNO;
            buttons[1].text = "No";
            break;
        case MB_RETRYCANCEL:
            data.numbuttons = 2;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDRETRY;
            buttons[0].text = "Retry";
            buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[1].buttonid = IDCANCEL;
            buttons[1].text = "Cancel";
            break;
        case MB_CANCELTRYCONTINUE:
            data.numbuttons = 3;
            buttons[0].flags = 0;
            buttons[0].buttonid = IDCANCEL;
            buttons[0].text = "Cancel";
            buttons[1].flags = 0;
            buttons[1].buttonid = IDTRYAGAIN;
            buttons[1].text = "Try Again";
            buttons[2].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
            buttons[2].buttonid = IDCONTINUE;
            buttons[2].text = "Continue";
            break;
        default:
            data.numbuttons = 0;
            break;
    }

    switch (uType & MB_ICONMASK)
    {
        case MB_ICONHAND:
            data.flags = SDL_MESSAGEBOX_ERROR;
            break;
        case MB_ICONEXCLAMATION:
            data.flags = SDL_MESSAGEBOX_WARNING;
            break;
        case MB_ICONASTERISK:
            data.flags = SDL_MESSAGEBOX_INFORMATION;
            break;
        default:
            data.flags = 0;
            break;
    }

    switch (uType & MB_DEFMASK)
    {
        case MB_DEFBUTTON2:
            buttons[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
            break;
        case MB_DEFBUTTON3:
            buttons[2].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
            break;
        default:
            buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
            break;
    }

    if ((hWnd == NULL) && ((uType & MB_MODEMASK) == MB_SYSTEMMODAL) && ((uType & ~(MB_TYPEMASK | MB_ICONMASK | MB_DEFMASK | MB_MODEMASK)) == 0))
    {
        if (data.numbuttons == 1)
        {
            if (0 == SDL_ShowSimpleMessageBox(data.flags, data.title, data.message, data.window))
            {
                return IDOK;
            }
        }
        else if (data.numbuttons > 1)
        {
            int buttonid;

            if (0 == SDL_ShowMessageBox(&data, &buttonid))
            {
                return buttonid;
            }
        }
    }

    eprintf("MessageBoxA: 0x%x\n\tCaption: %s\n\tText: %s\n", uType, lpCaption, lpText);

    /* no box could be shown: answer with the first button (OK, or Yes) */
    switch (uType & MB_TYPEMASK)
    {
        case MB_OK:
        case MB_OKCANCEL:
            return IDOK;
        case MB_YESNOCANCEL:
        case MB_YESNO:
            return IDYES;
    }

    eprintf("Unsupported method: %s\n", "MessageBoxA");
    exit(1);
}

uint32_t CCALL OffsetRect_c(void *lprc, int32_t dx, int32_t dy)
{
#ifdef DEBUG_USER32
    eprintf("OffsetRect: 0x%" PRIxPTR ", %i, %i\n", (intptr_t)lprc, dx, dy);
#endif

    if (lprc == NULL) return 0;

    ((lprect)lprc)->left += dx;
    ((lprect)lprc)->right += dx;
    ((lprect)lprc)->top += dy;
    ((lprect)lprc)->bottom += dy;

    return 1;
}

/* No message: the program gets on with its work, usually drawing the next frame.  The
 * port has no window of its own, so it must not invent a WM_PAINT here: a program that
 * draws when it has no message (and waits for a message to redraw) would never draw at
 * all.  A game that asks for it gets the repaint request the window of Windows would
 * send it. */
static int peek_idle(void *lpMsg)
{
    (void)lpMsg;
#if GAME_WINAPI_ACTIVATE_APP_ON_IDLE
    if (activate_app_state < 2 && lpfnWndProc != NULL && lpMsg != NULL)
    {
        ((lpmsg)lpMsg)->hwnd = (void *)PSEUDO_HANDLE_MAIN_WINDOW;
        ((lpmsg)lpMsg)->message = WM_ACTIVATEAPP;
        ((lpmsg)lpMsg)->wParam = (activate_app_state == 0) ? 0 : 1;
        ((lpmsg)lpMsg)->lParam = 0;
        activate_app_state++;
        return 1;
    }
#endif
#if GAME_WINAPI_PAINT_WHEN_IDLE
    if (lpfnWndProc != NULL && lpMsg != NULL)
    {
        ((lpmsg)lpMsg)->hwnd = (void *)PSEUDO_HANDLE_MAIN_WINDOW;
        ((lpmsg)lpMsg)->message = WM_PAINT;
        ((lpmsg)lpMsg)->wParam = 0;
        ((lpmsg)lpMsg)->lParam = 0;
        return 1;
    }
#endif

    if (Game_Wing_Tick != NULL)
    {
        Game_Wing_Tick();
    }

    return 0;
}

uint32_t CCALL PeekMessageA_c(void *lpMsg, void *hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg)
{
#ifdef DEBUG_USER32
    eprintf("PeekMessageA: 0x%" PRIxPTR ", %i, %i, %i\n", (uintptr_t)hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
#endif

    /* the program has one window, so a handle names the same queue as NULL does */
    if ((wMsgFilterMin == 0) && (wMsgFilterMax == 0) && ((wRemoveMsg == 0) || (wRemoveMsg == 1)))
    {
        uint32_t thread_id = GetCurrentThreadId_c();
        msg posted;

        for (;;)
        {
            if (posted_take(thread_id, &posted, wRemoveMsg != 0))
            {
                if (lpMsg != NULL) *((lpmsg)lpMsg) = posted;
                if (wRemoveMsg != 0 && posted.message == WM_PAINT) paint_pending = 0;
                if (getenv(GAME_ENV("MSG_LOG"))) fprintf(stderr, "[msg peek tid=%u hwnd=%x msg=%x wp=%x lp=%x]\n", thread_id, posted.hwnd.get_value(), posted.message, posted.wParam, posted.lParam);
                return 1;
            }

            {
                SDL_Event event;
                int found = find_event(&event, wRemoveMsg, 0);

                if (found == FIND_EVENT_WAKE) continue;
                if (!found) return peek_idle(lpMsg);

                if (lpMsg != NULL)
                {
                    translate_event((lpmsg)lpMsg, &event, wRemoveMsg);
                }
            }

            return 1;
        }
    }

    eprintf("Unsupported method: %s hWnd=%p min=%u max=%u remove=%u\n", "PeekMessageA", hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    exit(1);
}

/* A modal dialog (WinApi-dialog.c) inside the window procedure: the events go
 * to it through find_event, which returns when it has closed or with an event
 * nobody took (that one is dropped: the game does not run meanwhile) */
void Winapi_ModalLoop(int (*done)(void))
{
    while (!done())
    {
        SDL_Event event;

        if (Thread_Exit) Game_StopMain();
        find_event(&event, 1, 1);
    }
}

/* the item of the window menu chosen in WinApi-menubar.c */
void Winapi_PostCommand(uint32_t id)
{
    PostMessageA_c(PSEUDO_HANDLE_MAIN_WINDOW, WM_COMMAND, id & 0xffff, 0);
}

uint32_t CCALL PostMessageA_c(void *hWnd, uint32_t Msg, uint32_t wParam, uint32_t lParam)
{
    msg m;
    uint32_t target;

#ifdef DEBUG_USER32
    eprintf("PostMessageA: 0x%" PRIxPTR ", %i, %i, %i\n", (uintptr_t)hWnd, Msg, wParam, lParam);
#endif

#if !GAME_WINAPI_WINDOW_MESSAGES
    if ((hWnd == PSEUDO_HANDLE_MAIN_WINDOW) && (Msg >= WM_USER) && (Msg < WM_APP))
    {
        // the original posts these messages, but I have no idea what they are for (they're not used by the application)

        Winapi_SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
#endif

    if (Msg == WM_PAINT && hWnd == (void *)PSEUDO_HANDLE_MAIN_WINDOW)
    {
        if (paint_pending) return 1;
        paint_pending = 1;
    }

    target = (hWnd == NULL) ? GetCurrentThreadId_c() : window_thread_id;
    if (target == 0) target = GetCurrentThreadId_c();
    SDL_zero(m);
    m.hwnd.set_value((hWnd == NULL) ? 0 : GAME_PTR_TO_GUEST32(hWnd));
    m.message = Msg;
    m.wParam = wParam;
    m.lParam = (int32_t)lParam;
    m.time = message_time_now();
    m.pt.x = (uint32_t)mouse_x;
    m.pt.y = (uint32_t)mouse_y;

    if (getenv(GAME_ENV("MSG_LOG"))) fprintf(stderr, "[msg post tid=%u hwnd=%x msg=%x wp=%x lp=%x]\n", target, m.hwnd.get_value(), m.message, m.wParam, (uint32_t)m.lParam);
    if (!posted_put(target, &m))
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return 1;
}

uint32_t CCALL PostThreadMessageA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    msg m;
    uint32_t target = (a0 != 0) ? a0 : window_thread_id;
    if (target == 0) target = GetCurrentThreadId_c();

    SDL_zero(m);
    m.hwnd.set_value(0);
    m.message = a1;
    m.wParam = a2;
    m.lParam = (int32_t)a3;
    m.time = message_time_now();
    m.pt.x = (uint32_t)mouse_x;
    m.pt.y = (uint32_t)mouse_y;

    if (getenv(GAME_ENV("MSG_LOG"))) fprintf(stderr, "[msg post-thread tid=%u msg=%x wp=%x lp=%x]\n", target, m.message, m.wParam, (uint32_t)m.lParam);
    if (!posted_put(target, &m))
    {
        Winapi_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return 1;
}

/* WM_QUIT is a message of the thread that pumps the window, in the posted queue: as an SDL event it could
 * be taken by the loop of the port's main thread, which drops it, and the game never left its message loop
 * (a game that writes its settings after the loop would not save them, now and then) */
void CCALL PostQuitMessage_c(int32_t nExitCode)
{
    msg m;
    uint32_t target;

    eprintf("PostQuitMessage: %i\n", nExitCode);

    target = (window_thread_id != 0) ? window_thread_id : GetCurrentThreadId_c();
    SDL_zero(m);
    m.message = WM_QUIT;
    m.wParam = (uint32_t)nExitCode;
    m.time = message_time_now();
    posted_put(target, &m);
}

uint32_t CCALL PtInRect_c(const void *lprc, int32_t px, int32_t py)
{
    if (lprc == NULL) return 0;

#define RC ((const int32_t *)lprc)
    if ((px >= RC[0]) && (px < RC[2]) && (py >= RC[1]) && (py < RC[3])) return 1;

    return 0;
#undef RC
}

uint32_t CCALL RegisterClassA_c(void *lpWndClass)
{
    if (lpWndClass != NULL)
    {
        lpfnWndProc = ((wndclassa *)lpWndClass)->lpfnWndProc;
        class_menu_name = ((wndclassa *)lpWndClass)->lpszMenuName.get_value();
    }

    return 1;
}

void * CCALL SetCursor_c(PTR32(void) hCursor)
{
#ifdef DEBUG_USER32
    eprintf("SetCursor: 0x%" PRIxPTR "\n", (uintptr_t) hCursor);
#endif

    // the original loads and sets cursor only once and doesn't free it, so SDL cursor can be used directly
    if (GET_PTR32_VALUE(hCursor) == 32514)
    {
        SDL_SetCursor(wait_cursor);
    }

    return NULL;
}

uint32_t CCALL SetCursorPos_c(int32_t X, int32_t Y)
{
#ifdef DEBUG_USER32
    eprintf("SetCursorPos: %i, %i\n", X, Y);
#endif

    warp_mouse(X, Y);
    return 1;
}

void * CCALL SetFocus_c(void *hWnd)
{
#ifdef DEBUG_USER32
    eprintf("SetFocus: 0x%" PRIxPTR "\n", (uintptr_t) hWnd);
#endif

    return hWnd;
}

int32_t CCALL ShowCursor_c(uint32_t bShow)
{
#ifdef DEBUG_USER32
    eprintf("ShowCursor: %i\n", bShow);
#endif

    if (bShow)
    {
        cursor_visibility++;
        if (cursor_visibility == 0)
        {
            SDL_ShowCursor(SDL_ENABLE);
        }
    }
    else
    {
        cursor_visibility--;
        if (cursor_visibility == -1)
        {
            SDL_ShowCursor(SDL_DISABLE);
        }
    }

    return cursor_visibility;
}

uint32_t CCALL ShowWindow_c(void */*hWnd*/, int32_t /*nCmdShow*/)
{
#ifdef DEBUG_USER32
    eprintf("ShowWindow: 0x%" PRIxPTR ", %i\n", (uintptr_t)hWnd, nCmdShow);
#endif

    return 0;
}

uint32_t CCALL TranslateMessage_c(void *pMsg)
{
#ifdef DEBUG_USER32
    eprintf("TranslateMessage\n");
#endif

    // As in Windows, WM_KEYDOWN of a character key posts WM_CHAR to the same window
    // (letters by Shift/Caps Lock, digits without Shift, Space).  Keys with Ctrl or Alt
    // give no character.
    lpmsg m = (lpmsg)pMsg;
    if ((m != NULL) && (m->message == WM_KEYDOWN))
    {
        uint32_t vkey = m->wParam;
        SDL_Keymod mod = SDL_GetModState();
        uint32_t ch = 0;

        if (mod & (KMOD_CTRL | KMOD_ALT))
        {
            // no character
        }
        else if ((vkey >= 'A') && (vkey <= 'Z'))
        {
            int upper = ((mod & KMOD_SHIFT) != 0) != ((mod & KMOD_CAPS) != 0);
            ch = upper ? vkey : (vkey + ('a' - 'A'));
        }
        else if (!(mod & KMOD_SHIFT) && (((vkey >= '0') && (vkey <= '9')) || (vkey == VK_SPACE)))
        {
            ch = vkey;
        }

        if (ch != 0) PostMessageA_c(m->hwnd.get_value() ? (void *)GAME_GUEST32_TO_PTR(m->hwnd.get_value()) : NULL, WM_CHAR, ch, m->lParam);
    }

    // A game's PeekMessage loop may exit if TranslateMessage returns 0.
    // Real Windows TranslateMessage returns nonzero for keyboard messages (WM_KEYDOWN -> WM_CHAR).
    // A game may ignore WM_CHAR and still rely on the nonzero return
    // to keep its loop alive. Return 1 to match.
    return 1;
}

uint32_t CCALL UpdateWindow_c(void */*hWnd*/)
{
#ifdef DEBUG_USER32
    eprintf("UpdateWindow: 0x%" PRIxPTR "\n", (uintptr_t)hWnd);
#endif

    return 1;
}

uint32_t CCALL ValidateRect_c(void */*hWnd*/, const void */*lpRect*/)
{
    eprintf("Unimplemented: %s\n", "ValidateRect");
    exit(1);
//    return ValidateRect((HWND)hWnd, (const RECT *)lpRect);
}

uint32_t CCALL WaitMessage_c(void)
{
#ifdef DEBUG_USER32
    eprintf("WaitMessage\n");
#endif

    /* Windows wakes the program up when a message arrives. The port has no
     * message queue of its own: the program is woken up regularly and finds
     * the repaint request (the port's own WM_PAINT) in PeekMessage. */
#ifdef GAME_WINAPI_PUMP_ON_MAIN_THREAD
    wait_queue(33);
    if (Thread_Exit) Game_StopMain();
#else
    while (!SDL_WaitEventTimeout(NULL, 33))
    {
        if (Thread_Exit) Game_StopMain();
        break;
    }
#endif
    return 1;
}

