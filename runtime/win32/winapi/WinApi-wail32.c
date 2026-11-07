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

/* The digital sound of the Miles Sound System for Win32 (WAIL32.DLL, AIL 3.03)
 * for a game with GAME_WINAPI_WAIL32 1 in Game_hooks.h: a sample is a WAV
 * image in the guest's memory (AIL_set_sample_file) or raw PCM of a type
 * (AIL_set_sample_address and AIL_set_sample_type), converted to the format of
 * the port's SDL_mixer device and played on a channel of it.  The functions
 * are stdcall; a game that loads the DLL with LoadLibrary finds them with
 * GetProcAddress (GAME_EXTRA_PROCS), one that imports them through its import
 * shim (game/import_args.tsv). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"

#if GAME_WINAPI_WAIL32

#include <SDL.h>
#include <SDL_mixer.h>
#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_vfs.h"
#include "WinApi.h"
#include "WinApi-wail32.h"

/* the states of a sample (AIL.H) */
#define SMP_FREE        0x0001
#define SMP_DONE        0x0002
#define SMP_PLAYING     0x0004
#define SMP_STOPPED     0x0008

/* AIL_set_sample_type: format bit 0 16-bit, bit 1 stereo; flags bit 0 signed */
#define DIG_F_16BITS_MASK   1
#define DIG_F_STEREO_MASK   2
#define DIG_PCM_SIGN        1

#define WAIL_DRIVER_HANDLE  0x57440000u
#define WAIL_SAMPLE_BASE    0x57530000u
#define WAIL_SAMPLES        32
#define WAIL_CACHE          64

typedef struct {
    int used;
    uint32_t status;
    uint32_t data, length;      /* the guest's address of the PCM and its bytes */
    int32_t format;
    uint32_t flags;
    int32_t rate, volume, pan, loop_count;
    int channel;
    Mix_Chunk *chunk;           /* the chunk on the channel */
} wail_sample;

typedef struct {
    uint32_t data, length, hash;
    int32_t format, rate;
    uint32_t flags, last_used;
    Uint8 *buffer;
    Mix_Chunk *chunk;
} wail_cached;

static wail_sample samples[WAIL_SAMPLES];
static wail_cached cache[WAIL_CACHE];
static uint32_t cache_clock;
static uint32_t last_error;     /* the guest's string of AIL_last_error */
static int wail_log = -1;

#define WAIL_LOG(...) do { if (wail_log < 0) wail_log = (getenv(GAME_ENV("WAIL_LOG")) != NULL); \
                           if (wail_log) fprintf(stderr, "wail: " __VA_ARGS__); } while (0)

static wail_sample *sample_get(uint32_t handle)
{
    uint32_t i;

    if (handle < WAIL_SAMPLE_BASE || ((handle - WAIL_SAMPLE_BASE) & 15)) return NULL;
    i = (handle - WAIL_SAMPLE_BASE) / 16;
    if (i >= WAIL_SAMPLES || !samples[i].used) return NULL;
    return &samples[i];
}

static int sample_playing(const wail_sample *s)
{
    return s->chunk != NULL && s->channel >= 0 && Mix_Playing(s->channel) && Mix_GetChunk(s->channel) == s->chunk;
}

static void sample_halt(wail_sample *s)
{
    if (sample_playing(s)) Mix_HaltChannel(s->channel);
    s->channel = -1;
    s->chunk = NULL;
}

static void sample_defaults(wail_sample *s)
{
    sample_halt(s);
    s->status = SMP_DONE;
    s->data = s->length = 0;
    s->format = 0;
    s->flags = 0;
    s->rate = 11025;
    s->volume = 100;            /* DIG_DEFAULT_VOLUME */
    s->pan = 64;
    s->loop_count = 1;
}

