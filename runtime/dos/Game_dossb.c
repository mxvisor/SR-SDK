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
 * INT 21h AH=25h, so the game's fixup_interpret_as_code.sci lists it.
 *
 * With GAME_DOS_SB_OPL the card's FM chip is there too, at base+8/9 and at the
 * Ad Lib's 388h/389h: the index, then the data, written to the OPL2 of
 * Game_opl.h (opened at the first write; the driver's own timer writes it, so
 * the chip has no service routine), and the status with the chip's two timers,
 * which a driver runs to find the chip. */

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
#ifdef GAME_DOS_SB_OPL
#include "Game_opl.h"
#endif

#define SB_BASE GAME_SB_BASE
#define SB_DMA GAME_SB_DMA
#define DMA_CHANNELS 4

static int trace_sb(void);

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
static uint32_t play_position;      /* the card's place in the DMA buffer */

/* The samples the card has taken from the DMA buffer, for the output to play
 * (under audio_lock).  The card takes them on the game's thread, when
 * SR_CheckTimer runs, as many as its clock says are due, up to the end of a
 * block, and raises the IRQ there, as the card does: the driver fills the
 * block the card has just played, so what the card takes is always what the
 * driver wrote before.  The output takes them on its own thread, in pieces of
 * its own size, which may be bigger than a block of the driver (a double
 * buffer of 20 ms blocks against the 23 ms of SDL_mixer's buffer): it never
 * reads the DMA buffer itself. */
#define FIFO_SIZE 16384u
static uint8_t fifo[FIFO_SIZE];
static uint32_t fifo_read;
static uint32_t fifo_count;
static uint8_t fifo_last = 0x80;
static unsigned int fifo_underruns;
/* The card's clock: the host's counter, the one the PIT runs on, so that its
 * IRQs keep the time of the timer's (a game that starts a piece of sound at
 * each frame of the timer and has the card play it out in whole blocks needs
 * the two in step).  The output takes the samples at the sound device's rate;
 * the two clocks differ a little, and the output reads the FIFO a little
 * faster or slower (1 % at most) to keep it at its level. */
static uint64_t card_origin;            /* the counter at the anchor */
static uint64_t card_taken_at_origin;
static uint64_t card_taken;             /* samples the card has taken */
static int out_primed;
static int32_t out_error;               /* the FIFO's level off its mark, averaged, 1/256 samples */
static uint32_t out_fraction;           /* 16.16 */
static double out_drift;                /* the FIFO's error over time, sample-seconds */
static int32_t out_adjust;              /* the last step's difference, for the trace */
static uint32_t out_mark;
/* the card is at the end of a block, its IRQ not taken yet */
static int irq_due;
static uint64_t irq_due_stamp;

#ifdef GAME_DOS_SB_OPL
/* The FM chip: the index register, the timers (02h: timer 1, 80 us a step;
 * 03h: timer 2, 320 us a step; 04h: their control) and the status. */
static uint8_t opl_index;
static uint8_t opl_timer[2];
static uint8_t opl_control;
static uint8_t opl_status;
static uint64_t opl_timer_start[2];
static int opl_open;

static uint8_t opl_read_status(void)
{
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t frequency = SDL_GetPerformanceFrequency();

    for (unsigned int timer = 0; timer < 2; timer++) {
        const uint8_t flag = (uint8_t)(0x40u >> timer);
        uint64_t period;

        if (!(opl_control & (1u << timer)) || (opl_control & flag)) continue;
        period = (uint64_t)(256u - opl_timer[timer]) * (timer == 0 ? 80u : 320u) * frequency / 1000000u;
        if (now - opl_timer_start[timer] >= period) opl_status |= (uint8_t)(0x80u | flag);
    }
    /* bits 1-2 read as 1 on an OPL2 */
    return (uint8_t)(opl_status | 0x06u);
}

static void opl_write(uint8_t value)
{
    switch (opl_index) {
        case 0x02: opl_timer[0] = value; break;
        case 0x03: opl_timer[1] = value; break;
        case 0x04:
            if (value & 0x80u) {
                opl_status = 0;
                break;
            }
            for (unsigned int timer = 0; timer < 2; timer++) {
                if ((value & (1u << timer)) && !(opl_control & (1u << timer))) {
                    opl_timer_start[timer] = SDL_GetPerformanceCounter();
                }
            }
            opl_control = value;
            break;
        default: break;
    }
    /* the audio device may come later than the first write */
    if (!opl_open) {
        opl_open = Game_OplOpen(NULL, NULL, 1);
        if (!opl_open) return;
        Game_OplDcBlock();
    }
    Game_OplWriteTimed(opl_index, value);
}

