/**
 *
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

/* The Miles Sound System (AIL 3.x) a DOS4GW game was linked with: the entry
 * points its external_procedures.sci routes to llasm/ail.llinc, over the
 * SDK's AIL (core/Game_ail.c).
 *
 * The real AIL talks to a 16-bit real-mode .DIG driver through INT 66h,
 * which nothing here can run; SDL_mixer plays the samples instead.  An
 * HSAMPLE is a block of the guest's memory (a game may keep it in a dword
 * table) holding the number of the sample in Game_ail's pool.
 *
 * AIL's timer service matters as much as the sound: a game may run its
 * frame tick as an AIL timer, and read AIL's current PIT period out of AIL's
 * own data to reprogram the PIT itself.  AIL owns INT 8 while its timers
 * run: the game's GAME_DOS_TIMER_TICK() calls Game_AIL_TimerInterrupt, then
 * the guest's INT 8 (the BIOS chain).  The hooks of Game_hooks.h:
 *   GAME_DOS_AIL_PERIOD_POINTER()  where the game reads AIL's period (a dword,
 *                                  microseconds), kept current
 *   GAME_DOS_AIL_SAMPLE_NAME(addr) the name of the sound at a guest address,
 *                                  for the trace ($<GAME>_TRACE_SOUND)
 *   GAME_DOS_AIL_SERVICE_US        the period of the DIG driver's own service
 *                                  timer (5000 us: SB16.DIG)
 * $<GAME>_NOSOUND=1 (0) turns the digital sound off (on) over the config
 * file's Sound. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"
#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_ail.h"
#include "Game_dosint.h"
#include "Game_dospit.h"
#include "Game_dosail.h"

#ifndef GAME_DOS_AIL_SERVICE_US
#define GAME_DOS_AIL_SERVICE_US 5000u
#endif

/* AIL's timer table: 16 slots, the last one reserved for chaining to the
 * BIOS handler (AIL's ISR only calls back slots 0-14) */
#define AIL_TIMERS          16
#define AIL_USER_TIMERS     15
#define AIL_BIOS_TIMER_US   54925u
#define AILT_FREE           0u
#define AILT_STOPPED        1u
#define AILT_RUNNING        2u

#define HSAMPLE_MAGIC       0x4c504d53u     /* "SMPL" */

typedef struct {
    uint32_t status;
    uint32_t callback;  /* guest code address; 0 for the driver's service timer */
    uint32_t user;
    uint32_t period;    /* microseconds */
    uint32_t elapsed;
    uint32_t pending;
} ail_timer;

typedef struct {
    uint32_t magic;
    int32_t sample;     /* Game_ail's */
} ail_hsample;

static ail_timer timers[AIL_TIMERS];
static uint32_t timer_period_us = AIL_BIOS_TIMER_US;
static int timers_running;

static int trace_sound(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_SOUND")) != NULL;
    return enabled;
}

static int sample_of(void *S)
{
    const ail_hsample *h = (const ail_hsample *)S;

    return (h != NULL && h->magic == HSAMPLE_MAGIC) ? h->sample : -1;
}

/* ---- timers ---- */

static void publish_period(void)
{
#ifdef GAME_DOS_AIL_PERIOD_POINTER
    memcpy(GAME_DOS_AIL_PERIOD_POINTER(), &timer_period_us, sizeof(timer_period_us));
#endif
}

/* AIL's microseconds to a PIT reload value */
static uint32_t pit_reload_for(uint32_t microseconds)
{
    if (microseconds >= 0xD68Du) return 0;
    return (uint32_t)(((uint64_t)microseconds * 10000u) / 8380u);
}

/* the PIT runs at the shortest period of any registered timer, the BIOS
 * chain slot included */
static void update_timer_period(void)
{
    uint32_t period = 0xFFFFFFFFu;

    for (int index = 0; index < AIL_TIMERS; index++) {
        if (timers[index].status != AILT_FREE && timers[index].period != 0 && timers[index].period < period) {
            period = timers[index].period;
        }
    }
    if (period == timer_period_us) return;
    timer_period_us = period;
    publish_period();
    Game_DosPitProgram(pit_reload_for(period));
    if (trace_sound()) fprintf(stderr, "SOUND timer period %u us\n", period);
}

