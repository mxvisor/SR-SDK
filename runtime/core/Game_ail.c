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

/* The Miles Sound System (AIL 3.x) without its entry points (Game_ail.h).
 *
 * A sample plays on a channel of its own of the SDL_mixer device: sample n
 * on channel n, reserved, so the channels a game takes with -1 (sndPlaySound)
 * are others.  Its PCM is converted to the format of the device once: the
 * data lives in the game's buffers, which it may load again, so the cache's
 * key holds a hash of it.  A sequence is XMIDI, turned into a standard MIDI
 * file and played by Game_midi (TinySoundFont, soundfont.sf2 of the game
 * directory); one plays at a time. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>
#include "Game_hooks.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_midi.h"
#include "Game_ail.h"

#define AIL_PREFERENCES     19
#define DIG_DEFAULT_VOLUME  5
#define AIL_CACHE           64
#define AIL_FREE_CHANNELS   4       /* channels left to the others */

typedef struct {
    int used;
    uint32_t status;
    const uint8_t *data;
    uint32_t length;
    int32_t format;
    uint32_t flags;
    int32_t rate, volume, pan, loop_count;
} ail_sample;

typedef struct {
    int used;
    uint32_t status;
    const uint8_t *data;
    uint32_t size, number;
} ail_sequence;

typedef struct {
    const uint8_t *data;
    uint32_t length, hash;
    int32_t format, rate;
    uint32_t flags, last_used;
    Uint8 *buffer;
    Mix_Chunk *chunk;
} ail_cached;

/* AIL 3.x defaults (AIL.H) */
static int32_t preferences[AIL_PREFERENCES] = {200, 1, 32768, 100, 16, 100, 655, 0, 0, 1, 0, 120, 8, 127, 1, 0, 2, 1, 1};
static int32_t driver_rate = 11025;
static ail_sample samples[GAME_AIL_SAMPLES];
static ail_sequence sequences[GAME_AIL_SEQUENCES];
static ail_cached cache[AIL_CACHE];
static uint32_t cache_clock;
static int channels_reserved;
static int playing_sequence = -1;  /* the sequence Game_midi plays */
static int sequences_allocated;     /* Game_midi is AIL's to shut down */
static int ail_log = -1;

#define AIL_LOG(...) do { if (ail_log < 0) ail_log = (getenv(GAME_ENV("TRACE_SOUND")) != NULL); \
                          if (ail_log) fprintf(stderr, "ail: " __VA_ARGS__); } while (0)

static ail_sample *sample_get(int sample)
{
    if (sample < 0 || sample >= GAME_AIL_SAMPLES || !samples[sample].used) return NULL;
    return &samples[sample];
}

static ail_sequence *sequence_get(int sequence)
{
    if (sequence < 0 || sequence >= GAME_AIL_SEQUENCES || !sequences[sequence].used) return NULL;
    return &sequences[sequence];
}

static void reserve_channel(int channel)
{
    if (!Game_AudioIsOpen() || channel < channels_reserved) return;
    channels_reserved = channel + 1;
    if (Mix_AllocateChannels(-1) < channels_reserved + AIL_FREE_CHANNELS) Mix_AllocateChannels(channels_reserved + AIL_FREE_CHANNELS);
    Mix_ReserveChannels(channels_reserved);
}

static void sample_halt(int sample)
{
    if (Game_AudioIsOpen() && sample < channels_reserved) Mix_HaltChannel(sample);
}

static int sample_playing(int sample)
{
    return Game_AudioIsOpen() && sample < channels_reserved && Mix_Playing(sample);
}