/* 1 when the port is the FM chip's */
static int opl_port(uint16_t port)
{
    return port == SB_BASE + 8u || port == SB_BASE + 9u || port == 0x388u || port == 0x389u;
}
#endif

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

/* What the card keeps ahead of the output: a piece of the output and 60 ms
 * (the output's thread may take several pieces at once). */
static uint32_t fifo_target(void)
{
    const uint32_t target = (uint32_t)audio_samples + play_rate * 60u / 1000u;
    return target < FIFO_SIZE ? target : FIFO_SIZE;
}

/* The output: the samples the card has taken, read a little faster or slower
 * as the FIFO is above or below its mark (the control below), between two
 * samples in 16 bits, as DOSBox's mixer resamples: a sample skipped or given
 * twice clicks, and one cut back to 8 bits adds its own noise.  It starts once
 * the FIFO has reached the mark; short of samples, it holds the last one (no
 * click), or the silence when the card has stopped, and starts again from the
 * mark. */
static void SDLCALL audio_callback(void *userdata, Uint8 *bytes, int size)
{
    const uint32_t mark = fifo_target();
    int16_t *stream = (int16_t *)bytes;
    const int length = size / 2;
    int index = 0;

    (void)userdata;
    SDL_LockMutex(audio_lock);
    if (!out_primed && playing && fifo_count >= mark) {
        out_primed = 1;
        out_error = 0;
        out_fraction = 0;
    }
    if (out_primed) {
        /* A PI control of the FIFO's level by the reading speed, critically
         * damped at 0.1 rad/s: the clocks' difference (millionths) is learnt
         * in a minute or so, and the jitter of the level, averaged, moves the
         * speed by hundredths of a percent. */
        const double w = 0.1;
        const double rate = (double)(play_rate != 0 ? play_rate : 1u);
        double error, speed;
        int32_t step, adjust;

        out_error += (((int32_t)fifo_count - (int32_t)mark) * 256 - out_error) / 64;
        error = out_error / 256.0;
        out_drift += error * length / rate;
        speed = (2.0 * w * error + w * w * out_drift) / rate;
        if (speed > 0.01 || speed < -0.01) {
            out_drift -= error * length / rate;     /* no wind-up */
            speed = speed > 0 ? 0.01 : -0.01;
        }
        adjust = (int32_t)(speed * 65536.0);
        step = 65536 + adjust;
        out_adjust = adjust;
        out_mark = mark;
        for (; index < length; index++) {
            if (fifo_count == 0) {
                if (playing) fifo_underruns++;
                out_primed = 0;
                break;
            }
            const int s0 = fifo[fifo_read];
            const int s1 = fifo_count > 1 ? fifo[(fifo_read + 1u) % FIFO_SIZE] : s0;

            stream[index] = (int16_t)((s0 - 0x80) * 256 + (((s1 - s0) * (int32_t)out_fraction) >> 8));
            fifo_last = (uint8_t)s0;
            out_fraction += (uint32_t)step;
            while (out_fraction >= 65536u && fifo_count != 0) {
                out_fraction -= 65536u;
                fifo_read = (fifo_read + 1u) % FIFO_SIZE;
                fifo_count--;
            }
        }
    }
    if (!playing && fifo_count == 0) fifo_last = 0x80;
    for (; index < length; index++) stream[index] = (int16_t)((fifo_last - 0x80) * 256);
    SDL_UnlockMutex(audio_lock);
}

/* The card's clock starts (again) from now: at the start, at a new rate, after
 * a pause. */
static void card_anchor(void)
{
    card_origin = SDL_GetPerformanceCounter();
    card_taken_at_origin = card_taken;
}

/* The card takes from the DMA buffer the samples its clock says are due, up to
 * the end of the block it is in; 1 when it got there (its IRQ is due). */
