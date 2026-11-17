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

static void opl_mix(void *udata, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)(sizeof(Sint16) * Game_AudioChannels);

    (void)udata;
    SDL_LockMutex(opl_lock);
    while (frames-- > 0) {
        Sint16 sample;
        int level;
        unsigned int channel;

        opl_phase += opl_rate;
        if (opl_phase >= (unsigned int)Game_AudioRate) {
            opl_phase -= (unsigned int)Game_AudioRate;
            if (opl_serve != NULL) opl_serve(opl_user);
        }
        /* 1.5 times emu8950's output: the chip's level beside the Sound
         * Blaster's DAC (Game_dosail2.c), as DOSBox Staging measures them */
        level = OPL_calc(chip) * 3 / 2;
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