static uint32_t fnv1a(const uint8_t *data, uint32_t length)
{
    uint32_t hash = 2166136261u, i;

    for (i = 0; i < length; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static int chunk_in_use(const Mix_Chunk *chunk)
{
    int i;

    for (i = 0; i < WAIL_SAMPLES; i++)
    {
        if (samples[i].used && samples[i].chunk == chunk && sample_playing(&samples[i])) return 1;
    }
    return 0;
}

/* the sample's PCM in the format of the device; the data lives in the game's
 * buffers, which it may load again, so the key holds a hash of it */
static Mix_Chunk *converted(const wail_sample *s)
{
    const uint8_t *pcm = (const uint8_t *)GUEST_MEM(s->data);
    uint32_t hash = fnv1a(pcm, s->length), oldest = 0xffffffffu;
    int i, victim = -1, channels, length = 0;
    SDL_AudioFormat format;
    SDL_AudioStream *stream;
    Uint8 *buffer = NULL;
    Mix_Chunk *chunk;
    wail_cached *entry;

    for (i = 0; i < WAIL_CACHE; i++)
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

    if (s->format & DIG_F_16BITS_MASK) format = (s->flags & DIG_PCM_SIGN) ? AUDIO_S16LSB : AUDIO_U16LSB;
    else format = (s->flags & DIG_PCM_SIGN) ? AUDIO_S8 : AUDIO_U8;
    channels = (s->format & DIG_F_STEREO_MASK) ? 2 : 1;
    stream = SDL_NewAudioStream(format, (Uint8)channels, s->rate,
                                (SDL_AudioFormat)Game_AudioFormat, (Uint8)Game_AudioChannels, Game_AudioRate);
    if (stream == NULL) return NULL;
    if (SDL_AudioStreamPut(stream, pcm, (int)s->length) == 0 && SDL_AudioStreamFlush(stream) == 0)
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
    if (entry->chunk != NULL) Mix_FreeChunk(entry->chunk);    /* halts a channel that still plays it */
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

static void apply_volume_and_pan(const wail_sample *s)
{
    int volume = s->volume < 0 ? 0 : s->volume > 127 ? 127 : s->volume;
    int pan = s->pan < 0 ? 0 : s->pan > 127 ? 127 : s->pan;

    Mix_Volume(s->channel, (volume * MIX_MAX_VOLUME + 63) / 127);
    Mix_SetPanning(s->channel, pan <= 64 ? 255 : (Uint8)((127 - pan) * 255 / 63), pan >= 64 ? 255 : (Uint8)(pan * 255 / 64));
}

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL AIL_startup_c(void)
{
    WAIL_LOG("AIL_startup\n");
    return 0;
}

uint32_t CCALL AIL_shutdown_c(void)
{
    int i;

    WAIL_LOG("AIL_shutdown\n");
    for (i = 0; i < WAIL_SAMPLES; i++)
    {
        if (samples[i].used) sample_halt(&samples[i]);
        samples[i].used = 0;
    }
    return 0;
}

/* the driver of the wave device: the port's mixer, whatever the format asked */
uint32_t CCALL AIL_waveOutOpen_c(uint32_t drvr, uint32_t lphWaveOut, uint32_t wDeviceID, uint32_t lpFormat)
{
    (void)wDeviceID; (void)lpFormat;
    if (!Game_AudioIsOpen()) return 1;          /* MMSYSERR_ERROR: no sound */
    if (drvr != 0) *(uint32_t *)GUEST_MEM(drvr) = WAIL_DRIVER_HANDLE;
    if (lphWaveOut != 0) *(uint32_t *)GUEST_MEM(lphWaveOut) = 0;
    WAIL_LOG("AIL_waveOutOpen -> %08x\n", WAIL_DRIVER_HANDLE);
    return 0;
}

uint32_t CCALL AIL_waveOutClose_c(uint32_t dig)
{
    (void)dig;
    return 0;
}

uint32_t CCALL AIL_allocate_sample_handle_c(uint32_t dig)
{
    int i;

    (void)dig;
    for (i = 0; i < WAIL_SAMPLES; i++)
    {
        if (!samples[i].used)
        {
            memset(&samples[i], 0, sizeof(samples[i]));
            samples[i].used = 1;
            samples[i].channel = -1;
            sample_defaults(&samples[i]);
            return WAIL_SAMPLE_BASE + (uint32_t)i * 16;
        }
    }
    return 0;
}

uint32_t CCALL AIL_release_sample_handle_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        sample_halt(s);
        s->used = 0;
    }
    return 0;
}

uint32_t CCALL AIL_init_sample_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s != NULL) sample_defaults(s);
    return 0;
}

/* a WAV file in memory: PCM, its type and rate from the "fmt " chunk; 0 for
 * a file of another kind */
uint32_t CCALL AIL_set_sample_file_c(uint32_t S, uint32_t file_image, uint32_t block)
{
    wail_sample *s = sample_get(S);
    const uint8_t *wav = (const uint8_t *)GUEST_MEM(file_image);
    uint32_t riff_size, offset, data = 0, length = 0;
    uint16_t tag = 0, channels = 1, bits = 8;
    uint32_t rate = 11025;
    int has_fmt = 0;

    (void)block;
    if (s == NULL || wav == NULL || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) return 0;
    memcpy(&riff_size, wav + 4, 4);
    for (offset = 12; offset + 8 <= riff_size + 8; )
    {
        uint32_t size;

        memcpy(&size, wav + offset + 4, 4);
        if (memcmp(wav + offset, "fmt ", 4) == 0 && size >= 16)
        {
            memcpy(&tag, wav + offset + 8, 2);
            memcpy(&channels, wav + offset + 10, 2);
            memcpy(&rate, wav + offset + 12, 4);
            memcpy(&bits, wav + offset + 22, 2);
            has_fmt = 1;
        }
        else if (memcmp(wav + offset, "data", 4) == 0)
        {
            data = file_image + offset + 8;
            length = size;
            break;
        }
        offset += 8 + ((size + 1) & ~1u);
    }
    if (!has_fmt || tag != 1 || data == 0 || (bits != 8 && bits != 16) || channels < 1 || channels > 2)
    {
        WAIL_LOG("AIL_set_sample_file(%08x): not a PCM WAV\n", file_image);
        return 0;
    }
    sample_halt(s);
    s->data = data;
    s->length = length;
    s->format = ((bits == 16) ? DIG_F_16BITS_MASK : 0) | ((channels == 2) ? DIG_F_STEREO_MASK : 0);
    s->flags = (bits == 16) ? DIG_PCM_SIGN : 0;
    s->rate = (int32_t)rate;
    s->status = SMP_DONE;
    return 1;
}

