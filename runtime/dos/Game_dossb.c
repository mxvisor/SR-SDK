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

/* A Sound Blaster 2.0 for the game's own sound driver (GAME_DOS_SOUNDBLASTER
 * in Game_hooks.h): the DSP at GAME_SB_BASE (220h), the 8-bit DMA channel
 * GAME_SB_DMA (1) registers, and the IRQ GAME_SB_IRQ (5, INT 0Dh) delivered at
 * each auto-init block boundary.  The driver programs the DMA, starts the
 * auto-init playback with the DSP command 1Ch, and reads the DMA address back
 * to see how far the card has played; so the DSP commands, the DMA registers
 * and the IRQ at the end of a block are what a game needs.  The game finds the
 * card through the BLASTER variable of its environment (Game_DosBlaster).
 *
 * The IRQ is delivered to the guest's handler when SR_CheckTimer runs
 * (Game_dostimer.c), and the handler has to be translated code: SR does not
 * find a handler that is reached only through the pointer the game gives to
 * INT 21h AH=25h, so the game's fixup_interpret_as_code.sci lists it. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_hooks.h"
#include "Game_vars.h"
#include "Game_dosmem.h"
#include "Game_memory.h"
#include "Game_dosint.h"
#include "Game_dossb.h"
#include "Game_audiostream.h"

#define SB_BASE GAME_SB_BASE
#define SB_DMA GAME_SB_DMA
#define DMA_CHANNELS 4

static const uint8_t dma_page_port[DMA_CHANNELS] = { 0x87, 0x83, 0x81, 0x82 };

typedef struct {
    uint16_t address;
    uint16_t count;
    uint8_t page;
} dma_channel;

static dma_channel dma[DMA_CHANNELS];
static uint8_t dma_flipflop;

static uint8_t dsp_out[16];
static unsigned int dsp_out_count;
static uint8_t dsp_command;
static uint8_t dsp_params[2];
static unsigned int dsp_param_count;
static unsigned int dsp_param_need;
static uint8_t dsp_reset_pending;
static uint8_t dsp_time_constant = 0xd3;
static uint32_t dsp_block_size;
static uint8_t dsp_speaker;
static uint8_t dsp_irq_pending;

/* The mixer (SB Pro): the index at base+4, the data at base+5.  A game sets
 * its volume there: the voice volume (04h) and the master volume (22h), the
 * left channel in the high 3 bits of the nibble and the right in the low
 * ones.  The stream is mono, so the louder channel counts. */
static uint8_t mixer_index;
static uint8_t mixer_regs[256];
static int mixer_written;
static int mixer_gain = 256;

/* The IRQ the BLASTER string gives the game.  The game's own driver parses
 * it too and installs its handler at 0x08 + IRQ, so the card has to raise
 * the same one. */
static const uint8_t sb_irq = GAME_SB_IRQ;

static Game_AudioSource *audio_source;
static SDL_mutex *audio_lock;
static int audio_samples;
static uint32_t play_rate;
static volatile int playing;
static uint32_t play_start;
static uint32_t play_length;
static uint32_t play_position;
static uint64_t play_stamp;
static uint64_t irq_last_stamp;
static uint32_t irq_last_position;

static uint32_t current_rate(void)
{
    return 1000000u / (256u - dsp_time_constant);
}

/* A register's volume, 0..7, as a part of 256.  A step of the card's volume
 * is about 4.5 dB, and zero is silence. */
static int mixer_level(uint8_t value)
{
    static const int gain[8] = { 0, 11, 19, 32, 54, 91, 152, 256 };
    const int left = (value >> 5) & 7;
    const int right = (value >> 1) & 7;
    return gain[left > right ? left : right];
}

static void mixer_update(void)
{
    mixer_gain = mixer_level(mixer_regs[0x04]) * mixer_level(mixer_regs[0x22]) / 256;
}