static uint32_t fnv1a(const uint8_t *data, uint32_t length)
{
    uint32_t hash = 2166136261u, i;

    for (i = 0; i < length; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

/* a looped sound can stay on its channel for long: its chunk stays */
static int chunk_in_use(const Mix_Chunk *chunk)
{
    int i;

    for (i = 0; i < channels_reserved; i++)
    {
        if (Mix_Playing(i) && Mix_GetChunk(i) == chunk) return 1;
    }
    return 0;
}

static void free_cache(void)
{
    int i;

    for (i = 0; i < AIL_CACHE; i++)
    {
        if (cache[i].chunk != NULL) Mix_FreeChunk(cache[i].chunk);
        SDL_free(cache[i].buffer);
        memset(&cache[i], 0, sizeof(cache[i]));
    }
}

static Mix_Chunk *converted(const ail_sample *s)
{
    uint32_t hash = fnv1a(s->data, s->length), oldest = 0xffffffffu;
    int i, victim = -1, channels, length = 0;
    SDL_AudioFormat format;
    SDL_AudioStream *stream;
    Uint8 *buffer = NULL;
    Mix_Chunk *chunk;
    ail_cached *entry;

    for (i = 0; i < AIL_CACHE; i++)
    {
        entry = &cache[i];
        if (entry->chunk != NULL && entry->data == s->data && entry->length == s->length && entry->hash == hash &&
            entry->format == s->format && entry->flags == s->flags && entry->rate == s->rate)
        {
            entry->last_used = ++cache_clock;
            return entry->chunk;
        }
        if (entry->chunk == NULL) { if (oldest != 0) { victim = i; oldest = 0; } }
        else if (entry->last_used < oldest && !chunk_in_use(entry->chunk)) { victim = i; oldest = entry->last_used; }
    }
    if (victim < 0) return NULL;

    if (s->format & GAME_AIL_F_16BITS) format = AUDIO_S16LSB;
    else format = (s->flags & GAME_AIL_PCM_SIGN) ? AUDIO_S8 : AUDIO_U8;
    channels = (s->format & GAME_AIL_F_STEREO) ? 2 : 1;
    stream = SDL_NewAudioStream(format, (Uint8)channels, s->rate,
                                (SDL_AudioFormat)Game_AudioFormat, (Uint8)Game_AudioChannels, Game_AudioRate);
    if (stream == NULL) return NULL;
    if (SDL_AudioStreamPut(stream, s->data, (int)s->length) == 0 && SDL_AudioStreamFlush(stream) == 0)
    {
        length = SDL_AudioStreamAvailable(stream);
        buffer = (Uint8 *)SDL_malloc(length > 0 ? (size_t)length : 1);
        if (buffer != NULL) length = SDL_AudioStreamGet(stream, buffer, length);
    }
    SDL_FreeAudioStream(stream);
    if (buffer == NULL || length <= 0)
    {
        SDL_free(buffer);
        return NULL;
    }
    chunk = Mix_QuickLoad_RAW(buffer, (Uint32)length);
    if (chunk == NULL)
    {
        SDL_free(buffer);
        return NULL;
    }

    entry = &cache[victim];
    if (entry->chunk != NULL) Mix_FreeChunk(entry->chunk);
    SDL_free(entry->buffer);
    entry->data = s->data;
    entry->length = s->length;
    entry->hash = hash;
    entry->format = s->format;
    entry->flags = s->flags;
    entry->rate = s->rate;
    entry->last_used = ++cache_clock;
    entry->buffer = buffer;
    entry->chunk = chunk;
    return chunk;
}

/* SDL_mixer drops a channel's panning when it stops: set at every start */
static void apply_volume_and_pan(int sample)
{
    const ail_sample *s = &samples[sample];
    int volume = s->volume < 0 ? 0 : s->volume > 127 ? 127 : s->volume;
    int pan = s->pan < 0 ? 0 : s->pan > 127 ? 127 : s->pan;

    if (!Game_AudioIsOpen() || sample >= channels_reserved) return;
    Mix_Volume(sample, (volume * MIX_MAX_VOLUME + 63) / 127);
    Mix_SetPanning(sample, pan <= 64 ? 255 : (Uint8)((127 - pan) * 255 / 63), pan >= 64 ? 255 : (Uint8)(pan * 255 / 64));
}

int32_t Game_AilSetPreference(uint32_t number, int32_t value)
{
    int32_t old;

    if (number >= AIL_PREFERENCES) return 0;
    old = preferences[number];
    preferences[number] = value;
    return old;
}

void Game_AilSetDriverRate(int32_t rate)
{
    AIL_LOG("driver rate %i\n", rate);
    if (rate >= 4000 && rate <= 48000) driver_rate = rate;
}

void Game_AilShutdown(void)
{
    int i;

    AIL_LOG("shutdown\n");
    for (i = 0; i < GAME_AIL_SAMPLES; i++)
    {
        if (samples[i].used) sample_halt(i);
        samples[i].used = 0;
    }
    if (Game_AudioIsOpen()) free_cache();
    if (sequences_allocated) Game_MidiShutdown();
    sequences_allocated = 0;
    playing_sequence = -1;
    for (i = 0; i < GAME_AIL_SEQUENCES; i++) sequences[i].used = 0;
}

/* ---- samples ---- */

int Game_AilAllocateSample(void)
{
    int i;

    for (i = 0; i < GAME_AIL_SAMPLES; i++)
    {
        if (!samples[i].used)
        {
            memset(&samples[i], 0, sizeof(samples[i]));
            samples[i].used = 1;
            reserve_channel(i);
            Game_AilInitSample(i);
            return i;
        }
    }
    return -1;
}

void Game_AilReleaseSample(int sample)
{
    if (sample_get(sample) == NULL) return;
    sample_halt(sample);
    samples[sample].used = 0;
}

void Game_AilInitSample(int sample)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    sample_halt(sample);
    s->status = GAME_AIL_DONE;
    s->data = NULL;
    s->length = 0;
    s->format = 0;
    s->flags = 0;
    s->rate = driver_rate;
    s->volume = preferences[DIG_DEFAULT_VOLUME];
    s->pan = 64;
    s->loop_count = 1;
}

void Game_AilSetSampleAddress(int sample, const void *data, uint32_t length)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    s->data = (const uint8_t *)data;
    s->length = length;
}

