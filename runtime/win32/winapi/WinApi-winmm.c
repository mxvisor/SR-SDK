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
#include "Game_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <string.h>
#include "Game_sound.h"
#include "Game_vfs.h"
#include "Game_pad.h"
#include "WinApi-user32.h"

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

#ifdef __cplusplus
extern "C"
#endif
void Winapi_InputPut(const SDL_Event *event);

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
    /* straight into the input queue of the window's thread (WinApi-user32.c) */
    Winapi_InputPut(&event);

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
/* Joysticks: the gamepads of the host (Game_pad.h), as Windows shows an XInput
 * pad through winmm.  Joystick id N is the Nth pad there is (one pad is
 * always id 0, whatever slot it took).  The axes go 0..65535 with the centre
 * at 32767 and Y down: X/Y the left stick, Z the triggers (the left one up,
 * the right one down), R/U the right stick's Y/X.  The buttons: 1 A, 2 B,
 * 3 X, 4 Y, 5 LB, 6 RB, 7 Back, 8 Start, 9 and 10 the sticks' clicks.  The
 * D-pad is the POV for a game that asks for it (JOY_RETURNPOV, ...POVCTS);
 * for any other (joyGetPos has no POV) it moves the X/Y axes, so that a game
 * that reads the stick only can be played on the D-pad.  The dead zones are
 * those of Game_PadGetState, none with JOY_RETURNRAWDATA.  No pad (or
 * Gamepad=off): every id is unplugged, as it was before the pads.
 * <GAME>_DEBUG_PAD writes what a game reads whenever it changes. */

#define JOYERR_NOERROR_c   0
#define JOYERR_PARMS_c     165
#define JOYERR_NOCANDO_c   166
#define JOYERR_UNPLUGGED_c 167

#define JOY_RETURNPOV_c     0x0040
#define JOY_RETURNRAWDATA_c 0x0100
#define JOY_RETURNPOVCTS_c  0x0200
#define JOY_POVCENTERED_c   0xffff

#define JOYCAPS_HASZ_c    0x0001
#define JOYCAPS_HASR_c    0x0002
#define JOYCAPS_HASU_c    0x0004
#define JOYCAPS_HASPOV_c  0x0010
#define JOYCAPS_POVCTS_c  0x0040

#define MM_JOY1MOVE_c       0x3a0
#define MM_JOY1ZMOVE_c      0x3a2
#define MM_JOY1BUTTONDOWN_c 0x3b5
#define MM_JOY1BUTTONUP_c   0x3b7

#define WINMM_JOY_BUTTONS 10

#pragma pack(push, 1)
typedef struct {
    uint16_t wMid, wPid;
    char szPname[32];
    uint32_t wXmin, wXmax, wYmin, wYmax, wZmin, wZmax;
    uint32_t wNumButtons, wPeriodMin, wPeriodMax;
    uint32_t wRmin, wRmax, wUmin, wUmax, wVmin, wVmax;
    uint32_t wCaps, wMaxAxes, wNumAxes, wMaxButtons;
    char szRegKey[32];
    char szOEMVxD[260];
} JOYCAPSA_c;

typedef struct {
    uint32_t wXpos, wYpos, wZpos, wButtons;
} JOYINFO_c;

typedef struct {
    uint32_t dwSize, dwFlags;
    uint32_t dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos;
    uint32_t dwButtons, dwButtonNumber, dwPOV;
    uint32_t dwReserved1, dwReserved2;
} JOYINFOEX_c;
#pragma pack(pop)

typedef struct {
    uint32_t x, y, z, r, u, v;
    uint32_t buttons, count, pov;
} winmm_joy;

/* the slot of joystick id `id`, -1 when there is no such pad */
static int winmm_joy_slot(uint32_t id)
{
    unsigned int slots = Game_PadSlots();
    int slot;

    for (slot = 0; slot < GAME_PAD_MAX; slot++)
    {
        if ((slots & (1u << slot)) == 0) continue;
        if (id == 0) return slot;
        id--;
    }
    return -1;
}

/* a stick's -32768..32767 (Y up) as an axis 0..65535 with the centre at 32767
 * (`flip`: Y down) */
static uint32_t winmm_joy_axis(int value, int flip)
{
    value = flip ? 32767 - value : 32767 + value;
    return (value < 0) ? 0 : (uint32_t)value;
}

