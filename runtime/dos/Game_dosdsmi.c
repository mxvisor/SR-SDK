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

/* DSMI's MCP (Otto Chrons' Multi Channel Player, v2.6) on the host, for a game
 * that defines GAME_DOS_DSMI.
 *
 * DSMI (Digital Sound and Music Interface) has three layers: AMP plays the AMF
 * modules, MCP mixes the channels that AMP and the game's effects drive
 * through a CDI table, and an SDI driver hands MCP's buffer to the card.  MCP's
 * mixers read their volume and conversion tables through ES, a selector of a
 * DOS block with a base of its own; the recompiled code takes every segment as
 * flat, so they mix silence.  MCP also takes its time from the card's DMA
 * position.  So two CDI entries of MCP are replaced (the game's
 * instruction_replacements.sci calls these functions at their first
 * instruction, then returns):
 *
 *   getdelta (CDI entry 0x38)  Game_DsmiMcpGetDelta: the microseconds of sound
 *                              the host's stream lacks to hold MCP_LATENCY_US
 *                              (and the device's buffer) queued: the clock is
 *                              the device's, so the stream never runs dry
 *                              while the game polls in time, and never grows;
 *   poll (CDI entry 0x34)      Game_DsmiMcpPoll(microseconds, vars, order):
 *                              mixes so many microseconds of MCP's voices into
 *                              a stream of the host.  vars is the guest
 *                              address of MCP's stereo flag, the first of its
 *                              variables; order that of its word table of the
 *                              voice offsets of the channels.
 *
 * The other CDI entries stay the game's: they only write MCP's voice records,
 * which this file reads.  MCP's variables, from the stereo flag on:
 *   +000 b stereo   +06F b state (bit 1: open)   +088 b master volume 0..64
 *   +0AF d channels +0E4 the voice records, 0x28 bytes each:
 *   +00 w flags: 1 playing, 2 looped once, 4 muted, 8 sample set
 *   +02 d sample (guest address)   +06 d length    +0E d position
 *   +12 d loop start  +16 d loop end (0: no loop)
 *   +1A b volume 0..32  +1B d rate (Hz)  +23 w position fraction
 *   +25 w sample handle (0: in memory)   +27 b panning -63..63 (100 surround)
 * The samples are unsigned 8-bit (MCP's volume table starts at -128).
 *
 * Finding the addresses in a game: the CDI table of MCP holds the entries
 * (getdelta begins with a call through a pointer to the SDI's position
 * routine, poll with enter 0, 0), and every entry begins with the bound check
 * against the channels and the lookup in the order table. */

#include "Game_hooks.h"
#include "Game_vars.h"

#ifdef GAME_DOS_DSMI

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "Game_memory.h"
#include "Game_audiostream.h"

#define MCP_RATE 44100
/* the sound kept queued ahead of the device, besides two of its buffers: the
 * game polls from its main loop, some 10-20 ms apart, and a queue near empty
 * plays gaps (the device takes 1024 frames, 23 ms, at a time) */
#define MCP_LATENCY_US 60000

/* MCP's variables, by their distance from the stereo flag */
#define VAR_STEREO   0x000
#define VAR_STATE    0x06F   /* bit 1: open */
#define VAR_MASTER   0x088   /* 0..64 */
#define VAR_CHANNELS 0x0AF
#define VAR_VOICES   0x0E4

static Game_AudioSource *source;
static int source_failed;
static uint64_t frame_rest;   /* microseconds * rate left over, below one frame */
static int16_t *mix_out;
static int32_t *mix_acc;
static unsigned int mix_frames;
static int trace = -1;
static unsigned long trace_polls;

static int open_source(void)
{
    SDL_AudioSpec desired, obtained;

    if (source != NULL) return 1;
    if (source_failed) return 0;
    memset(&desired, 0, sizeof(desired));
    desired.freq = MCP_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    source = Game_AudioSourceOpen(&desired, &obtained, 0);
    if (source == NULL) {
        source_failed = 1;
        fprintf(stderr, GAME_PROGRAM_NAME ": DSMI MCP: no audio stream: %s\n", SDL_GetError());
        return 0;
    }
    Game_AudioSourcePause(source, 0);
    frame_rest = 0;
    return 1;
}

static uint8_t rd8(uint32_t address) { return *(uint8_t *)GAME_GUEST32_TO_PTR(address); }
static uint16_t rd16(uint32_t address) { uint16_t v; memcpy(&v, (void *)GAME_GUEST32_TO_PTR(address), 2); return v; }
static uint32_t rd32(uint32_t address) { uint32_t v; memcpy(&v, (void *)GAME_GUEST32_TO_PTR(address), 4); return v; }
static void wr16(uint32_t address, uint16_t v) { memcpy((void *)GAME_GUEST32_TO_PTR(address), &v, 2); }
static void wr32(uint32_t address, uint32_t v) { memcpy((void *)GAME_GUEST32_TO_PTR(address), &v, 4); }

/* One voice into acc (frames stereo pairs), as MCP's mixing loop
 * moves it: to the loop end, or the sample's end, then back to the loop
 * start, or the voice stops. */