int Game_AilSetSampleWav(int sample, const void *wav)
{
    ail_sample *s = sample_get(sample);
    const uint8_t *file = (const uint8_t *)wav;
    const uint8_t *data = NULL;
    uint32_t riff_size, offset, length = 0, rate = 11025;
    uint16_t tag = 0, channels = 1, bits = 8;
    int has_fmt = 0;

    if (s == NULL || file == NULL || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) return 0;
    memcpy(&riff_size, file + 4, 4);
    for (offset = 12; offset + 8 <= riff_size + 8; )
    {
        uint32_t size;

        memcpy(&size, file + offset + 4, 4);
        if (memcmp(file + offset, "fmt ", 4) == 0 && size >= 16)
        {
            memcpy(&tag, file + offset + 8, 2);
            memcpy(&channels, file + offset + 10, 2);
            memcpy(&rate, file + offset + 12, 4);
            memcpy(&bits, file + offset + 22, 2);
            has_fmt = 1;
        }
        else if (memcmp(file + offset, "data", 4) == 0)
        {
            data = file + offset + 8;
            length = size;
            break;
        }
        offset += 8 + ((size + 1) & ~1u);
    }
    if (!has_fmt || tag != 1 || data == NULL || (bits != 8 && bits != 16) || channels < 1 || channels > 2)
    {
        AIL_LOG("sample %i: not a PCM WAV\n", sample);
        return 0;
    }
    sample_halt(sample);
    s->data = data;
    s->length = length;
    s->format = ((bits == 16) ? GAME_AIL_F_16BITS : 0) | ((channels == 2) ? GAME_AIL_F_STEREO : 0);
    s->flags = (bits == 16) ? GAME_AIL_PCM_SIGN : 0;
    s->rate = (int32_t)rate;
    s->status = GAME_AIL_DONE;
    return 1;
}

void Game_AilSetSampleType(int sample, int32_t format, uint32_t flags)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    s->format = format;
    s->flags = flags;
}

void Game_AilSetSampleRate(int sample, int32_t rate)
{
    ail_sample *s = sample_get(sample);

    if (s != NULL && rate > 0) s->rate = rate;
}

void Game_AilSetSampleVolume(int sample, int32_t volume)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    s->volume = volume;
    if (s->status != GAME_AIL_DONE) apply_volume_and_pan(sample);
}

void Game_AilSetSamplePan(int sample, int32_t pan)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    s->pan = pan;
    if (s->status != GAME_AIL_DONE) apply_volume_and_pan(sample);
}

void Game_AilSetSampleLoopCount(int sample, int32_t count)
{
    ail_sample *s = sample_get(sample);

    if (s != NULL) s->loop_count = count;
}

void Game_AilStartSample(int sample)
{
    ail_sample *s = sample_get(sample);
    Mix_Chunk *chunk;

    if (s == NULL || s->data == NULL || s->length == 0 || !Game_AudioIsOpen()) return;
    chunk = converted(s);
    if (chunk == NULL)
    {
        AIL_LOG("sample %i: no conversion: %s\n", sample, SDL_GetError());
        return;
    }
    if (Mix_PlayChannel(sample, chunk, s->loop_count == 0 ? -1 : s->loop_count - 1) < 0) return;
    apply_volume_and_pan(sample);
    s->status = GAME_AIL_PLAYING;
    AIL_LOG("start sample %i: %u bytes, format %i flags %u at %i Hz, volume %i, pan %i, loops %i\n", sample,
            s->length, s->format, s->flags, s->rate, s->volume, s->pan, s->loop_count);
}

void Game_AilStopSample(int sample)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL || s->status != GAME_AIL_PLAYING) return;
    if (sample_playing(sample)) Mix_Pause(sample);
    s->status = GAME_AIL_STOPPED;
}

void Game_AilResumeSample(int sample)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL || s->status != GAME_AIL_STOPPED) return;
    if (sample_playing(sample)) Mix_Resume(sample);
    s->status = GAME_AIL_PLAYING;
}

void Game_AilEndSample(int sample)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return;
    sample_halt(sample);
    s->status = GAME_AIL_DONE;
}

uint32_t Game_AilSampleStatus(int sample)
{
    ail_sample *s = sample_get(sample);

    if (s == NULL) return GAME_AIL_FREE;
    if (s->status == GAME_AIL_PLAYING && !sample_playing(sample)) s->status = GAME_AIL_DONE;
    return s->status;
}

