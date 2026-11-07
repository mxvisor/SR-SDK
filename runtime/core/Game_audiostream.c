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

/* A source's PCM on the speakers (Game_audiostream.h).  By default each source
 * has a device of its own, with the small buffer it asks for: the Sound Blaster
 * driver of a game times its IRQs by it.  SDL has one playback device on
 * Android, though, and the second SDL_OpenAudioDevice, after SDL_mixer's,
 * fails: there (and anywhere with GAME_AUDIO_ON_MIXER 1 in Game_hooks.h) the
 * sources are converted to SDL_mixer's format and added to its output after
 * the mix.  SDL_mixer has one post-mix hook: this file owns it and mixes every
 * open source in. */

#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_audiostream.h"
#include "Game_hooks.h"

#ifndef GAME_AUDIO_ON_MIXER
#ifdef __ANDROID__
#define GAME_AUDIO_ON_MIXER 1
#else
#define GAME_AUDIO_ON_MIXER 0
#endif
#endif

#define MIXED_SOURCES 4

struct Game_AudioSource {
    SDL_AudioDeviceID device;   /* its own device; 0 on SDL_mixer */
    SDL_AudioSpec spec;         /* the samples the source makes */
    /* on SDL_mixer */
    SDL_mutex *lock;            /* the callback and the queue run under it */
    SDL_AudioStream *convert;   /* spec to SDL_mixer's format */
    SDL_AudioStream *queue;     /* the queued samples, with no callback */
    Uint8 *piece;               /* spec.size bytes, one fill */
    volatile int paused;
};

static Game_AudioSource *mixed[MIXED_SOURCES];
static SDL_mutex *mixed_lock;

/* One piece of the source's samples (under source->lock) */
static void source_fill(Game_AudioSource *source)
{
    const int size = (int)source->spec.size;
    int got = 0;

    if (source->spec.callback != NULL) {
        source->spec.callback(source->spec.userdata, source->piece, size);
        return;
    }
    if (source->queue != NULL) got = SDL_AudioStreamGet(source->queue, source->piece, size);
    if (got < 0) got = 0;
    memset(source->piece + got, source->spec.silence, (size_t)(size - got));
}

static void SDLCALL post_mix(void *udata, Uint8 *stream, int len)
{
    static Uint8 converted[4096];
    int index;

    (void)udata;
    SDL_LockMutex(mixed_lock);
    for (index = 0; index < MIXED_SOURCES; index++) {
        Game_AudioSource *source = mixed[index];
        Uint8 *out = stream;
        int left = len;

        if (source == NULL) continue;
        SDL_LockMutex(source->lock);
        while (!source->paused && left > 0) {
            const int part = left < (int)sizeof(converted) ? left : (int)sizeof(converted);
            int got;

            while (SDL_AudioStreamAvailable(source->convert) < part) {
                source_fill(source);
                if (SDL_AudioStreamPut(source->convert, source->piece, (int)source->spec.size) != 0) break;
            }
            got = SDL_AudioStreamGet(source->convert, converted, part);
            if (got <= 0) break;
            SDL_MixAudioFormat(out, converted, (SDL_AudioFormat)Game_AudioFormat, (Uint32)got, SDL_MIX_MAXVOLUME);
            out += got;
            left -= got;
        }
        SDL_UnlockMutex(source->lock);
    }
    SDL_UnlockMutex(mixed_lock);
}

static void mixer_free(Game_AudioSource *source)
{
    if (source->convert != NULL) SDL_FreeAudioStream(source->convert);
    if (source->queue != NULL) SDL_FreeAudioStream(source->queue);
    if (source->lock != NULL) SDL_DestroyMutex(source->lock);
    free(source->piece);
}

/* The source in SDL_mixer's output: what allowed_changes lets change becomes
 * SDL_mixer's, the rest is converted.  0 when it cannot be. */