static int winmm_joy_read(uint32_t id, int raw, int pov, winmm_joy *joy)
{
    static const uint16_t order[WINMM_JOY_BUTTONS] = {
        GAME_PAD_A, GAME_PAD_B, GAME_PAD_X, GAME_PAD_Y,
        GAME_PAD_LEFT_SHOULDER, GAME_PAD_RIGHT_SHOULDER, GAME_PAD_BACK, GAME_PAD_START,
        GAME_PAD_LEFT_THUMB, GAME_PAD_RIGHT_THUMB
    };
    Game_PadState state;
    int slot = winmm_joy_slot(id);
    int index, up, down, left, right;

    if (slot < 0) return 0;
    if (!(raw ? Game_PadGetRawState(slot, &state) : Game_PadGetState(slot, &state))) return 0;

    joy->x = winmm_joy_axis(state.left_x, 0);
    joy->y = winmm_joy_axis(state.left_y, 1);
    joy->z = (uint32_t)(32767 + (state.left_trigger - state.right_trigger) * 128);
    if (joy->z > 65535) joy->z = 65535;
    joy->r = winmm_joy_axis(state.right_y, 1);
    joy->u = winmm_joy_axis(state.right_x, 0);
    joy->v = 32767;

    joy->buttons = 0;
    joy->count = 0;
    for (index = 0; index < WINMM_JOY_BUTTONS; index++)
    {
        if (state.buttons & order[index])
        {
            joy->buttons |= 1u << index;
            joy->count++;
        }
    }

    up = (state.buttons & GAME_PAD_DPAD_UP) != 0;
    down = (state.buttons & GAME_PAD_DPAD_DOWN) != 0;
    left = (state.buttons & GAME_PAD_DPAD_LEFT) != 0;
    right = (state.buttons & GAME_PAD_DPAD_RIGHT) != 0;
    if (up && down) up = down = 0;
    if (left && right) left = right = 0;

    joy->pov = JOY_POVCENTERED_c;
    if (pov)
    {
        if (up) joy->pov = right ? 4500 : (left ? 31500 : 0);
        else if (down) joy->pov = right ? 13500 : (left ? 22500 : 18000);
        else if (right) joy->pov = 9000;
        else if (left) joy->pov = 27000;
    }
    else
    {
        /* the D-pad on the stick: it goes to the edge where it is pressed */
        if (left) joy->x = 0;
        if (right) joy->x = 65535;
        if (up) joy->y = 0;
        if (down) joy->y = 65535;
    }

    if (getenv(GAME_ENV("DEBUG_PAD")) != NULL && id < GAME_PAD_MAX)
    {
        /* what the game reads, when it changes */
        static winmm_joy seen[GAME_PAD_MAX];
        if (memcmp(&seen[id], joy, sizeof(*joy)) != 0)
        {
            seen[id] = *joy;
            fprintf(stderr, "winmm: joy %u x=%u y=%u z=%u r=%u u=%u buttons=0x%x pov=%u\n", id, joy->x, joy->y, joy->z, joy->r, joy->u, joy->buttons, joy->pov);
        }
    }
    return 1;
}

uint32_t CCALL joyGetNumDevs_c(void)
{
    /* the ids a game may try, pads or not (Windows says 16) */
    return GAME_PAD_MAX;
}

uint32_t CCALL joyGetDevCapsA_c(uint32_t id, uint32_t pjc, uint32_t cbjc)
{
    JOYCAPSA_c caps;
    int slot;

    if (pjc == 0) return JOYERR_PARMS_c;
    slot = winmm_joy_slot(id);
    if (slot < 0) return JOYERR_PARMS_c;

    memset(&caps, 0, sizeof(caps));
    caps.wMid = 0x045e;         /* as Windows names an XInput pad */
    caps.wPid = 0x028e;
    snprintf(caps.szPname, sizeof(caps.szPname), "%s", Game_PadName(slot));
    caps.wXmax = caps.wYmax = caps.wZmax = 65535;
    caps.wRmax = caps.wUmax = caps.wVmax = 65535;
    caps.wNumButtons = WINMM_JOY_BUTTONS;
    caps.wMaxButtons = 32;
    caps.wPeriodMin = 10;
    caps.wPeriodMax = 1000;
    caps.wCaps = JOYCAPS_HASZ_c | JOYCAPS_HASR_c | JOYCAPS_HASU_c | JOYCAPS_HASPOV_c | JOYCAPS_POVCTS_c;
    caps.wNumAxes = 5;
    caps.wMaxAxes = 6;

    memcpy(GUEST_MEM(pjc), &caps, (cbjc < sizeof(caps)) ? cbjc : sizeof(caps));
    return JOYERR_NOERROR_c;
}