static void reset_timers(void)
{
    memset(timers, 0, sizeof(timers));
    timers[AIL_USER_TIMERS].status = AILT_RUNNING;
    timers[AIL_USER_TIMERS].period = AIL_BIOS_TIMER_US;
    timer_period_us = AIL_BIOS_TIMER_US;
    timers_running = 0;
}

static int timer_valid(int32_t timer)
{
    return timer >= 0 && timer < AIL_USER_TIMERS && timers[timer].status != AILT_FREE;
}

/* AIL's ISR, minus the BIOS chain: every running timer gains the programmed
 * period, and one that reaches its own period is called back once */
void Game_AIL_TimerInterrupt(void)
{
    /* an interrupt that cannot be taken now is not counted either: AIL's ISR
     * returns at once when it is re-entered */
    if (!timers_running || !Game_DosGuestInterruptsEnabled()) return;
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        ail_timer *timer = &timers[index];
        if (timer->status != AILT_RUNNING) continue;
        timer->elapsed += timer_period_us;
        if (timer->elapsed >= timer->period) {
            timer->elapsed -= timer->period;
            timer->pending++;
        }
    }
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        ail_timer *timer = &timers[index];
        if (timer->callback == 0) {
            timer->pending = 0;
            continue;
        }
        while (timer->pending != 0) {
            if (!Game_DosCallGuestTimer(timer->callback, timer->user)) return;
            timer->pending--;
        }
    }
}

void Game_AIL_startup(void)
{
    if (trace_sound()) fprintf(stderr, "SOUND AIL_startup\n");
    reset_timers();
    publish_period();
}

void Game_AIL_shutdown(void)
{
    if (trace_sound()) fprintf(stderr, "SOUND AIL_shutdown\n");
    Game_AilShutdown();
    /* AIL restores the BIOS rate along with its INT 8 vector */
    if (timer_period_us != AIL_BIOS_TIMER_US) Game_DosPitProgram(0);
    reset_timers();
}

int32_t Game_AIL_set_preference(uint32_t number, int32_t value)
{
    return Game_AilSetPreference(number, value);
}

int32_t Game_AIL_register_timer(uint32_t callback)
{
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        if (timers[index].status == AILT_FREE) {
            memset(&timers[index], 0, sizeof(timers[index]));
            timers[index].status = AILT_STOPPED;
            timers[index].callback = callback;
            if (trace_sound()) fprintf(stderr, "SOUND AIL_register_timer(%08x) = %d\n", callback, index);
            return index;
        }
    }
    return -1;
}

void Game_AIL_set_timer_period(int32_t timer, uint32_t microseconds)
{
    if (!timer_valid(timer)) return;
    timers[timer].period = microseconds;
    timers[timer].elapsed = 0;
    update_timer_period();
}

void Game_AIL_start_timer(int32_t timer)
{
    if (!timer_valid(timer)) return;
    timers[timer].status = AILT_RUNNING;
    timers_running = 1;
}

void Game_AIL_stop_timer(int32_t timer)
{
    if (timer_valid(timer)) timers[timer].status = AILT_STOPPED;
}

void Game_AIL_release_timer_handle(int32_t timer)
{
    if (!timer_valid(timer)) return;
    memset(&timers[timer], 0, sizeof(timers[timer]));
    update_timer_period();
}

/* ---- the digital driver and its samples ---- */