static int mixer_open(Game_AudioSource *source, const SDL_AudioSpec *desired, int allowed_changes)
{
    SDL_AudioSpec *spec = &source->spec;
    int index;

    *spec = *desired;
    if (allowed_changes & SDL_AUDIO_ALLOW_FREQUENCY_CHANGE) spec->freq = Game_AudioRate;
    if (allowed_changes & SDL_AUDIO_ALLOW_FORMAT_CHANGE) spec->format = (SDL_AudioFormat)Game_AudioFormat;
    if (allowed_changes & SDL_AUDIO_ALLOW_CHANNELS_CHANGE) spec->channels = (Uint8)Game_AudioChannels;
    if (spec->samples == 0) spec->samples = 1024;
    spec->silence = (Uint8)(SDL_AUDIO_ISSIGNED(spec->format) ? 0 : 0x80);
    spec->size = (Uint32)spec->samples * spec->channels * (SDL_AUDIO_BITSIZE(spec->format) / 8);

    if (mixed_lock == NULL) mixed_lock = SDL_CreateMutex();
    source->lock = SDL_CreateMutex();
    source->piece = (Uint8 *)malloc(spec->size);
    source->convert = SDL_NewAudioStream(spec->format, spec->channels, spec->freq,
                                         (SDL_AudioFormat)Game_AudioFormat, (Uint8)Game_AudioChannels, Game_AudioRate);
    if (spec->callback == NULL)
        source->queue = SDL_NewAudioStream(spec->format, spec->channels, spec->freq,
                                           spec->format, spec->channels, spec->freq);
    if (mixed_lock == NULL || source->lock == NULL || source->piece == NULL || source->convert == NULL ||
        (spec->callback == NULL && source->queue == NULL)) {
        mixer_free(source);
        return 0;
    }

    SDL_LockMutex(mixed_lock);
    for (index = 0; index < MIXED_SOURCES && mixed[index] != NULL; index++) {}
    if (index < MIXED_SOURCES) mixed[index] = source;
    SDL_UnlockMutex(mixed_lock);
    if (index == MIXED_SOURCES) {
        mixer_free(source);
        return 0;
    }
    Mix_SetPostMix(post_mix, NULL);
    return 1;
}

Game_AudioSource *Game_AudioSourceOpen(const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes)
{
    Game_AudioSource *source = (Game_AudioSource *)calloc(1, sizeof(*source));

    if (source == NULL) return NULL;
    source->paused = 1;
    if (GAME_AUDIO_ON_MIXER && Game_AudioIsOpen() && mixer_open(source, desired, allowed_changes)) {
        *obtained = source->spec;
        return source;
    }
    memset(source, 0, sizeof(*source));
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        free(source);
        return NULL;
    }
    source->device = SDL_OpenAudioDevice(NULL, 0, desired, &source->spec, allowed_changes);
    if (source->device == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        free(source);
        return NULL;
    }
    *obtained = source->spec;
    return source;
}

void Game_AudioSourceClose(Game_AudioSource *source)
{
    int index, left = 0;

    if (source == NULL) return;
    if (source->device != 0) {
        SDL_CloseAudioDevice(source->device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        free(source);
        return;
    }
    SDL_LockMutex(mixed_lock);
    for (index = 0; index < MIXED_SOURCES; index++) {
        if (mixed[index] == source) mixed[index] = NULL;
        if (mixed[index] != NULL) left++;
    }
    SDL_UnlockMutex(mixed_lock);
    if (left == 0 && Game_AudioIsOpen()) Mix_SetPostMix(NULL, NULL);
    mixer_free(source);
    free(source);
}

void Game_AudioSourceLock(Game_AudioSource *source)
{
    if (source == NULL) return;
    if (source->device != 0) SDL_LockAudioDevice(source->device);
    else SDL_LockMutex(source->lock);
}

void Game_AudioSourceUnlock(Game_AudioSource *source)
{
    if (source == NULL) return;
    if (source->device != 0) SDL_UnlockAudioDevice(source->device);
    else SDL_UnlockMutex(source->lock);
}

void Game_AudioSourcePause(Game_AudioSource *source, int pause)
{
    if (source == NULL) return;
    if (source->device != 0) SDL_PauseAudioDevice(source->device, pause);
    else source->paused = pause;
}

int Game_AudioSourceQueue(Game_AudioSource *source, const void *data, Uint32 len)
{
    int result;

    if (source == NULL) return -1;
    if (source->device != 0) return SDL_QueueAudio(source->device, data, len);
    if (source->queue == NULL) return -1;
    SDL_LockMutex(source->lock);
    result = SDL_AudioStreamPut(source->queue, data, (int)len);
    SDL_UnlockMutex(source->lock);
    return result;
}

Uint32 Game_AudioSourceQueued(Game_AudioSource *source)
{
    int queued;

    if (source == NULL) return 0;
    if (source->device != 0) return SDL_GetQueuedAudioSize(source->device);
    if (source->queue == NULL) return 0;
    SDL_LockMutex(source->lock);
    queued = SDL_AudioStreamAvailable(source->queue);
    SDL_UnlockMutex(source->lock);
    return queued > 0 ? (Uint32)queued : 0;
}

int Game_AudioSourceOnMixer(const Game_AudioSource *source)
{
    return source != NULL && source->device == 0;
}
