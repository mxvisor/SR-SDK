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
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#include "WinApi-winmm.h"
#include "Game_memory.h"
#include "Game_defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#else
#include "WinApi-kernel32.h"
#endif

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else

/* From mmsystem.h, not available on this platform */
#endif

#define TIME_ONESHOT   0x0000
#define TIME_PERIODIC  0x0001

#ifndef _WIN32
unsigned int CCALL Winapi_GetTicks(void);
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

/* Multimedia timer support.
 *
 * The recompiled guest calls timeSetEvent() from its main thread and expects a
 * stdcall TIMECALLBACK to be invoked periodically (fuEvent == TIME_PERIODIC) or
 * once (fuEvent == TIME_ONESHOT).  That callback must run on the guest CPU
 * thread, so the SDL timer only posts a SDL_USEREVENT (code 6); the window
 * message path (translate_event() in WinApi-user32.c) dispatches it through
 * Winapi_CallGuest().  Only one timer is used by the game. */

struct winapi_timer {
    SDL_TimerID sdl_id;
    void *callback;
    uint32_t interval;
    uint32_t event;
    void *user;
};

static struct winapi_timer winapi_timer_ = { 0, NULL, 0, 0, NULL };

/* WINAPI_TIMER_LOG=1 logs the timer calls */
static int timer_log(void)
{
    static int on = -1;

    if (on < 0) on = (getenv("WINAPI_TIMER_LOG") != NULL);
    return on;
}

static uint32_t SDLCALL winapi_timer_callback(uint32_t interval, void *userdata)
{
    struct winapi_timer *timer = (struct winapi_timer *)userdata;
    SDL_Event event;
    static unsigned fired;

    if (timer_log() && (fired++ < 5)) eprintf("timer fires: proc %p\n", timer->callback);

    SDL_zero(event);
    event.type = SDL_USEREVENT;
    event.user.code = EC_WINAPI_TIMER;
    event.user.data1 = (void *)(uintptr_t)GAME_PTR_TO_GUEST32(timer->callback);
    event.user.data2 = timer->user;
    SDL_PushEvent(&event);

    return (timer->event & TIME_PERIODIC) ? interval : 0;
}


/* kernel32's GetTickCount, here beside timeGetTime: milliseconds since the
 * start; games time fades and frames with it */
uint32_t CCALL GetTickCount_c(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    return Winapi_GetTicks();
#endif
}

uint32_t CCALL timeGetTime_c(void)
{
#ifdef DEBUG_WINMM
    eprintf("timeGetTime\n");
#endif

#ifdef _WIN32
    return GetTickCount();
#else
    return Winapi_GetTicks();
#endif
}

uint32_t CCALL timeSetEvent_c(uint32_t delay, uint32_t resolution,
    void *lpTimeProc, void *dwUser, uint32_t fuEvent)
{
    (void)resolution;

    winapi_timer_.callback = lpTimeProc;
    winapi_timer_.user = dwUser;
    winapi_timer_.interval = delay ? delay : 1;
    winapi_timer_.event = fuEvent;

    winapi_timer_.sdl_id = SDL_AddTimer(winapi_timer_.interval, winapi_timer_callback, &winapi_timer_);
    if (timer_log()) eprintf("timeSetEvent: delay %u proc %p event %u -> %u\n", delay, lpTimeProc, fuEvent, (unsigned)winapi_timer_.sdl_id);
    if (!winapi_timer_.sdl_id)
    {
        return 0;
    }

    /* Windows returns a timer identifier that is later passed to
       timeKillEvent(); the game only uses one timer. */
    return 1;
}

uint32_t CCALL timeKillEvent_c(uint32_t id)
{
    if (timer_log()) eprintf("timeKillEvent: id %u (sdl %u)\n", id, (unsigned)winapi_timer_.sdl_id);

    if (winapi_timer_.sdl_id)
    {
        SDL_RemoveTimer(winapi_timer_.sdl_id);
        winapi_timer_.sdl_id = 0;
    }

    return 0;
}

uint32_t CCALL timeBeginPeriod_c(uint32_t period_ms)
{
    (void)period_ms;
    return 0;
}

uint32_t CCALL timeEndPeriod_c(uint32_t period_ms)
{
    (void)period_ms;
    return 0;
}

uint32_t CCALL timeGetDevCaps_c(void *ptc, uint32_t cbtc)
{
    (void)cbtc;
    TIMECAPS_c *tc = (TIMECAPS_c *)ptc;
    tc->wPeriodMin = 1;
    tc->wPeriodMax = 1000000;
    return 0;
}
/* Joysticks: none is attached.  A success code would make a game read the
 * JOYCAPS/JOYINFOEX it passed in, which nobody filled (a game took the
 * stack garbage in dwButtons for a pressed button and skipped every movie). */

#define JOYERR_PARMS_c     165
#define JOYERR_UNPLUGGED_c 167

uint32_t CCALL joyGetNumDevs_c(void)
{
    return 0;
}

uint32_t CCALL joyGetDevCapsA_c(uint32_t id, uint32_t pjc, uint32_t cbjc)
{
    (void)id; (void)pjc; (void)cbjc;
    return JOYERR_PARMS_c;
}

uint32_t CCALL joyGetPosEx_c(uint32_t id, uint32_t pji)
{
    (void)id; (void)pji;
    return JOYERR_UNPLUGGED_c;
}

/* (a game that polls it every tick walked its hero by itself on the JOYINFO
 * that a success left unwritten) */
uint32_t CCALL joyGetPos_c(uint32_t id, uint32_t pji)
{
    (void)id; (void)pji;
    return JOYERR_UNPLUGGED_c;
}

uint32_t CCALL joyConfigChanged_c(uint32_t flags)
{
    (void)flags;
    return 0;
}

/* One MIDI and one wave output device: a game that counts them keeps its music
 * and sound on (it plays them through its own drivers or the layer's stubs) */
uint32_t CCALL midiOutGetNumDevs_c(void)
{
    return 1;
}

uint32_t CCALL waveOutGetNumDevs_c(void)
{
    return 1;
}
