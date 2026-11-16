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

/* The drivers of the Miles AIL/32 2.x (the Audio Interface Library before the
 * Miles Sound System) a DOS4GW game was linked with, over the SDK's AIL
 * (core/Game_ail.c).
 *
 * The game loads a driver (an .ADV or a DLL) into its memory itself and hands
 * it to AIL_register_driver; every other call of the API is a stub that puts
 * the driver function's number into EAX and jumps to one dispatcher, which
 * looks the number up in the driver's table of {number, address} pairs and
 * jumps to the driver's code.  That code cannot run here, so the game's
 * external_procedures.sci routes the two (register and dispatch) to the
 * SR_AIL2_* procedures of llasm/ail2.llinc, and the drivers are these: a
 * digital one (driver type 2, the .VOC player of a Sound Blaster) on a sample
 * of Game_ail, and an XMIDI one (type 3) on Game_ail's sequences.  The kind of
 * a registered driver is the kind of the functions its table lists.  The
 * numbers, the descriptor and the states are those of AIL 2.14's AIL.H and
 * AIL.INC (open-source freeware since 2000); every argument and every field
 * of the descriptor is 32 bits wide in AIL/32, and a far pointer of the
 * digital functions is two of them, the offset and the selector.
 *
 * A digital driver of AIL 2 plays one .VOC at a time.  The XMIDI driver needs
 * no timbres (it plays General MIDI through Game_midi): its timbre requests
 * are empty.  The config file's Sound and Music turn the drivers off: the
 * game's AIL_register_driver fails then.  $<GAME>_TRACE_SOUND traces the
 * calls. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"
#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_ail.h"
#include "Game_dosail2.h"

/* the numbers of the driver functions (AIL.INC) */
enum {
    AIL2_DESC_DRVR = 100, AIL2_DET_DEV = 101, AIL2_INIT_DRVR = 102, AIL2_SERVE_DRVR = 103, AIL2_SHUTDOWN_DRVR = 104,

    AIL2_INDEX_VOC_BLK = 120, AIL2_REG_SND_BUFF = 121, AIL2_SND_BUFF_STAT = 122, AIL2_P_VOC_FILE = 123,
    AIL2_VOC_PB_STAT = 124, AIL2_START_D_PB = 125, AIL2_STOP_D_PB = 126, AIL2_PAUSE_D_PB = 127,
    AIL2_RESUME_D_PB = 128, AIL2_SET_D_PB_VOL = 129, AIL2_D_PB_VOL = 130, AIL2_SET_D_PB_PAN = 131,
    AIL2_D_PB_PAN = 132, AIL2_F_VOC_FILE = 133, AIL2_F_SND_BUFF = 134,

    AIL2_STATE_TAB_SIZE = 150, AIL2_REG_SEQ = 151, AIL2_REL_SEQ_HND = 152, AIL2_T_CACHE_SIZE = 153,
    AIL2_DEFINE_T_CACHE = 154, AIL2_T_REQ = 155, AIL2_INSTALL_T = 156, AIL2_PROTECT_T = 157,
    AIL2_UNPROTECT_T = 158, AIL2_T_STATUS = 159,
    AIL2_START_SEQ = 170, AIL2_STOP_SEQ = 171, AIL2_RESUME_SEQ = 172, AIL2_SEQ_STAT = 173,
    AIL2_REL_VOL = 174, AIL2_REL_TEMPO = 175, AIL2_SET_REL_VOL = 176, AIL2_SET_REL_TEMPO = 177,
    AIL2_LAST = 192
};

/* drvr_desc.drvr_type */
#define DSP_DRVR    2
#define XMIDI_DRVR  3

/* the states of AIL_VOC_playback_status and AIL_sequence_status */
#define DAC_STOPPED 0
#define DAC_PAUSED  1
#define DAC_PLAYING 2
#define DAC_DONE    3
#define SEQ_STOPPED 0
#define SEQ_PLAYING 1
#define SEQ_DONE    2

#define AIL2_DRIVERS 16

/* drvr_desc of AIL/32 */
typedef struct {
    uint32_t min_API_version;
    uint32_t drvr_type;
    char data_suffix[4];
    uint32_t dev_name_table;
    int32_t default_IO;
    int32_t default_IRQ;
    int32_t default_DMA;
    int32_t default_DRQ;
    int32_t service_rate;
    uint32_t display_size;
} ail2_desc;

typedef struct {
    int type;               /* 0: a free handle */
    ail2_desc desc;
    /* digital: the sample of Game_ail, the .VOC block to play next, the state */
    int sample;
    uint32_t voc_block;
    int voc_state;
    uint8_t *voc_pcm;       /* the voice data of several blocks, joined */
    int32_t volume, pan;
} ail2_driver;