/* The next length samples the DSP takes from the DMA buffer (under audio_lock). */
static void sb_fill(Uint8 *stream, int length)
{
    uint8_t *buffer;
    int index;

    buffer = playing ? Game_DosMemPtr(play_start, 1) : NULL;
    if (buffer == NULL || play_length == 0 || !dsp_speaker) {
        memset(stream, 0x80, (size_t)length);
        if (buffer != NULL && play_length != 0) play_position = (play_position + (uint32_t)length) % play_length;
    } else {
        for (index = 0; index < length; index++) {
            stream[index] = (uint8_t)(0x80 + ((buffer[play_position] - 0x80) * mixer_gain >> 8));
            if (++play_position >= play_length) play_position = 0;
        }
    }
}

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int length)
{
    (void)userdata;
    SDL_LockMutex(audio_lock);
    sb_fill(stream, length);
    play_stamp = SDL_GetPerformanceCounter();
    SDL_UnlockMutex(audio_lock);
}

static void audio_open(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    uint32_t rate = current_rate();

    if (audio_source != NULL && play_rate == rate) return;
    if (audio_lock == NULL) audio_lock = SDL_CreateMutex();
    Game_AudioSourceClose(audio_source);
    SDL_zero(want);
    want.freq = (int)rate;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 128;
    want.callback = audio_callback;
    audio_source = Game_AudioSourceOpen(&want, &have, 0);
    if (audio_source == NULL) {
        fprintf(stderr, GAME_NAME ": no Sound Blaster output: %s\n", SDL_GetError());
        return;
    }
    /* on SDL_mixer's device the card's samples go out a mixer buffer at a time */
    audio_samples = Game_AudioSourceOnMixer(audio_source)
        ? (int)((uint64_t)Game_AudioBufferSize * rate / (uint32_t)Game_AudioRate)
        : have.samples;
    play_rate = rate;
    play_stamp = SDL_GetPerformanceCounter();
    Game_AudioSourcePause(audio_source, 0);
}

static void dsp_start(void)
{
    const dma_channel *channel = &dma[SB_DMA];
    /* The 8237 addresses real-mode memory: the page and the address are the
     * linear address of the buffer in the DOS memory, the guest's own
     * (Game_dosmem.h). */
    const uint32_t linear = ((uint32_t)channel->page << 16) | channel->address;

    SDL_LockMutex(audio_lock != NULL ? audio_lock : (audio_lock = SDL_CreateMutex()));
    play_start = linear;
    play_length = (uint32_t)channel->count + 1u;
    play_position = 0;
    irq_last_position = 0;
    playing = 1;
    SDL_UnlockMutex(audio_lock);
    audio_open();
}

static void dsp_put(uint8_t value)
{
    if (dsp_out_count < sizeof(dsp_out)) dsp_out[dsp_out_count++] = value;
}

static void dsp_reset(void)
{
    SDL_LockMutex(audio_lock != NULL ? audio_lock : (audio_lock = SDL_CreateMutex()));
    playing = 0;
    SDL_UnlockMutex(audio_lock);
    dsp_command = 0;
    dsp_param_need = 0;
    dsp_param_count = 0;
    dsp_out_count = 0;
    dsp_irq_pending = 0;
    dsp_speaker = 0;
    dsp_put(0xaa);
}

static void dsp_execute(void)
{
    switch (dsp_command) {
        case 0x40: dsp_time_constant = dsp_params[0]; break;
        case 0x48: dsp_block_size = ((uint32_t)dsp_params[1] << 8 | dsp_params[0]) + 1u; break;
        case 0x14: case 0x1c: dsp_start(); break;
        default: break;
    }
}

static void dsp_write(uint8_t value)
{
    if (dsp_param_need != 0) {
        dsp_params[dsp_param_count++] = value;
        if (dsp_param_count == dsp_param_need) {
            dsp_param_need = 0;
            dsp_execute();
        }
        return;
    }
    dsp_command = value;
    dsp_param_count = 0;
    switch (value) {
        case 0x40: dsp_param_need = 1; break;
        case 0x48: case 0x14: dsp_param_need = 2; break;
        case 0x1c: dsp_execute(); break;
        case 0xd1: dsp_speaker = 1; break;
        case 0xd3: dsp_speaker = 0; break;
        case 0xd0: case 0xda:
            SDL_LockMutex(audio_lock != NULL ? audio_lock : (audio_lock = SDL_CreateMutex()));
            playing = 0;
            SDL_UnlockMutex(audio_lock);
            break;
        case 0xd4:
            if (play_length != 0) playing = 1;
            break;
        case 0xe1: dsp_put(2); dsp_put(1); break;
        case 0xd8: dsp_put(dsp_speaker ? 0xff : 0x00); break;
        default: break;
    }
}