uint32_t CCALL joyGetPosEx_c(uint32_t id, uint32_t pji)
{
    JOYINFOEX_c *info;
    winmm_joy joy;

    if (pji == 0) return JOYERR_PARMS_c;
    info = (JOYINFOEX_c *)GUEST_MEM(pji);
    if (info->dwSize < sizeof(JOYINFOEX_c)) return JOYERR_PARMS_c;
    if (!winmm_joy_read(id, (info->dwFlags & JOY_RETURNRAWDATA_c) != 0,
                        (info->dwFlags & (JOY_RETURNPOV_c | JOY_RETURNPOVCTS_c)) != 0, &joy))
    {
        return JOYERR_UNPLUGGED_c;
    }

    info->dwXpos = joy.x;
    info->dwYpos = joy.y;
    info->dwZpos = joy.z;
    info->dwRpos = joy.r;
    info->dwUpos = joy.u;
    info->dwVpos = joy.v;
    info->dwButtons = joy.buttons;
    info->dwButtonNumber = joy.count;
    info->dwPOV = joy.pov;
    return JOYERR_NOERROR_c;
}

/* (a game that polls it every tick walked its hero by itself on the JOYINFO
 * that a success left unwritten: it is written whenever it succeeds) */
uint32_t CCALL joyGetPos_c(uint32_t id, uint32_t pji)
{
    JOYINFO_c *info;
    winmm_joy joy;

    if (pji == 0) return JOYERR_PARMS_c;
    if (!winmm_joy_read(id, 0, 0, &joy)) return JOYERR_UNPLUGGED_c;

    info = (JOYINFO_c *)GUEST_MEM(pji);
    info->wXpos = joy.x;
    info->wYpos = joy.y;
    info->wZpos = joy.z;
    info->wButtons = joy.buttons & 0x0f;
    return JOYERR_NOERROR_c;
}

/* the pads are read anew at every call: there is nothing to reload */
uint32_t CCALL joyConfigChanged_c(uint32_t flags)
{
    (void)flags;
    return JOYERR_NOERROR_c;
}

/* joySetCapture: the window gets MM_JOY1MOVE/MM_JOY1ZMOVE (wParam the
 * buttons 1-4, lParam X and Y; ZMOVE: Z) and MM_JOY1BUTTONDOWN/UP (the
 * buttons and, in the high byte, the ones that changed), MM_JOY2* for id 1.
 * An SDL timer reads the pad every period and posts the messages to the
 * window's thread.  A move is posted when the joystick moved, also without
 * fChanged: one every period would only fill the queue of a game that does
 * not take them. */
#define WINMM_JOY_CAPTURES 2

struct winmm_capture {
    SDL_TimerID timer;
    uint32_t hwnd;
    uint32_t id;
    uint32_t period;
    int first;
    winmm_joy last;
};

static struct winmm_capture winmm_captures[WINMM_JOY_CAPTURES];

static Uint32 SDLCALL winmm_capture_tick(Uint32 interval, void *param)
{
    struct winmm_capture *capture = (struct winmm_capture *)param;
    void *hwnd = (void *)GAME_GUEST32_TO_PTR(capture->hwnd);
    winmm_joy joy;
    uint32_t pressed, changed;

    if (!winmm_joy_read(capture->id, 0, 0, &joy)) return interval;

    pressed = joy.buttons & 0x0f;
    changed = capture->first ? 0 : (pressed ^ (capture->last.buttons & 0x0f));
    if (changed & pressed)
    {
        PostMessageA_c(hwnd, MM_JOY1BUTTONDOWN_c + capture->id, pressed | ((changed & pressed) << 8), (joy.x & 0xffff) | (joy.y << 16));
    }
    if (changed & ~pressed)
    {
        PostMessageA_c(hwnd, MM_JOY1BUTTONUP_c + capture->id, pressed | ((changed & ~pressed) << 8), (joy.x & 0xffff) | (joy.y << 16));
    }
    if (capture->first || joy.x != capture->last.x || joy.y != capture->last.y)
    {
        PostMessageA_c(hwnd, MM_JOY1MOVE_c + capture->id, pressed, (joy.x & 0xffff) | (joy.y << 16));
    }
    if (capture->first || joy.z != capture->last.z)
    {
        PostMessageA_c(hwnd, MM_JOY1ZMOVE_c + capture->id, pressed, joy.z & 0xffff);
    }
    capture->last = joy;
    capture->first = 0;
    return interval;
}

