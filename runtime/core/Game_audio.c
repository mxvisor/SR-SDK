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


/* The SDL_mixer device of every port, DOS and Win32: CD audio (Game_music.c)
 * and MIDI (Game_midi.c) play through it, and so does a game's own digital
 * sound on SDL_mixer channels.  It opens in the format audio/pc.c's
 * Init_Audio chose; GAME_MIXER_CHANNELS sets the channels (default SDL_mixer's
 * own).  A failure only silences the port. */

#include <stdio.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_hooks.h"

static int audio_open;

int Game_AudioInit(void)
{
    int rate = 0, channels = 0;
    Uint16 format = 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "%s: no audio: %s\n", GAME_NAME, SDL_GetError());
        return -1;
    }
    if (Mix_OpenAudio(Game_AudioRate, (Uint16)Game_AudioFormat, (int)Game_AudioChannels, Game_AudioBufferSize) != 0) {
        fprintf(stderr, "%s: no audio: %s\n", GAME_NAME, Mix_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    /* SDL_mixer lets the device change the rate and the channels (Android's
     * often runs at 48 kHz): what is mixed has to be made for what was opened. */
    if (Mix_QuerySpec(&rate, &format, &channels) != 0) {
        Game_AudioRate = rate;
        Game_AudioChannels = (unsigned int)channels;
        Game_AudioFormat = format;
    }
    fprintf(stderr, "%s: audio %i Hz, %i channels, format %x, %i samples\n",
            GAME_NAME, rate, channels, (unsigned int)format, Game_AudioBufferSize);
#ifdef GAME_MIXER_CHANNELS
    Mix_AllocateChannels(GAME_MIXER_CHANNELS);
#endif
    audio_open = 1;
    return 0;
}

void Game_AudioDeinit(void)
{
    if (!audio_open) return;
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_open = 0;
}

int Game_AudioIsOpen(void)
{
    return audio_open;
}