static ail2_driver drivers[AIL2_DRIVERS];

static const char dsp_names[] = "Sound Blaster Digital Sound\0";
static const char xmidi_names[] = "General MIDI Synthesizer\0";

static int trace_sound(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_SOUND")) != NULL;
    return enabled;
}

static const uint8_t *guest(uint32_t address)
{
    return (const uint8_t *)GAME_GUEST32_TO_PTR(address);
}

static uint32_t read32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, guest(address), 4);
    return value;
}

static uint32_t read24(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* The kind of a driver by the functions its table lists: pairs of 32-bit
 * {number, address}, a number of -1 at the end. */
static int driver_type(uint32_t table)
{
    unsigned int index;
    int type = 0;

    for (index = 0; index < 256; index++) {
        const uint32_t function = read32(table + index * 8);
        if (function == 0xffffffffu) break;
        if (function >= AIL2_INDEX_VOC_BLK && function <= AIL2_F_SND_BUFF) type = DSP_DRVR;
        else if (function >= AIL2_STATE_TAB_SIZE && function <= AIL2_LAST) type = XMIDI_DRVR;
    }
    return type;
}

/* digital: the .VOC file's blocks from `block` on, played as one sample */

static void voc_stop(ail2_driver *d)
{
    if (d->sample >= 0) Game_AilEndSample(d->sample);
    free(d->voc_pcm);
    d->voc_pcm = NULL;
}

/* Collects the 8-bit PCM of the voice blocks from d->voc_block up to a
 * terminator or a marker, and starts it.  0 if there is nothing to play. */
static int voc_start(ail2_driver *d)
{
    const uint8_t *block = guest(d->voc_block);
    const uint8_t *first = NULL;
    uint32_t first_length = 0, total = 0, rate = 0, used = 0;
    int pieces = 0, stereo = 0, xblk = 0, pass;
    uint32_t xblk_rate = 0;

    /* two passes: measure, then copy (only when the voice is in pieces) */
    for (pass = 0; pass < 2; pass++) {
        const uint8_t *p = block;
        int guard;
        if (pass == 1) {
            if (pieces <= 1) break;
            d->voc_pcm = (uint8_t *)malloc(total);
            if (d->voc_pcm == NULL) return 0;
        }
        for (guard = 0; guard < 65536; guard++) {
            const uint8_t type = p[0];
            const uint32_t length = type == 0 ? 0 : read24(p + 1);
            const uint8_t *data = NULL;
            uint32_t size = 0;

            if (type == 0 || type == 4) break;     /* terminator, marker */
            if (type == 1) {                        /* new voice: time constant, packing */
                if (p[5] != 0 && !xblk) break;      /* only 8-bit PCM */
                if (pass == 0) {
                    if (xblk) rate = xblk_rate;
                    else rate = 1000000u / (256u - p[4]);
                }
                xblk = 0;
                data = p + 6;
                size = length - 2;
            } else if (type == 2) {                 /* continuation */
                data = p + 4;
                size = length;
            } else if (type == 8) {                 /* extended: the next voice block's format */
                const uint32_t constant = p[4] | ((uint32_t)p[5] << 8);
                if (p[6] != 0) break;
                if (pass == 0) {
                    stereo = p[7] != 0;
                    xblk_rate = 256000000u / (65536u - constant) / (stereo ? 2u : 1u);
                }
                xblk = 1;
            } else if (type == 9) {                 /* new voice, the format in full */
                const uint32_t samples = read32(GAME_PTR_TO_GUEST32(p + 4));
                if (p[8] != 8 || (p[12] | p[13]) != 0) break;
                if (pass == 0) {
                    rate = samples;
                    stereo = p[9] == 2;
                }
                data = p + 16;
                size = length - 12;
            }
            if (data != NULL && size != 0) {
                if (pass == 0) {
                    if (pieces++ == 0) { first = data; first_length = size; }
                    total += size;
                } else {
                    memcpy(d->voc_pcm + used, data, size);
                    used += size;
                }
            }
            p += 4 + length;
        }
    }
    if (total == 0 || rate == 0) return 0;

    Game_AilInitSample(d->sample);
    Game_AilSetSampleType(d->sample, stereo ? GAME_AIL_F_STEREO : 0, 0);
    Game_AilSetSampleRate(d->sample, (int32_t)rate);
    Game_AilSetSampleAddress(d->sample, pieces > 1 ? d->voc_pcm : first, pieces > 1 ? total : first_length);
    Game_AilSetSampleVolume(d->sample, d->volume);
    Game_AilSetSamplePan(d->sample, d->pan);
    Game_AilSetSampleLoopCount(d->sample, 1);
    Game_AilStartSample(d->sample);
    if (trace_sound()) fprintf(stderr, "AIL2: VOC %08x: %u bytes in %i blocks at %u Hz\n", d->voc_block, total, pieces, rate);
    return 1;
}

/* AIL_play_VOC_file: the first block, or the one after the marker `marker` */
static void voc_select(ail2_driver *d, uint32_t file, int32_t marker)
{
    const uint8_t *p = guest(file);
    uint32_t block = file + (p[0x14] | ((uint32_t)p[0x15] << 8));
    int guard;

    voc_stop(d);
    d->voc_state = DAC_DONE;
    if (marker != -1) {
        for (guard = 0; guard < 65536; guard++) {
            const uint8_t *b = guest(block);
            const int32_t number = b[0] == 4 ? (int32_t)(b[4] | (b[5] << 8)) : -1;
            if (b[0] == 0) return;
            block += 4 + read24(b + 1);
            if (number == marker) break;
        }
    }
    d->voc_block = block;
    d->voc_state = DAC_STOPPED;
}

static uint32_t voc_status(ail2_driver *d)
{
    if (d->voc_state == DAC_PLAYING && (Game_AilSampleStatus(d->sample) & GAME_AIL_DONE)) d->voc_state = DAC_DONE;
    return (uint32_t)d->voc_state;
}

static uint32_t digital_call(ail2_driver *d, uint32_t function, const uint32_t *args)
{
    switch (function) {
    case AIL2_P_VOC_FILE:
        /* AIL/32: the file as a far pointer (offset, selector), as the real
         * mode address of the DMA (the file is in DOS memory), the marker */
        voc_select(d, args[1], (int32_t)args[4]);
        return 0;
    case AIL2_START_D_PB:
        if (d->voc_state == DAC_STOPPED) d->voc_state = voc_start(d) ? DAC_PLAYING : DAC_DONE;
        return 0;
    case AIL2_STOP_D_PB:
        voc_stop(d);
        d->voc_state = DAC_STOPPED;
        return 0;
    case AIL2_PAUSE_D_PB:
        if (voc_status(d) == DAC_PLAYING) {
            Game_AilStopSample(d->sample);
            d->voc_state = DAC_PAUSED;
        }
        return 0;
    case AIL2_RESUME_D_PB:
        if (d->voc_state == DAC_PAUSED) {
            Game_AilResumeSample(d->sample);
            d->voc_state = DAC_PLAYING;
        }
        return 0;
    case AIL2_VOC_PB_STAT:
        return voc_status(d);
    case AIL2_SET_D_PB_VOL:
        d->volume = (int32_t)(args[1] & 127);
        Game_AilSetSampleVolume(d->sample, d->volume);
        return 0;
    case AIL2_D_PB_VOL:
        return (uint32_t)d->volume;
    case AIL2_SET_D_PB_PAN:
        d->pan = (int32_t)(args[1] & 127);
        Game_AilSetSamplePan(d->sample, d->pan);
        return 0;
    case AIL2_D_PB_PAN:
        return (uint32_t)d->pan;
    case AIL2_F_VOC_FILE:
    case AIL2_F_SND_BUFF:
        /* the Sound Blaster plays 8-bit PCM as it is */
        return 0;
    case AIL2_SND_BUFF_STAT:
        return DAC_DONE;
    default:
        /* the double buffers (AIL_index_VOC_block, AIL_register_sound_buffer):
         * no game here plays them yet */
        if (trace_sound()) fprintf(stderr, "AIL2: digital function %u not supported\n", function);
        return 0;
    }
}

/* XMIDI: a sequence handle is the number of Game_ail's sequence */
static uint32_t xmidi_call(ail2_driver *d, uint32_t function, const uint32_t *args)
{
    const int sequence = (int)args[1];
    (void)d;

    switch (function) {
    case AIL2_STATE_TAB_SIZE:
        /* the game allocates a state table per sequence; this driver keeps none */
        return 16;
    case AIL2_REG_SEQ: {
        const int number = Game_AilAllocateSequence();
        if (number < 0) return 0xffffffffu;
        if (!Game_AilInitSequence(number, guest(args[1]), args[2])) {
            Game_AilReleaseSequence(number);
            return 0xffffffffu;
        }
        if (trace_sound()) fprintf(stderr, "AIL2: sequence %i: XMIDI %08x, number %u\n", number, args[1], args[2]);
        return (uint32_t)number;
    }
    case AIL2_REL_SEQ_HND:
        Game_AilReleaseSequence(sequence);
        return 0;
    case AIL2_T_CACHE_SIZE:
        return 0;
    case AIL2_T_REQ:
        return 0xffffu;
    case AIL2_T_STATUS:
        return 1;
    case AIL2_DEFINE_T_CACHE:
    case AIL2_INSTALL_T:
    case AIL2_PROTECT_T:
    case AIL2_UNPROTECT_T:
        return 0;
    case AIL2_START_SEQ:
        Game_AilStartSequence(sequence);
        return 0;
    case AIL2_STOP_SEQ:
        Game_AilStopSequence(sequence);
        return 0;
    case AIL2_RESUME_SEQ:
        Game_AilResumeSequence(sequence);
        return 0;
    case AIL2_SEQ_STAT: {
        const uint32_t status = Game_AilSequenceStatus(sequence);
        if (status & GAME_AIL_PLAYING) return SEQ_PLAYING;
        if (status & GAME_AIL_STOPPED) return SEQ_STOPPED;
        return SEQ_DONE;
    }
    case AIL2_REL_VOL:
    case AIL2_REL_TEMPO:
        return 100;
    case AIL2_SET_REL_VOL:
    case AIL2_SET_REL_TEMPO:
        return 0;
    default:
        if (trace_sound()) fprintf(stderr, "AIL2: XMIDI function %u not supported\n", function);
        return 0;
    }
}

extern "C" uint32_t Game_AIL2_RegisterDriver(uint32_t image)
{
    const uint8_t *p = guest(image);
    ail2_driver *d;
    int handle, type;

    if (image == 0 || memcmp(p + 4, "Copy", 4) != 0) return 0xffffffffu;
    type = driver_type(read32(image));
    if (trace_sound()) fprintf(stderr, "AIL2: driver %08x: type %i\n", image, type);
    if (type == DSP_DRVR ? !Game_Sound : type == XMIDI_DRVR ? !Game_Music : 1) return 0xffffffffu;
    for (handle = 0; handle < AIL2_DRIVERS && drivers[handle].type != 0; handle++) {}
    if (handle == AIL2_DRIVERS) return 0xffffffffu;
    if (type == DSP_DRVR && (drivers[handle].sample = Game_AilAllocateSample()) < 0) return 0xffffffffu;

    d = &drivers[handle];
    d->type = type;
    d->voc_state = DAC_DONE;
    d->volume = 127;
    d->pan = 64;
    memset(&d->desc, 0, sizeof(d->desc));
    d->desc.min_API_version = 200;
    d->desc.drvr_type = (uint32_t)type;
    /* the file of the timbres the game loads for the driver ("AD": Ad Lib's
     * .AD, which the XMIDI drivers on OPL read); the timbre requests here are
     * empty, so it is never read */
    memcpy(d->desc.data_suffix, type == DSP_DRVR ? "VOC" : "AD", type == DSP_DRVR ? 4 : 3);
    d->desc.dev_name_table = GAME_PTR_TO_GUEST32(type == DSP_DRVR ? dsp_names : xmidi_names);
    d->desc.default_IO = type == DSP_DRVR ? 0x220 : 0x388;
    d->desc.default_IRQ = type == DSP_DRVR ? 7 : -1;
    d->desc.default_DMA = type == DSP_DRVR ? 1 : -1;
    d->desc.default_DRQ = -1;
    d->desc.service_rate = -1;
    return (uint32_t)handle;
}

extern "C" uint32_t Game_AIL2_CallDriver(uint32_t function, uint32_t arguments)
{
    const uint32_t handle = read32(arguments);
    uint32_t args[6];
    ail2_driver *d;
    uint32_t result = 0;
    unsigned int index;

    if (handle >= AIL2_DRIVERS || drivers[handle].type == 0) return 0;
    d = &drivers[handle];
    for (index = 0; index < sizeof(args) / sizeof(args[0]); index++) args[index] = read32(arguments + index * 4);

    switch (function) {
    case AIL2_DESC_DRVR:
        result = GAME_PTR_TO_GUEST32(&d->desc);
        break;
    case AIL2_DET_DEV:
        result = 1;
        break;
    case AIL2_INIT_DRVR:
    case AIL2_SERVE_DRVR:
        break;
    case AIL2_SHUTDOWN_DRVR:
        if (d->type == DSP_DRVR) {
            voc_stop(d);
            Game_AilReleaseSample(d->sample);
        }
        d->type = 0;
        break;
    default:
        result = d->type == DSP_DRVR ? digital_call(d, function, args) : xmidi_call(d, function, args);
        break;
    }
    if (trace_sound() && function != AIL2_VOC_PB_STAT && function != AIL2_SEQ_STAT)
        fprintf(stderr, "AIL2: %u(%u, %08x, %08x, %08x) = %08x\n", function, handle, args[1], args[2], args[3], result);
    return result;
}
