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


/* An OPL2 on SDL_mixer's music: see Game_opl.h. */

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_opl.h"
#include "emu8950.h"

/* the Ad Lib's crystal: the chip makes a sample every 72 clocks, 49716 Hz */
#define OPL_CLOCK 3579545
#define OPL_YM3812 2

static OPL *chip;
static SDL_mutex *opl_lock;
static Game_OplServe opl_serve;
static void *opl_user;
static unsigned int opl_rate;
static unsigned int opl_phase;     /* the time to the next service, in 1/opl_rate of an output frame */
/* Game_OplDcBlock: the high-pass of the card's output capacitor */
static int opl_dc_block;
static float opl_dc_coefficient;
static float opl_dc_in, opl_dc_out;

/* Game_OplWriteTimed: the writes of another thread, with their time.  A
 * buffer of the output is the time from the call before to this one: each
 * write goes in at the sample of its time in it, one buffer later, so the
 * writes keep the distances the driver gave them (a note off and on again a
 * tick later), not the steps of the buffers. */
#define OPL_QUEUE 4096u
typedef struct {
    uint64_t time;
    uint8_t reg;
    uint8_t value;
} opl_timed;
static opl_timed opl_queue[OPL_QUEUE];
static unsigned int opl_queue_read, opl_queue_count;
static uint64_t opl_window_end;    /* the time of the call before, 0: none */

static void opl_mix(void *udata, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)(sizeof(Sint16) * Game_AudioChannels);

    const int total = frames;
    const uint64_t now = SDL_GetPerformanceCounter();
    uint64_t window_start, window_length;

    (void)udata;
    SDL_LockMutex(opl_lock);
    window_start = opl_window_end != 0 ? opl_window_end
                 : now - (uint64_t)total * SDL_GetPerformanceFrequency() / (uint64_t)Game_AudioRate;
    window_length = now > window_start ? now - window_start : 1u;
    opl_window_end = now;
    while (frames-- > 0) {
        Sint16 sample;
        int level;
        unsigned int channel;
        const uint64_t time = window_start + window_length * (uint64_t)(total - frames) / (uint64_t)total;

        while (opl_queue_count != 0 && opl_queue[opl_queue_read].time <= time) {
            OPL_writeReg(chip, opl_queue[opl_queue_read].reg, opl_queue[opl_queue_read].value);
            opl_queue_read = (opl_queue_read + 1u) % OPL_QUEUE;
            opl_queue_count--;
        }

        opl_phase += opl_rate;
        if (opl_phase >= (unsigned int)Game_AudioRate) {
            opl_phase -= (unsigned int)Game_AudioRate;
            if (opl_serve != NULL) opl_serve(opl_user);
        }
        /* 1.5 times emu8950's output: the chip's level beside the Sound
         * Blaster's DAC (Game_dosail2.c), as DOSBox Staging measures them */
        level = OPL_calc(chip) * 3 / 2;
        if (opl_dc_block) {
            opl_dc_out = (float)level - opl_dc_in + opl_dc_coefficient * opl_dc_out;
            opl_dc_in = (float)level;
            level = (int)opl_dc_out;
        }
        sample = (Sint16)(level > 32767 ? 32767 : level < -32768 ? -32768 : level);
        for (channel = 0; channel < Game_AudioChannels; channel++) *out++ = sample;
    }
    SDL_UnlockMutex(opl_lock);
}

int Game_OplOpen(Game_OplServe serve, void *user, int rate)
{
    if (!Game_AudioIsOpen() || rate <= 0) return 0;
    Game_OplClose();
    if (opl_lock == NULL && (opl_lock = SDL_CreateMutex()) == NULL) return 0;
    chip = OPL_new(OPL_CLOCK, (uint32_t)Game_AudioRate);
    if (chip == NULL) return 0;
    OPL_setChipType(chip, OPL_YM3812);
    OPL_reset(chip);
    opl_serve = serve;
    opl_user = user;
    opl_rate = (unsigned int)rate;
    opl_phase = 0;
    opl_dc_block = 0;
    opl_queue_read = opl_queue_count = 0;
    opl_window_end = 0;
    Mix_HookMusic(opl_mix, NULL);
    return 1;
}

void Game_OplClose(void)
{
    if (chip == NULL) return;
    Mix_HookMusic(NULL, NULL);
    SDL_LockMutex(opl_lock);
    OPL_delete(chip);
    chip = NULL;
    opl_serve = NULL;
    opl_user = NULL;
    SDL_UnlockMutex(opl_lock);
}

void Game_OplDcBlock(void)
{
    Game_OplLock();
    /* a corner of 10 Hz */
    opl_dc_coefficient = 1.0f - 2.0f * 3.14159265f * 10.0f / (float)Game_AudioRate;
    opl_dc_in = opl_dc_out = 0.0f;
    opl_dc_block = 1;
    Game_OplUnlock();
}

void Game_OplLock(void)
{
    if (opl_lock == NULL) opl_lock = SDL_CreateMutex();
    SDL_LockMutex(opl_lock);
}

void Game_OplUnlock(void)
{
    SDL_UnlockMutex(opl_lock);
}

void Game_OplWrite(unsigned int reg, unsigned int value)
{
    if (chip != NULL) OPL_writeReg(chip, reg & 0xff, (uint8_t)value);
}

void Game_OplWriteTimed(unsigned int reg, unsigned int value)
{
    SDL_LockMutex(opl_lock);
    if (chip != NULL) {
        if (opl_queue_count == OPL_QUEUE) {
            /* the output stopped: the oldest goes in now */
            OPL_writeReg(chip, opl_queue[opl_queue_read].reg, opl_queue[opl_queue_read].value);
            opl_queue_read = (opl_queue_read + 1u) % OPL_QUEUE;
            opl_queue_count--;
        }
        opl_timed *const entry = &opl_queue[(opl_queue_read + opl_queue_count) % OPL_QUEUE];

        entry->time = SDL_GetPerformanceCounter();
        entry->reg = (uint8_t)reg;
        entry->value = (uint8_t)value;
        opl_queue_count++;
    }
    SDL_UnlockMutex(opl_lock);
}