int32_t Game_AIL_install_DIG_INI(uint32_t *dig)
{
    const char *nosound = getenv(GAME_ENV("NOSOUND"));
    int sound_enabled = (nosound != NULL && nosound[0] != 0) ? nosound[0] == '0' : Game_Sound != 0;
    void *driver;

    if (trace_sound()) {
        fprintf(stderr, "SOUND device %d Hz format %04x channels %u, digital sound %s\n",
                Game_AudioRate, Game_AudioFormat, Game_AudioChannels, sound_enabled ? "on" : "off");
    }
    if (!Game_AudioIsOpen() || !sound_enabled) {
        if (trace_sound()) fprintf(stderr, "SOUND AIL_install_DIG_INI: no device\n");
        return 1;   /* AIL_NO_INI_FILE: the game takes its no-sound branch */
    }
    /* the game only hands it back to AIL_allocate_sample_handle */
    driver = x86_malloc(256);
    if (driver == NULL) return 1;
    memset(driver, 0, 256);
    *dig = GAME_PTR_TO_GUEST32(driver);
    /* the driver's own service timer: the PIT's period with it */
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        if (timers[index].status == AILT_FREE) {
            memset(&timers[index], 0, sizeof(timers[index]));
            timers[index].status = AILT_RUNNING;
            timers[index].period = GAME_DOS_AIL_SERVICE_US;
            timers_running = 1;
            update_timer_period();
            break;
        }
    }
    if (trace_sound()) fprintf(stderr, "SOUND AIL_install_DIG_INI = %p\n", driver);
    return 0;
}

void *Game_AIL_allocate_sample_handle(void *dig)
{
    ail_hsample *h;
    int sample;

    (void)dig;
    if (!Game_AudioIsOpen()) return NULL;
    h = (ail_hsample *)x86_malloc(sizeof(ail_hsample));
    if (h == NULL) return NULL;
    sample = Game_AilAllocateSample();
    if (sample < 0) {
        x86_free(h);
        return NULL;
    }
    h->magic = HSAMPLE_MAGIC;
    h->sample = sample;
    return h;
}

void Game_AIL_release_sample_handle(void *S)
{
    int sample = sample_of(S);

    if (sample < 0) return;
    Game_AilReleaseSample(sample);
    ((ail_hsample *)S)->magic = 0;
    x86_free(S);
}

/* like AIL's own, every sample function ignores a NULL handle */
void Game_AIL_init_sample(void *S)
{
    Game_AilInitSample(sample_of(S));
}

void Game_AIL_set_sample_address(void *S, void *start, uint32_t len)
{
    Game_AilSetSampleAddress(sample_of(S), start, len);
}

void Game_AIL_set_sample_type(void *S, int32_t format, uint32_t flags)
{
    Game_AilSetSampleType(sample_of(S), format, flags);
}

void Game_AIL_set_sample_playback_rate(void *S, int32_t playback_rate)
{
    Game_AilSetSampleRate(sample_of(S), playback_rate);
}

void Game_AIL_set_sample_volume(void *S, int32_t volume)
{
    Game_AilSetSampleVolume(sample_of(S), volume);
}

void Game_AIL_set_sample_pan(void *S, int32_t pan)
{
    Game_AilSetSamplePan(sample_of(S), pan);
}

void Game_AIL_set_sample_loop_count(void *S, int32_t loop_count)
{
    Game_AilSetSampleLoopCount(sample_of(S), loop_count);
}

void Game_AIL_start_sample(void *S)
{
    int sample = sample_of(S);

    Game_AilStartSample(sample);
    if (trace_sound() && sample >= 0) {
        const char *name = NULL;
#ifdef GAME_DOS_AIL_SAMPLE_NAME
        const void *start = Game_AilSampleData(sample);
        if (start != NULL) name = GAME_DOS_AIL_SAMPLE_NAME(GAME_PTR_TO_GUEST32(start));
#endif
        fprintf(stderr, "SOUND start sample %d %.20s status %u\n", sample, name != NULL ? name : "?",
                Game_AilSampleStatus(sample));
    }
}

/* AIL_stop_sample pauses; AIL_resume_sample goes on from there */
void Game_AIL_stop_sample(void *S)
{
    Game_AilStopSample(sample_of(S));
}

void Game_AIL_resume_sample(void *S)
{
    Game_AilResumeSample(sample_of(S));
}

void Game_AIL_end_sample(void *S)
{
    Game_AilEndSample(sample_of(S));
}

uint32_t Game_AIL_sample_status(void *S)
{
    int sample = sample_of(S);

    return (sample < 0) ? 0 : Game_AilSampleStatus(sample);
}