uint32_t CCALL joySetCapture_c(uint32_t hwnd, uint32_t id, uint32_t period, uint32_t changed)
{
    struct winmm_capture *capture;

    (void)changed;
    if (hwnd == 0 || id >= WINMM_JOY_CAPTURES) return JOYERR_PARMS_c;
    if (winmm_joy_slot(id) < 0) return JOYERR_UNPLUGGED_c;
    capture = &winmm_captures[id];
    if (capture->timer != 0) return JOYERR_NOCANDO_c;

    if (period < 10) period = 10;
    if (period > 1000) period = 1000;
    capture->hwnd = hwnd;
    capture->id = id;
    capture->period = period;
    capture->first = 1;
    capture->timer = SDL_AddTimer(period, winmm_capture_tick, capture);
    return (capture->timer != 0) ? JOYERR_NOERROR_c : JOYERR_NOCANDO_c;
}

uint32_t CCALL joyReleaseCapture_c(uint32_t id)
{
    if (id >= WINMM_JOY_CAPTURES) return JOYERR_PARMS_c;
    if (winmm_captures[id].timer != 0)
    {
        SDL_RemoveTimer(winmm_captures[id].timer);
        winmm_captures[id].timer = 0;
    }
    return JOYERR_NOERROR_c;
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

/* msacm32, the Audio Compression Manager: no codec here.  acmMetrics answers
 * the size of the largest format (a program allocates its WAVEFORMATEX by it,
 * then fills it: a 0 would leave it no room) and counts no drivers. */
#define ACM_METRIC_MAX_SIZE_FORMAT_c    50
#define ACM_METRIC_MAX_SIZE_FILTER_c    51

uint32_t CCALL acmMetrics_c(uint32_t hao, uint32_t uMetric, uint32_t pMetric)
{
    (void)hao;
    if (pMetric == 0) return 11;                        /* MMSYSERR_INVALPARAM */
    switch (uMetric)
    {
        case ACM_METRIC_MAX_SIZE_FORMAT_c:
            *(uint32_t *)GUEST_MEM(pMetric) = 50;       /* an ADPCMWAVEFORMAT with its 7 coefficients */
            break;
        case ACM_METRIC_MAX_SIZE_FILTER_c:
            *(uint32_t *)GUEST_MEM(pMetric) = 32;       /* WAVEFILTER */
            break;
        default:
            *(uint32_t *)GUEST_MEM(pMetric) = 0;        /* the counts of drivers and codecs */
            break;
    }
    return 0;
}

/* sndPlaySound: one sound at a time, a file of the game or a WAV image in
 * memory (SND_MEMORY), on a channel of the port's SDL_mixer device */
#define SND_ASYNC_c     0x0001
#define SND_MEMORY_c    0x0004
#define SND_LOOP_c      0x0008
#define SND_NOSTOP_c    0x0010

static Mix_Chunk *snd_chunk;
static int snd_channel = -1;

static void snd_stop(void)
{
    if (snd_channel >= 0 && Mix_GetChunk(snd_channel) == snd_chunk) Mix_HaltChannel(snd_channel);
    snd_channel = -1;
    if (snd_chunk != NULL) Mix_FreeChunk(snd_chunk);
    snd_chunk = NULL;
}

uint32_t CCALL sndPlaySoundA_c(uint32_t lpszSound, uint32_t fuSound)
{
    SDL_RWops *rw = NULL;

    if (!Game_AudioIsOpen()) return lpszSound == 0;
    if (snd_channel >= 0 && Mix_Playing(snd_channel) && Mix_GetChunk(snd_channel) == snd_chunk)
    {
        if (fuSound & SND_NOSTOP_c) return 0;
    }
    snd_stop();
    if (lpszSound == 0) return 1;

    if (fuSound & SND_MEMORY_c)
    {
        const uint8_t *wav = (const uint8_t *)GUEST_MEM(lpszSound);
        uint32_t size;

        if (memcmp(wav, "RIFF", 4) != 0) return 0;
        memcpy(&size, wav + 4, 4);
        rw = SDL_RWFromConstMem(wav, (int)size + 8);
    }
    else
    {
        char path[1024];

        if (Game_ResolveDataPath((const char *)GUEST_MEM(lpszSound), path, sizeof(path))) rw = SDL_RWFromFile(path, "rb");
    }
    if (rw == NULL) return 0;
    snd_chunk = Mix_LoadWAV_RW(rw, 1);
    if (snd_chunk == NULL) return 0;
    snd_channel = Mix_PlayChannel(-1, snd_chunk, (fuSound & SND_LOOP_c) ? -1 : 0);
    if (snd_channel < 0) { snd_stop(); return 0; }
    if (!(fuSound & (SND_ASYNC_c | SND_LOOP_c)))
    {
        while (Mix_Playing(snd_channel) && Mix_GetChunk(snd_channel) == snd_chunk) SDL_Delay(5);
    }
    return 1;
}