/* The samples the DSP has taken from the DMA buffer so far, in bytes.  The
 * 8237's address and count registers follow the transfer, and the guest's own
 * sound engine (the game's driver, not AIL) reads the address register to
 * tell which half of its double buffer is being played, so both registers
 * have to move with the transfer. */
static uint32_t sb_position(void)
{
    uint32_t position;

    if (play_length == 0) return 0;
    SDL_LockMutex(audio_lock);
    position = play_position;
    if (playing && audio_source != NULL) {
        const uint64_t elapsed = SDL_GetPerformanceCounter() - play_stamp;
        uint64_t ahead = elapsed * play_rate / SDL_GetPerformanceFrequency();

        if (ahead > (uint64_t)audio_samples) ahead = (uint64_t)audio_samples;
        position = (position + (uint32_t)ahead) % play_length;
    }
    SDL_UnlockMutex(audio_lock);
    return position;
}

/* The 8237 counts the address up as the transfer runs and the auto-init
 * restarts it at the beginning of the buffer; the count counts down. */
static uint16_t dma_address_now(const dma_channel *channel)
{
    if (channel != &dma[SB_DMA] || play_length == 0) return channel->address;
    return (uint16_t)(channel->address + (uint16_t)sb_position());
}

static uint16_t dma_count_now(const dma_channel *channel)
{
    if (channel != &dma[SB_DMA] || play_length == 0) return channel->count;
    return (uint16_t)(channel->count - (uint16_t)sb_position());
}

static int trace_sb(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_SOUND")) != NULL;
    return enabled;
}

/* <GAME>_SB_DISABLE: the card is not there at all.  The guest's own driver
 * looks for the DSP (it waits for the 0xAA of a reset), and without it it
 * leaves its sound off: the way to tell the sound apart from the rest. */
static int sb_disabled(void)
{
    static int disabled = -1;
    if (disabled < 0) disabled = getenv(GAME_ENV("SB_DISABLE")) != NULL;
    return disabled;
}

int Game_DosSbIn(uint16_t port, uint32_t *value)
{
    if (port >= SB_BASE && port < SB_BASE + 0x10u) {
        if (sb_disabled()) return 0;
        switch (port - SB_BASE) {
            case 0x0a:
                if (dsp_out_count == 0) { *value = 0xff; return 1; }
                *value = dsp_out[0];
                memmove(dsp_out, dsp_out + 1, --dsp_out_count);
                return 1;
            case 0x05: *value = mixer_regs[mixer_index]; return 1;
            case 0x0c: *value = 0x00; return 1;
            case 0x0e:
                *value = (dsp_out_count != 0 || dsp_irq_pending) ? 0x80u : 0x00u;
                dsp_irq_pending = 0;
                return 1;
            default: *value = 0xff; return 1;
        }
    }
    if (port < 8u) {
        const unsigned int channel = port >> 1;

        dma_flipflop ^= 1;
        if (port & 1u) {
            static uint16_t latched;

            if (dma_flipflop) latched = dma_count_now(&dma[channel]);
            *value = dma_flipflop ? (latched & 0xffu) : (latched >> 8);
        } else {
            const uint16_t address = dma_address_now(&dma[channel]);

            *value = dma_flipflop ? (address & 0xffu) : (address >> 8);
        }
        return 1;
    }
    return 0;
}