uint32_t CCALL AIL_set_sample_address_c(uint32_t S, uint32_t start, uint32_t length)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        sample_halt(s);
        s->data = start;
        s->length = length;
    }
    return 0;
}

uint32_t CCALL AIL_set_sample_type_c(uint32_t S, uint32_t format, uint32_t flags)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        s->format = (int32_t)format;
        s->flags = flags;
    }
    return 0;
}

uint32_t CCALL AIL_set_sample_playback_rate_c(uint32_t S, uint32_t rate)
{
    wail_sample *s = sample_get(S);

    if (s != NULL && (int32_t)rate > 0) s->rate = (int32_t)rate;
    return 0;
}

uint32_t CCALL AIL_set_sample_volume_c(uint32_t S, uint32_t volume)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        s->volume = (int32_t)volume;
        if (sample_playing(s)) apply_volume_and_pan(s);
    }
    return 0;
}

uint32_t CCALL AIL_set_sample_pan_c(uint32_t S, uint32_t pan)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        s->pan = (int32_t)pan;
        if (sample_playing(s)) apply_volume_and_pan(s);
    }
    return 0;
}

/* 0 plays forever */
uint32_t CCALL AIL_set_sample_loop_count_c(uint32_t S, uint32_t count)
{
    wail_sample *s = sample_get(S);

    if (s != NULL) s->loop_count = (int32_t)count;
    return 0;
}

uint32_t CCALL AIL_start_sample_c(uint32_t S)
{
    wail_sample *s = sample_get(S);
    Mix_Chunk *chunk;

    if (s == NULL || s->data == 0 || s->length == 0 || !Game_AudioIsOpen()) return 0;
    sample_halt(s);
    chunk = converted(s);
    if (chunk == NULL) return 0;
    s->channel = Mix_PlayChannel(-1, chunk, s->loop_count == 0 ? -1 : s->loop_count - 1);
    if (s->channel < 0) return 0;
    s->chunk = chunk;
    apply_volume_and_pan(s);
    s->status = SMP_PLAYING;
    WAIL_LOG("AIL_start_sample(%08x): %u bytes at %i Hz, volume %i, channel %i\n", S, s->length, s->rate, s->volume, s->channel);
    return 0;
}

uint32_t CCALL AIL_stop_sample_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s != NULL && s->status == SMP_PLAYING)
    {
        if (sample_playing(s)) Mix_Pause(s->channel);
        s->status = SMP_STOPPED;
    }
    return 0;
}

uint32_t CCALL AIL_resume_sample_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s != NULL && s->status == SMP_STOPPED)
    {
        if (sample_playing(s)) Mix_Resume(s->channel);
        s->status = SMP_PLAYING;
    }
    return 0;
}

uint32_t CCALL AIL_end_sample_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s != NULL)
    {
        sample_halt(s);
        s->status = SMP_DONE;
    }
    return 0;
}

uint32_t CCALL AIL_sample_status_c(uint32_t S)
{
    wail_sample *s = sample_get(S);

    if (s == NULL) return SMP_FREE;
    if (s->status == SMP_PLAYING && !sample_playing(s))
    {
        s->status = SMP_DONE;
        s->chunk = NULL;
        s->channel = -1;
    }
    return s->status;
}

/* the mixer serves itself */
uint32_t CCALL AIL_serve_c(void)
{
    return 0;
}

uint32_t CCALL AIL_last_error_c(void)
{
    if (last_error == 0)
    {
        char *text = (char *)x86_malloc(1);
        if (text != NULL) text[0] = 0;
        last_error = GUEST_ADDR(text);
    }
    return last_error;
}

/* a file of the game into memory: into dest, or a block of its own for dest 0 */
uint32_t CCALL FILE_read_c(uint32_t filename, uint32_t dest)
{
    char path[1024];
    FILE *file;
    long size;
    void *out;

    if (filename == 0 || !Game_ResolveDataPath((const char *)GUEST_MEM(filename), path, sizeof(path))) return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    out = (dest != 0) ? GUEST_MEM(dest) : x86_malloc((unsigned int)(size > 0 ? size : 1));
    if (out == NULL || size < 0 || fread(out, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    return GUEST_ADDR(out);
}

#ifdef __cplusplus
}
#endif

#endif /* GAME_WINAPI_WAIL32 */
