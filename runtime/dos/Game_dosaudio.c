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


/* The default audio device of a DOS game: SDL_mixer opened in the format
 * audio/pc.c's Init_Audio chose, with GAME_MIXER_CHANNELS channels (default
 * 16).  A game that plays its own sound through it (digital sound, a sound
 * cache to free at the end) lists Game_dosaudio.c in runtime_skip and brings
 * Game_AudioInit, Game_AudioDeinit and Game_AudioIsOpen. */

#include <stdio.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_hooks.h"

#ifndef GAME_MIXER_CHANNELS
#define GAME_MIXER_CHANNELS 16
#endif

static int audio_open;

int Game_AudioInit(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "%s: no audio: %s\n", GAME_NAME, SDL_GetError());
        return -1;
    }
    /* the format audio/pc.c's Init_Audio chose */
    if (Mix_OpenAudio(Game_AudioRate, (Uint16)Game_AudioFormat, (int)Game_AudioChannels, Game_AudioBufferSize) != 0) {
        fprintf(stderr, "%s: no audio: %s\n", GAME_NAME, Mix_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    Mix_AllocateChannels(GAME_MIXER_CHANNELS);
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