static int sb_capture(void)
{
    const uint8_t *buffer = Game_DosMemPtr(play_start, play_length);
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t due = card_taken_at_origin + (now - card_origin) * current_rate() / SDL_GetPerformanceFrequency();
    uint32_t need, to_end, count;

    if (due <= card_taken) return 0;
    /* the game stood still for half a second: the time is not caught up */
    if (due - card_taken > current_rate() / 2u) {
        card_anchor();
        return 0;
    }
    need = (uint32_t)(due - card_taken);
    SDL_LockMutex(audio_lock);
    to_end = dsp_block_size - play_position % dsp_block_size;
    if (to_end > play_length - play_position) to_end = play_length - play_position;
    count = need < to_end ? need : to_end;
    for (uint32_t index = 0; index < count; index++) {
        const uint8_t value = (buffer != NULL && dsp_speaker)
            ? (uint8_t)(0x80 + ((buffer[play_position] - 0x80) * mixer_gain >> 8)) : 0x80u;
        if (fifo_count == FIFO_SIZE) {
            fifo_read = (fifo_read + 1u) % FIFO_SIZE;
            fifo_count--;
        }
        fifo[(fifo_read + fifo_count) % FIFO_SIZE] = value;
        fifo_count++;
        card_taken++;
        if (++play_position >= play_length) play_position = 0;
    }
    SDL_UnlockMutex(audio_lock);
    return count == to_end;
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
    want.format = AUDIO_S16SYS;
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
    irq_due = 0;
    playing = 1;
    card_anchor();
    SDL_UnlockMutex(audio_lock);
    if (trace_sb()) fprintf(stderr, "SB start: buffer %05x, %u bytes, block %u, %u Hz\n",
                            play_start, play_length, dsp_block_size, current_rate());
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
    fifo_count = 0;
    irq_due = 0;
    out_primed = 0;
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
        case 0x40:
            dsp_time_constant = dsp_params[0];
            card_anchor();
            /* a driver pauses (D0h), sets the rate and continues (D4h) */
            if (audio_source != NULL) audio_open();
            break;
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
            if (play_length != 0 && !playing) {
                card_anchor();
                playing = 1;
            }
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
 * have to move with the transfer: they are where the card takes its samples
 * (sb_capture), the place its IRQs come from. */
static uint32_t sb_position(void)
{
    if (play_length == 0) return 0;
    return play_position;
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
#ifdef GAME_DOS_SB_OPL
    if (opl_port(port)) {
        if (sb_disabled()) return 0;
        *value = (port & 1u) ? 0xffu : opl_read_status();
        return 1;
    }
#endif
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

#ifdef GAME_DOS_SB_OPL
    if (opl_port(port)) {
        if (sb_disabled()) return 0;
        if (port & 1u) opl_write((uint8_t)value);
        else opl_index = (uint8_t)value;
        return 1;
    }
#endif

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

/* Called from SR_CheckTimer (Game_dostimer.c): the card takes the samples
 * that are due from the DMA buffer, and raises its IRQ at the end of each
 * block.  It waits at the end of a block while the guest's interrupts are off,
 * or while the IRQ before is not acknowledged (base+0Eh), 100 ms at most. */
void Game_DosSbDeliverIrq(void)
{
    if (!playing || play_length == 0 || dsp_block_size == 0 || audio_source == NULL) return;
    if (sb_disabled()) return;

    for (unsigned int round = 0; round < 16; round++) {
        if (irq_due) {
            if (!sb_no_irq()) {
                const uint8_t vector = (uint8_t)(0x08 + sb_irq);
                const uint64_t now = SDL_GetPerformanceCounter();
                int ran;

                if (dsp_irq_pending && now - irq_due_stamp < SDL_GetPerformanceFrequency() / 10u) return;
                if (!Game_DosGuestInterruptsEnabled()) return;
                irq_due = 0;
                dsp_irq_pending = 1;
                ran = Game_DosHardwareInterrupt(vector);
                if (trace_sb()) fprintf(stderr, "SB irq %02x: handler %08x, ran=%d, ahead %u (mark %u, off %d), speed %+d/65536, underruns %u\n",
                                        vector, Game_DosGetVector(vector), ran, fifo_count, out_mark, out_error / 256, out_adjust, fifo_underruns);
            } else {
                irq_due = 0;
            }
            /* the handler may have stopped or restarted the card */
            if (!playing || play_length == 0 || dsp_block_size == 0 || irq_due) return;
        }
        if (!sb_capture()) return;
        irq_due = 1;
        irq_due_stamp = SDL_GetPerformanceCounter();
    }
}

void Game_DosSbShutdown(void)
{
    Game_AudioSourceClose(audio_source);
    audio_source = NULL;
    playing = 0;
#ifdef GAME_DOS_SB_OPL
    if (opl_open) Game_OplClose();
    opl_open = 0;
#endif
}

/* The BLASTER variable the game's driver reads to find the card. */
const char *Game_DosBlaster(void)
{
    static char blaster[32];

    if (blaster[0] == 0) snprintf(blaster, sizeof(blaster), "BLASTER=A%x I%u D%u T1", SB_BASE, (unsigned int)sb_irq, (unsigned int)SB_DMA);
    return blaster;
}