static void mix_voice(uint32_t voice, uint32_t vars, int32_t *acc, unsigned int frames)
{
    uint16_t flags = rd16(voice + 0x00);
    const uint32_t sample = rd32(voice + 0x02);
    const uint32_t length = rd32(voice + 0x06);
    uint32_t position = rd32(voice + 0x0E);
    const uint32_t loop_start = rd32(voice + 0x12);
    const uint32_t loop_end = rd32(voice + 0x16);
    const uint32_t rate = rd32(voice + 0x1B);
    uint32_t fraction = rd16(voice + 0x23);
    const int pan = (int8_t)rd8(voice + 0x27);
    const int stereo = rd8(vars + VAR_STEREO) != 0;
    int level, left, right, looped;
    uint32_t end, step;
    const uint8_t *data;
    unsigned int index;

    if (!(flags & 1) || (flags & 4) || sample == 0 || rd16(voice + 0x25) != 0) return;
    looped = loop_end != 0 && loop_end > loop_start;
    end = looped ? loop_end : length;
    if (end == 0 || rate == 0) return;

    /* MCP's level: volume * master / 64, 0..32 */
    level = rd8(voice + 0x1A) * rd8(vars + VAR_MASTER) >> 6;
    if (level > 32) level = 32;
    left = right = level;
    if (stereo && pan != 100) {
        int p = pan < -63 ? -63 : pan > 63 ? 63 : pan;
        if (p > 0) left = level * (63 - p) / 63;
        else if (p < 0) right = level * (63 + p) / 63;
    }

    /* 16.16 steps at the host's rate */
    step = (uint32_t)(((uint64_t)rate << 16) / MCP_RATE);
    data = (const uint8_t *)GAME_GUEST32_TO_PTR(sample);
    for (index = 0; index < frames; index++) {
        int s0, s1, value;

        /* the end is past the last sample: a loop goes on from its start
         * with no sample dropped, so a short loop (a flute's) has no click */
        if (position >= end) {
            if (looped) {
                position = loop_start + (position - end) % (loop_end - loop_start);
                flags |= 3;
            } else {
                flags &= ~1;
                break;
            }
        }
        s0 = (int)data[position] - 128;
        if (position + 1 < end) s1 = (int)data[position + 1] - 128;
        else s1 = looped ? (int)data[loop_start] - 128 : s0;
        value = s0 + (((s1 - s0) * (int)(fraction & 0xFFFF)) >> 16);
        if (level != 0) {
            acc[2 * index] += value * left;
            acc[2 * index + 1] += value * right;
        }
        fraction += step;
        position += fraction >> 16;
        fraction &= 0xFFFF;
    }
    wr32(voice + 0x0E, position);
    wr16(voice + 0x23, (uint16_t)fraction);
    wr16(voice + 0x00, flags);
}

#ifdef __cplusplus
extern "C" {
#endif

/* CDI getdelta: the microseconds the stream lacks to its latency */
uint32_t Game_DsmiMcpGetDelta(void)
{
    uint64_t target, queued;

    if (!open_source()) return 0;
    target = MCP_LATENCY_US;
    if (Game_AudioRate > 0) target += 2000000ull * (uint32_t)Game_AudioBufferSize / (uint32_t)Game_AudioRate;
    queued = (uint64_t)Game_AudioSourceQueued(source) * 1000000u / (MCP_RATE * 4) + frame_rest / MCP_RATE;
    return queued < target ? (uint32_t)(target - queued) : 0;
}

/* CDI poll: mix microseconds of all channels */
int32_t Game_DsmiMcpPoll(uint32_t microseconds, uint32_t vars, uint32_t order)
{
    uint64_t total;
    unsigned int frames, channels, channel, index, active = 0;
    int32_t scale;

    if (!(rd8(vars + VAR_STATE) & 2)) return -1;
    if (!open_source()) return 0;
    total = (uint64_t)microseconds * MCP_RATE + frame_rest;
    frames = (unsigned int)(total / 1000000u);
    frame_rest = total % 1000000u;
    if (frames == 0) return 0;

    if (frames > mix_frames) {
        free(mix_out);
        free(mix_acc);
        mix_out = (int16_t *)malloc(frames * 2 * sizeof(int16_t));
        mix_acc = (int32_t *)malloc(frames * 2 * sizeof(int32_t));
        if (mix_out == NULL || mix_acc == NULL) { mix_frames = 0; return 0; }
        mix_frames = frames;
    }
    memset(mix_acc, 0, frames * 2 * sizeof(int32_t));

    channels = rd32(vars + VAR_CHANNELS);
    if (channels > 32) channels = 32;
    for (channel = 0; channel < channels; channel++) {
        const uint32_t voice = vars + VAR_VOICES + rd16(order + 2 * channel);
        if (rd16(voice) & 1) active++;
        mix_voice(voice, vars, mix_acc, frames);
    }

    /* a voice at full level is 128 * 32 = 4096: times scale, four of them
     * fill the range (MCP's table divides by the channel count instead) */
    scale = 2;
    for (index = 0; index < frames * 2; index++) {
        int32_t value = mix_acc[index] * scale;
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        mix_out[index] = (int16_t)value;
    }
    Game_AudioSourceQueue(source, mix_out, frames * 4);

    if (trace < 0) trace = getenv(GAME_ENV("TRACE_SOUND")) != NULL;
    if (trace && (trace_polls++ % 200) == 0)
        fprintf(stderr, GAME_PROGRAM_NAME ": DSMI MCP poll %u us, %u frames, %u/%u voices, queued %u\n",
                microseconds, frames, active, channels, (unsigned int)Game_AudioSourceQueued(source));
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* GAME_DOS_DSMI */