int Game_DosSbOut(uint16_t port, uint32_t value)
{
    unsigned int channel;

    if (port >= SB_BASE && port < SB_BASE + 0x10u) {
        if (sb_disabled()) return 0;
        if (trace_sb()) fprintf(stderr, "SB out %x<%02x (pos %u block %u rate %u playing %d)\n",
                                port - SB_BASE, value & 0xffu, play_position, dsp_block_size, play_rate, playing);
        switch (port - SB_BASE) {
            case 0x06:
                if (value & 1u) dsp_reset_pending = 1;
                else if (dsp_reset_pending) { dsp_reset_pending = 0; dsp_reset(); }
                break;
            case 0x04: mixer_index = (uint8_t)value; break;
            case 0x05:
                SDL_LockMutex(audio_lock != NULL ? audio_lock : (audio_lock = SDL_CreateMutex()));
                if (!mixer_written) {
                    /* full volume until the game sets it */
                    mixer_regs[0x04] = mixer_regs[0x22] = 0xff;
                    mixer_written = 1;
                }
                mixer_regs[mixer_index] = (uint8_t)value;
                mixer_update();
                SDL_UnlockMutex(audio_lock);
                break;
            case 0x0c: dsp_write((uint8_t)value); break;
            default: break;
        }
        return 1;
    }
    if (port < 8u) {
        uint16_t *field;
        channel = port >> 1;
        field = (port & 1u) ? &dma[channel].count : &dma[channel].address;
        if (dma_flipflop == 0) *field = (uint16_t)((*field & 0xff00u) | (value & 0xffu));
        else *field = (uint16_t)((*field & 0x00ffu) | ((value & 0xffu) << 8));
        dma_flipflop ^= 1;
        return 1;
    }
    if (port == 0x0c) { dma_flipflop = 0; return 1; }
    if (port == 0x0a || port == 0x0b) return 1;
    for (channel = 0; channel < DMA_CHANNELS; channel++) {
        if (port == dma_page_port[channel]) {
            dma[channel].page = (uint8_t)value;
            return 1;
        }
    }
    return 0;
}

/* <GAME>_SB_NOIRQ: the card works, but its IRQ is not delivered (a test). */
static int sb_no_irq(void)
{
    static int no_irq = -1;
    if (no_irq < 0) no_irq = getenv(GAME_ENV("SB_NOIRQ")) != NULL;
    return no_irq;
}

/* Called from SR_CheckTimer (Game_dostimer.c): deliver IRQ 5 when the auto-init block wraps. */
void Game_DosSbDeliverIrq(void)
{
    uint32_t position;
    uint64_t now;

    if (!playing || play_length == 0 || dsp_block_size == 0) return;
    if (sb_disabled()) return;
    if (sb_no_irq()) return;
    if (!Game_DosGuestInterruptsEnabled()) return;
    if (dsp_irq_pending) return;

    now = SDL_GetPerformanceCounter();
    uint32_t block_interval_us = dsp_block_size * 1000000u / play_rate;
    uint64_t threshold = block_interval_us * SDL_GetPerformanceFrequency() / 1000000u;
    if (now - irq_last_stamp < threshold) return;

    SDL_LockMutex(audio_lock);
    position = play_position;
    SDL_UnlockMutex(audio_lock);

    if (position / dsp_block_size != irq_last_position / dsp_block_size) {
        irq_last_position = position;
        dsp_irq_pending = 1;

        const uint8_t vector = (uint8_t)(0x08 + sb_irq);
        if (Game_DosGetVector(vector) != 0) {
            int ran = Game_CallGuestInterrupt(vector);
            if (trace_sb()) fprintf(stderr, "SB irq %02x: handler %08x, ran=%d\n", vector, Game_DosGetVector(vector), ran);
        } else {
            /* No valid handler installed: advance rate limiter so the next
             * block boundary can fire without starving the main loop. */
        }
        irq_last_stamp = SDL_GetPerformanceCounter();
    }
}

void Game_DosSbShutdown(void)
{
    Game_AudioSourceClose(audio_source);
    audio_source = NULL;
    playing = 0;
}

/* The BLASTER variable the game's driver reads to find the card. */
const char *Game_DosBlaster(void)
{
    static char blaster[32];

    if (blaster[0] == 0) snprintf(blaster, sizeof(blaster), "BLASTER=A%x I%u D%u T1", SB_BASE, (unsigned int)sb_irq, (unsigned int)SB_DMA);
    return blaster;
}