const void *Game_AilSampleData(int sample)
{
    ail_sample *s = sample_get(sample);

    return (s != NULL) ? s->data : NULL;
}

/* ---- sequences ---- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* the bytes of XMIDI data, from its IFF chunks: an optional FORM XDIR, then
 * CAT XMID (or a lone FORM XMID); 0 for data of another kind */
static uint32_t xmidi_size(const uint8_t *data)
{
    uint32_t pos = 0, length;

    if (memcmp(data, "FORM", 4) == 0 && memcmp(data + 8, "XDIR", 4) == 0)
    {
        length = be32(data + 4);
        if (length > 0x1000000u) return 0;
        pos = 8 + length + (length & 1);
    }
    if ((memcmp(data + pos, "CAT ", 4) != 0 && memcmp(data + pos, "FORM", 4) != 0) ||
        memcmp(data + pos + 8, "XMID", 4) != 0) return 0;
    length = be32(data + pos + 4);
    if (length > 0x1000000u) return 0;
    return pos + 8 + length;
}

int Game_AilAllocateSequence(void)
{
    int i;

    for (i = 0; i < GAME_AIL_SEQUENCES; i++)
    {
        if (!sequences[i].used)
        {
            memset(&sequences[i], 0, sizeof(sequences[i]));
            sequences[i].used = 1;
            sequences[i].status = GAME_AIL_DONE;
            sequences_allocated = 1;
            return i;
        }
    }
    return -1;
}

void Game_AilReleaseSequence(int sequence)
{
    if (sequence_get(sequence) == NULL) return;
    Game_AilEndSequence(sequence);
    sequences[sequence].used = 0;
}

int Game_AilInitSequence(int sequence, const void *xmidi, uint32_t number)
{
    ail_sequence *q = sequence_get(sequence);
    uint32_t size;

    if (q == NULL) return 0;
    Game_AilEndSequence(sequence);
    size = (xmidi != NULL) ? xmidi_size((const uint8_t *)xmidi) : 0;
    AIL_LOG("init sequence %i: %u bytes of XMIDI, number %u\n", sequence, size, number);
    if (size == 0) return 0;
    q->data = (const uint8_t *)xmidi;
    q->size = size;
    q->number = number;
    q->status = GAME_AIL_STOPPED;
    return 1;
}

void Game_AilStartSequence(int sequence)
{
    ail_sequence *q = sequence_get(sequence);
    uint8_t *midi;
    size_t length = 0;
    int loop = 0;

    if (q == NULL || q->data == NULL) return;
    if (playing_sequence >= 0)
    {
        Game_MidiStop();
        sequences[playing_sequence].status = GAME_AIL_DONE;
        playing_sequence = -1;
    }
    q->status = GAME_AIL_DONE;
    if (!Game_AudioIsOpen()) return;
    midi = Game_MidiFromXmidi(q->data, q->size, q->number, &length, &loop);
    if (midi == NULL)
    {
        AIL_LOG("sequence %i: no sequence %u in the XMIDI\n", sequence, q->number);
        return;
    }
    if (Game_MidiPlay(midi, length, loop))
    {
        playing_sequence = sequence;
        q->status = GAME_AIL_PLAYING;
    }
    free(midi);     /* Game_MidiPlay keeps a copy */
    AIL_LOG("start sequence %i: %s\n", sequence, q->status == GAME_AIL_PLAYING ? "playing" : "not played");
}

void Game_AilStopSequence(int sequence)
{
    ail_sequence *q = sequence_get(sequence);

    if (q == NULL || q->status != GAME_AIL_PLAYING) return;
    if (sequence == playing_sequence) Game_MidiPause(1);
    q->status = GAME_AIL_STOPPED;
}

/* a sequence AIL_init_sequence set up is stopped at its start */
void Game_AilResumeSequence(int sequence)
{
    ail_sequence *q = sequence_get(sequence);

    if (q == NULL || q->status != GAME_AIL_STOPPED) return;
    if (sequence != playing_sequence)
    {
        Game_AilStartSequence(sequence);
        return;
    }
    Game_MidiPause(0);
    q->status = GAME_AIL_PLAYING;
}

void Game_AilEndSequence(int sequence)
{
    ail_sequence *q = sequence_get(sequence);

    if (q == NULL) return;
    if (sequence == playing_sequence)
    {
        Game_MidiStop();
        playing_sequence = -1;
    }
    q->status = GAME_AIL_DONE;
}

uint32_t Game_AilSequenceStatus(int sequence)
{
    ail_sequence *q = sequence_get(sequence);

    if (q == NULL) return GAME_AIL_FREE;
    if (q->status == GAME_AIL_PLAYING && sequence == playing_sequence && Game_MidiFinished())
    {
        q->status = GAME_AIL_DONE;
    }
    return q->status;
}
