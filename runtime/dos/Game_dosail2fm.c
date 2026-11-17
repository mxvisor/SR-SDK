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


/* The Ad Lib XMIDI driver of AIL 2 (a32adlib, adlib.adv): the XMIDI
 * interpreter of XMIDI.ASM and the synthesizer of YAMAHA.INC for a YM3812, as
 * AIL 2.14 by John Miles (Copyright (C) 1991, 1992 Miles Design, Inc.,
 * open-source freeware since 2000: NOTICE.md) has them, ported to C, on the
 * OPL2 of Game_opl.c.  Game_dosail2.c registers it and hands it the calls of
 * the driver's functions.
 *
 * The interpreter runs on the audio thread, 120 times a second of the music
 * (QUANT_RATE: the service rate the driver asks AIL for); the calls of the
 * game take Game_OplLock, as the driver's procedures disabled the interrupts.
 *
 * The sequences' states are kept here, not in the tables the game allocates
 * for them; the timbres are in the game's cache (AIL_define_timbre_cache), as
 * the driver had them.  What the port leaves out: the callbacks of
 * AIL_install_callback (the game's code cannot run on the audio thread: the
 * trigger's value can still be polled), the 4-operator voices of the OPL3
 * drivers and the TVFX timbres of Origin's version.
 *
 * Names of procedures and variables are those of the assembly sources, so the
 * two can be read side by side. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"
#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_opl.h"
#include "Game_dosail2fm.h"

/* XMIDI.ASM */
#define QUANT_RATE      120
#define QUANT_TIME      8333            /* us per interval */
#define QUANT_TIME_16   133333          /* 16,000,000 / QUANT_RATE */
#define MAX_NOTES       32
#define FOR_NEST        4
#define NSEQS           8
#define QUANT_ADVANCE   1
#define DEF_PITCH_L     0x00
#define DEF_PITCH_H     0x40
#define NUM_CHANS       16

/* YAMAHA.INC, YM3812 */
#define MAX_REC_CHAN    10
#define MAX_TRUE_CHAN   9
#define MIN_TRUE_CHAN   2
#define DEF_SYNTH_VOL   100
#define MAX_TIMBS       192
#define DEF_TC_SIZE     3584
#define NUM_VOICES      9
#define NUM_SLOTS       16
#define DEF_PITCH_RANGE 12
#define DEF_AV_DEPTH    0xc0

/* AIL.INC */
#define SEQ_STOPPED     0
#define SEQ_PLAYING     1
#define SEQ_DONE        2

#define MODULATION      1
#define PART_VOLUME     7
#define PANPOT          10
#define EXPRESSION      11
#define SUSTAIN         64
#define CHAN_LOCK       110
#define CHAN_PROTECT    111
#define VOICE_PROTECT   112
#define TIMBRE_PROTECT  113
#define PATCH_BANK_SEL  114
#define INDIRECT_C_PFX  115
#define FOR_LOOP        116
#define NEXT_LOOP       117
#define CLEAR_BEAT_BAR  118
#define CALLBACK_TRIG   119
#define RESET_ALL_CTRLS 121
#define ALL_NOTES_OFF   123

enum {
    AIL_INIT_DRVR = 102, AIL_SERVE_DRVR = 103, AIL_SHUTDOWN_DRVR = 104,
    AIL_STATE_TAB_SIZE = 150, AIL_REG_SEQ = 151, AIL_REL_SEQ_HND = 152, AIL_T_CACHE_SIZE = 153,
    AIL_DEFINE_T_CACHE = 154, AIL_T_REQ = 155, AIL_INSTALL_T = 156, AIL_PROTECT_T = 157,
    AIL_UNPROTECT_T = 158, AIL_T_STATUS = 159,
    AIL_START_SEQ = 170, AIL_STOP_SEQ = 171, AIL_RESUME_SEQ = 173, AIL_SEQ_STAT = 174,
    AIL_REL_VOL = 175, AIL_REL_TEMPO = 176, AIL_SET_REL_VOL = 177, AIL_SET_REL_TEMPO = 178,
    AIL_BEAT_CNT = 179, AIL_BAR_CNT = 180, AIL_BRA_INDEX = 181, AIL_CON_VAL = 182,
    AIL_SET_CON_VAL = 183, AIL_CHAN_NOTES = 185, AIL_SEND_CV_MSG = 186, AIL_SEND_SYSEX_MSG = 187,
    AIL_WRITE_DISP = 188, AIL_INSTALL_CB = 189, AIL_CANCEL_CB = 190, AIL_LOCK_CHAN = 191,
    AIL_MAP_SEQ_CHAN = 192, AIL_RELEASE_CHAN = 193, AIL_TRUE_SEQ_CHAN = 194
};

/* ---------------------------------------------------------------------------
 * XMIDI.ASM: the data
 * ------------------------------------------------------------------------- */

/* the controllers logged in the state tables (ctrl_log) */
enum { PV, MODUL, PAN, EXP, SUS, PBS, C_LOCK, C_PROT, V_PROT, NUM_CONTROLS };
static const uint8_t logged_ctrls[NUM_CONTROLS] = {
    PART_VOLUME, MODULATION, PANPOT, EXPRESSION, SUSTAIN, PATCH_BANK_SEL, CHAN_LOCK, CHAN_PROTECT, VOICE_PROTECT
};
static const uint8_t ctrl_default[NUM_CONTROLS] = { 127, 0, 64, 127, 0, 0, 0, 0, 0 };
static const uint8_t prg_default[9] = { 68, 48, 95, 78, 41, 3, 110, 122, 0xff };   /* (Roland defaults) */
static int8_t ctrl_hash[256];           /* the index of a logged controller, -1 */

typedef struct {
    int registered;
    uint32_t TIMB, RBRN, EVNT;          /* guest addresses of the chunks, 0 for none */
    uint32_t EVNT_ptr;
    int32_t cur_callback;
    uint32_t ctrl_ptr;
    int seq_started, status, post_release;
    int32_t interval_cnt, note_count;
    int32_t vol_percent, vol_target;
    int32_t vol_accum, vol_period;
    int32_t tempo_error, tempo_percent, tempo_target;
    int32_t tempo_accum, tempo_period;
    int32_t beat_count, measure_count, time_numerator;
    int32_t time_fraction, beat_fraction, time_per_beat;
    uint32_t FOR_ptrs[FOR_NEST];
    int32_t FOR_loop_cnt[FOR_NEST];
    uint8_t chan_map[NUM_CHANS], chan_program[NUM_CHANS];
    uint8_t chan_pitch_l[NUM_CHANS], chan_pitch_h[NUM_CHANS], chan_indirect[NUM_CHANS];
    uint8_t chan_controls[NUM_CONTROLS][NUM_CHANS];
    uint8_t note_chan[MAX_NOTES], note_num[MAX_NOTES];
    int32_t note_time[MAX_NOTES];
} state_table;

static state_table sequence_state[NSEQS];
static int current_handle;
static int service_active;
static int init_OK;
static int callback_warned;

static uint8_t global_controls[NUM_CONTROLS][NUM_CHANS];
static uint8_t global_program[NUM_CHANS], global_pitch_l[NUM_CHANS], global_pitch_h[NUM_CHANS];
static uint8_t active_notes[NUM_CHANS];
static uint8_t lock_status[NUM_CHANS];  /* bit 7: locked, 6: lock-protected */

/* ---------------------------------------------------------------------------
 * YAMAHA.INC: the data
 * ------------------------------------------------------------------------- */

static const uint16_t freq_table[192] = {
    0x02b2,0x02b4,0x02b7,0x02b9,0x02bc,0x02be,0x02c1,0x02c3,0x02c6,0x02c9,
    0x02cb,0x02ce,0x02d0,0x02d3,0x02d6,0x02d8,0x02db,0x02dd,0x02e0,0x02e3,
    0x02e5,0x02e8,0x02eb,0x02ed,0x02f0,0x02f3,0x02f6,0x02f8,0x02fb,0x02fe,
    0x0301,0x0303,0x0306,0x0309,0x030c,0x030f,0x0311,0x0314,0x0317,0x031a,
    0x031d,0x0320,0x0323,0x0326,0x0329,0x032b,0x032e,0x0331,0x0334,0x0337,
    0x033a,0x033d,0x0340,0x0343,0x0346,0x0349,0x034c,0x034f,0x0352,0x0356,
    0x0359,0x035c,0x035f,0x0362,0x0365,0x0368,0x036b,0x036f,0x0372,0x0375,
    0x0378,0x037b,0x037f,0x0382,0x0385,0x0388,0x038c,0x038f,0x0392,0x0395,
    0x0399,0x039c,0x039f,0x03a3,0x03a6,0x03a9,0x03ad,0x03b0,0x03b4,0x03b7,
    0x03bb,0x03be,0x03c1,0x03c5,0x03c8,0x03cc,0x03cf,0x03d3,0x03d7,0x03da,
    0x03de,0x03e1,0x03e5,0x03e8,0x03ec,0x03f0,0x03f3,0x03f7,0x03fb,0x03fe,
    0xfe01,0xfe03,0xfe05,0xfe07,0xfe08,0xfe0a,0xfe0c,0xfe0e,0xfe10,0xfe12,
    0xfe14,0xfe16,0xfe18,0xfe1a,0xfe1c,0xfe1e,0xfe20,0xfe21,0xfe23,0xfe25,
    0xfe27,0xfe29,0xfe2b,0xfe2d,0xfe2f,0xfe31,0xfe34,0xfe36,0xfe38,0xfe3a,
    0xfe3c,0xfe3e,0xfe40,0xfe42,0xfe44,0xfe46,0xfe48,0xfe4a,0xfe4c,0xfe4f,
    0xfe51,0xfe53,0xfe55,0xfe57,0xfe59,0xfe5c,0xfe5e,0xfe60,0xfe62,0xfe64,
    0xfe67,0xfe69,0xfe6b,0xfe6d,0xfe6f,0xfe72,0xfe74,0xfe76,0xfe79,0xfe7b,
    0xfe7d,0xfe7f,0xfe82,0xfe84,0xfe86,0xfe89,0xfe8b,0xfe8d,0xfe90,0xfe92,
    0xfe95,0xfe97,0xfe99,0xfe9c,0xfe9e,0xfea1,0xfea3,0xfea5,0xfea8,0xfeaa,
    0xfead,0xfeaf
};

static const uint8_t vel_graph[16] = { 82,85,88,91,94,97,100,103,106,109,112,115,118,121,124,127 };

static const uint8_t op_0[NUM_VOICES] = { 0,1,2,6,7,8,12,13,14 };
static const uint8_t op_1[NUM_VOICES] = { 3,4,5,9,10,11,15,16,17 };
static const uint8_t op_index[18] = { 0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21 };

/* the registers 01-f5 after reset_synth */
static uint8_t array0_init(unsigned int reg)
{
    if (reg == 0x01) return 0x20;               /* wave select enable */
    if (reg == 0x04) return 0x60;               /* reset the timers */
    if (reg >= 0x20 && reg <= 0x35) return 1;
    if (reg >= 0x40 && reg <= 0x55) return 63;
    if (reg >= 0x60 && reg <= 0x75) return 255;
    if (reg >= 0x80 && reg <= 0x95) return 15;
    if (reg == 0xbd) return DEF_AV_DEPTH;
    return 0;
}

static uint32_t note_event;             /* used for LRU counting */
static uint32_t timb_hist[MAX_TIMBS];   /* last note event count for LRU */
static uint16_t timb_offsets[MAX_TIMBS];/* offsets of resident timbres */
static uint8_t timb_bank[MAX_TIMBS], timb_num[MAX_TIMBS];
static uint8_t timb_attribs[MAX_TIMBS]; /* bit 7=in use 6=protected */

static uint32_t cache_base;             /* timbre cache base addr (the game's) */
static uint32_t cache_size, cache_end;

static int rover_2op;

/* the .BNK timbre, the size first */
enum { B_length = 0, B_transpose = 2, B_mod_AVEKM, B_mod_KSLTL, B_mod_AD, B_mod_SR, B_mod_WS, B_fb_c,
       B_car_AVEKM, B_car_KSLTL, B_car_AD, B_car_SR, B_car_WS, BNK_SIZE };
#define OPL3BNK_SIZE 25

enum { FREE = 0, KEYON = 1, KEYOFF = 2 };
enum { BNK_INST = 0, TV_INST = 1, TV_EFFECT = 2, OPL3_INST = 3 };

#define U_ALL_REGS  0xf9
#define U_AVEKM     0x80
#define U_KSLTL     0x40
#define U_ADSR      0x20
#define U_WS        0x10
#define U_FBC       0x08
#define U_FREQ      0x01

static uint32_t S_timbre[NUM_SLOTS];    /* the timbre in the cache (guest address) */
static uint8_t S_status[NUM_SLOTS], S_type[NUM_SLOTS];
static int8_t S_voice[NUM_SLOTS];
static uint8_t S_channel[NUM_SLOTS], S_note[NUM_SLOTS], S_keynum[NUM_SLOTS];
static int8_t S_transpose[NUM_SLOTS];
static uint8_t S_velocity[NUM_SLOTS], S_sustain[NUM_SLOTS], S_update[NUM_SLOTS];
static uint8_t S_KBF_shadow[NUM_SLOTS], S_BLOCK[NUM_SLOTS], S_FBC[NUM_SLOTS];
static uint8_t S_KSLTL_0[NUM_SLOTS], S_KSLTL_1[NUM_SLOTS], S_AVEKM_0[NUM_SLOTS], S_AVEKM_1[NUM_SLOTS];
static uint8_t S_AD_0[NUM_SLOTS], S_AD_1[NUM_SLOTS], S_SR_0[NUM_SLOTS], S_SR_1[NUM_SLOTS];
static uint8_t S_scale_01[NUM_SLOTS];
static uint16_t S_ws_val[NUM_SLOTS], S_m1_val[NUM_SLOTS], S_m0_val[NUM_SLOTS], S_fb_val[NUM_SLOTS];
static uint16_t S_p_val[NUM_SLOTS], S_v1_val[NUM_SLOTS], S_v0_val[NUM_SLOTS];
static uint16_t S_V_priority[NUM_SLOTS];

static uint8_t MIDI_vol[NUM_CHANS], MIDI_pan[NUM_CHANS], MIDI_pitch_l[NUM_CHANS], MIDI_pitch_h[NUM_CHANS];
static uint8_t MIDI_express[NUM_CHANS], MIDI_mod[NUM_CHANS], MIDI_sus[NUM_CHANS], MIDI_vprot[NUM_CHANS];
static int16_t MIDI_timbre[NUM_CHANS];  /* timbre cache index for each channel, -1 */
static uint8_t MIDI_bank[NUM_CHANS];
static int16_t MIDI_program[NUM_CHANS];
static int16_t RBS_timbres[128];        /* RBS timbre index cache */
static uint8_t MIDI_voices[NUM_CHANS];  /* # of voices assigned to channel */
static int8_t V_channel[NUM_VOICES];    /* MIDI channel of a voice, -1 */

static void send_MIDI_message(unsigned int stat, unsigned int d1, unsigned int d2);
static void release_channel(int chan);

static int output_open;                 /* the OPL2: between Game_AIL2_FmOpen and FmClose */
static int lock_channel(void);

/* ---------------------------------------------------------------------------
 * the game's memory
 * ------------------------------------------------------------------------- */

static uint8_t *guest(uint32_t address)
{
    return (uint8_t *)GAME_GUEST32_TO_PTR(address);
}

static uint8_t byte_at(uint32_t address)
{
    return *guest(address);
}

static uint16_t word_at(uint32_t address)
{
    const uint8_t *p = guest(address);
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t be32_at(uint32_t address)
{
    const uint8_t *p = guest(address);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int tag_at(uint32_t address, const char *tag)
{
    return memcmp(guest(address), tag, 4) == 0;
}

/* ---------------------------------------------------------------------------
 * YAMAHA.INC: the synthesizer
 * ------------------------------------------------------------------------- */

static void write_register(unsigned int op, unsigned int base, unsigned int value)
{
    Game_OplWrite(base + op_index[op], value);
}

static void send_byte(unsigned int voice, unsigned int base, unsigned int value)
{
    Game_OplWrite(base + voice, value);
}

static void reset_synth(void)
{
    unsigned int reg;
    for (reg = 0x01; reg <= 0xf5; reg++) Game_OplWrite(reg, array0_init(reg));
}

static int index_timbre(unsigned int gnum)  /* Get global timbre's local index */
{
    int si;
    for (si = 0; si < MAX_TIMBS; si++)
        if ((timb_attribs[si] & 0x80) && timb_bank[si] == (gnum >> 8) && timb_num[si] == (gnum & 0xff)) return si;
    return -1;
}

static void protect_timbre(unsigned int bank, unsigned int num, int protect)
{
    const unsigned int gnum = ((bank & 0xff) << 8) | (num & 0xff);
    int index;

    if (gnum == 0xffff) {
        for (index = 0; index < MAX_TIMBS; index++)
            timb_attribs[index] = protect ? timb_attribs[index] | 0x40 : timb_attribs[index] & ~0x40;
        return;
    }
    index = index_timbre(gnum);
    if (index < 0) return;
    timb_attribs[index] = protect ? timb_attribs[index] | 0x40 : timb_attribs[index] & ~0x40;
}

static uint32_t timbre_status(unsigned int bank, unsigned int num)
{
    const int index = index_timbre(((bank & 0xff) << 8) | (num & 0xff));
    if (index < 0) return 0;
    return (uint32_t)timb_offsets[index] + 1;   /* offset+1 in local cache */
}

static uint32_t get_request(int sequence)
{
    const state_table *s;
    uint32_t si;
    unsigned int di;

    if (sequence < 0 || sequence >= NSEQS || !sequence_state[sequence].registered) return 0xffff;
    s = &sequence_state[sequence];
    if (s->TIMB == 0) return 0xffff;            /* no requested timbres */
    si = s->TIMB;
    if (!tag_at(si, "TIMB")) return 0xffff;
    si += 8;
    for (di = word_at(si); di != 0; di--) {     /* TIMB.cnt */
        si += 2;
        if (index_timbre(word_at(si)) == -1) return word_at(si);    /* AL=num, AH=bank */
    }
    return 0xffff;
}

static void release_voice(int slot);

/* Excise LRU timbre from cache, adjust links & references; 0 if none */
static int delete_LRU(void)
{
    uint32_t oldest = 0xffffffffu, tsize, toff;
    int index = -1, si, chan;

    for (si = 0; si < MAX_TIMBS; si++) {
        if (!(timb_attribs[si] & 0x80) || (timb_attribs[si] & 0x40)) continue;
        if (timb_hist[si] > oldest) continue;
        oldest = timb_hist[si];
        index = si;
    }
    if (index == -1) return 0;

    toff = timb_offsets[index];
    tsize = word_at(cache_base + toff);
    memmove(guest(cache_base + toff), guest(cache_base + toff + tsize), cache_end - toff - tsize);
    timb_attribs[index] = 0;
    cache_end -= tsize;

    for (chan = 0; chan < NUM_CHANS; chan++)
        if (MIDI_timbre[chan] == index) MIDI_timbre[chan] = -1;
    for (si = 0; si < 128; si++)
        if (RBS_timbres[si] == index) RBS_timbres[si] = -1;
    for (si = 0; si < MAX_TIMBS; si++)
        if ((timb_attribs[si] & 0x80) && timb_offsets[si] > toff) timb_offsets[si] -= (uint16_t)tsize;

    toff += cache_base;                         /* adjust pointers to any active timbres */
    for (si = 0; si < NUM_SLOTS; si++) {
        if (S_status[si] == FREE || S_timbre[si] < toff) continue;
        if (S_timbre[si] == toff) {
            release_voice(si);
            S_status[si] = FREE;
        } else {
            S_timbre[si] -= tsize;
        }
    }
    return 1;
}

static void install_timbre(unsigned int bank, unsigned int num, uint32_t addr)
{
    int index = index_timbre(((bank & 0xff) << 8) | (num & 0xff));
    int chan;

    if (index == -1) {
        uint32_t size;

        if (addr == 0) return;
        size = word_at(addr);
        for (;;) {                              /* find first available timbre slot */
            for (index = 0; index < MAX_TIMBS && (timb_attribs[index] & 0x80); index++) {}
            if (index < MAX_TIMBS && cache_end + size <= cache_size) break;
            if (!delete_LRU()) return;          /* (the driver would wait for ever) */
        }
        timb_num[index] = (uint8_t)num;
        timb_bank[index] = (uint8_t)bank;
        timb_attribs[index] = 0x80;
        timb_hist[index] = note_event++;
        timb_offsets[index] = (uint16_t)cache_end;
        memcpy(guest(cache_base + cache_end), guest(addr), size);
        cache_end += size;
    }
    for (chan = 0; chan < NUM_CHANS; chan++)    /* update the channels that asked for it */
        if (MIDI_program[chan] == (int)(num & 0xff) && MIDI_bank[chan] == (bank & 0xff)) MIDI_timbre[chan] = (int16_t)index;
}

static void init_synth(void)
{
    int i;

    note_event = 0;
    memset(timb_attribs, 0, sizeof(timb_attribs));
    for (i = 0; i < NUM_CHANS; i++) {
        MIDI_timbre[i] = -1;
        MIDI_voices[i] = 0;
        MIDI_program[i] = -1;
        MIDI_bank[i] = 0;
    }
    memset(S_status, FREE, sizeof(S_status));
    memset(V_channel, -1, sizeof(V_channel));
    for (i = 0; i < 128; i++) RBS_timbres[i] = -1;
    rover_2op = -1;
}

static void update_voice(int slot);
static void update_priority(void);

static void assign_voice(int slot)          /* Allocate hardware voice to slot */
{
    int tries, voice = rover_2op;

    for (tries = 0; tries < NUM_VOICES; tries++) {
        if (++voice == NUM_VOICES) voice = 0;
        rover_2op = voice;
        if (V_channel[voice] != -1) continue;
        S_voice[slot] = (int8_t)voice;          /* found free voice, assign it to slot */
        MIDI_voices[S_channel[slot]]++;
        V_channel[voice] = (int8_t)S_channel[slot];
        S_update[slot] = U_ALL_REGS;
        update_voice(slot);
        return;
    }
    update_priority();                          /* assign voice based on priority search */
}

static void release_voice(int slot)         /* Release slot's voice */
{
    if (S_voice[slot] == -1) return;
    S_BLOCK[slot] &= 0xdf;                      /* force KON = 0... */
    S_update[slot] |= U_FREQ;
    update_voice(slot);                         /* ...silence any note... */
    MIDI_voices[S_channel[slot]]--;             /* ...and deallocate the voice */
    V_channel[S_voice[slot]] = -1;
    S_voice[slot] = -1;
    if (S_type[slot] == OPL3_INST || S_type[slot] == BNK_INST) S_status[slot] = FREE;
}

/* (v * w * 2) / 256, rounded up if not 0: the driver's 1/127 */
static unsigned int scale127(unsigned int v, unsigned int w)
{
    unsigned int x = ((v * w) << 1) >> 8 & 0xff;
    return x != 0 ? x + 1 : 0;
}

static void update_voice(int slot)          /* Update hardware regs for slot */
{
    unsigned int voice, voice0, voice1, vol = 0, chan;

    if (S_voice[slot] == -1) return;            /* no hardware voice assigned */
    chan = S_channel[slot] & 0x0f;
    if (S_update[slot] & U_KSLTL) {
        vol = scale127(MIDI_vol[chan], MIDI_express[chan]);
        vol = scale127(vol, S_velocity[slot]);  /* composite (vol+expression) volume */
    }
    voice = (unsigned int)S_voice[slot];
    voice0 = op_0[voice];                       /* "modulator" operator cell */
    voice1 = op_1[voice];                       /* "carrier" operator cell */

    if (S_update[slot] & U_AVEKM) {
        const unsigned int vib = MIDI_mod[chan] >= 64 ? 0x40 : 0x00;
        write_register(voice0, 0x20, (S_m0_val[slot] >> 12) | vib | S_AVEKM_0[slot]);
        write_register(voice1, 0x20, (S_m1_val[slot] >> 12) | vib | S_AVEKM_1[slot]);
        S_update[slot] &= ~U_AVEKM;
    }
    if (S_update[slot] & U_KSLTL) {
        unsigned int level = S_v0_val[slot] >> 10;     /* modulator volume level, 0-63 */
        if (S_scale_01[slot] & 1) level = level * vol / 127;    /* additive operator */
        write_register(voice0, 0x40, (~level & 0x3f) | S_KSLTL_0[slot]);
        level = S_v1_val[slot] >> 10;                   /* carrier volume level */
        if (S_scale_01[slot] & 2) level = level * vol / 127;
        write_register(voice1, 0x40, (~level & 0x3f) | S_KSLTL_1[slot]);
        S_update[slot] &= ~U_KSLTL;
    }
    if (S_update[slot] & U_ADSR) {
        write_register(voice0, 0x60, S_AD_0[slot]);
        write_register(voice1, 0x60, S_AD_1[slot]);
        write_register(voice0, 0x80, S_SR_0[slot]);
        write_register(voice1, 0x80, S_SR_1[slot]);
        S_update[slot] &= ~U_ADSR;
    }
    if (S_update[slot] & U_WS) {
        write_register(voice1, 0xe0, S_ws_val[slot] & 0xff);
        write_register(voice0, 0xe0, S_ws_val[slot] >> 8);
        S_update[slot] &= ~U_WS;
    }
    if (S_update[slot] & U_FBC) {
        send_byte(voice, 0xc0, ((S_fb_val[slot] >> 12) & 0x0e) | (S_FBC[slot] & 1));
        S_update[slot] &= ~U_FBC;
    }
    if (S_update[slot] & U_FREQ) {             /* (update FREQ's KeyOn bit last) */
        if (!(S_BLOCK[slot] & 0x20)) {
            send_byte(voice, 0xb0, S_KBF_shadow[slot] & 0xdf);  /* turn the note off immediately */
        } else {
            /* signed pitch bend, -0x100..+0x100, times the range: 1/256 halftones */
            int32_t pitch = ((((int32_t)MIDI_pitch_h[chan] << 7) | MIDI_pitch_l[chan]) - 0x2000) >> 5;
            int32_t note = (int32_t)S_note[slot] + S_transpose[slot] - 24;
            int32_t ax, block;
            unsigned int index, f_num;

            pitch = (int16_t)(pitch * DEF_PITCH_RANGE);
            do note += 12; while (note < 0);    /* normalize to 0-95 */
            note += 12;
            do note -= 12; while (note > 95);
            ax = (int16_t)(pitch + (note << 8));    /* add computed note offset */
            ax = (int16_t)(ax + 8) >> 4;            /* true note #, in 1/16 halftones */
            ax -= 12 * 16;
            do ax += 12 * 16; while (ax < 0);
            ax += 12 * 16;
            do ax -= 12 * 16; while (ax > 96 * 16 - 1);
            index = (unsigned int)ax >> 4;          /* the physical note # */
            f_num = freq_table[(index % 12) * 16 + (ax & 15)];
            block = (int32_t)(index / 12) - 1;
            if (f_num & 0x8000) block++;
            if (block < 0) {
                block++;
                f_num = (uint16_t)((int16_t)f_num >> 1);
            }
            f_num = (f_num & 0x3ff) | ((unsigned int)block << 10);
            send_byte(voice, 0xa0, f_num & 0xff);
            S_KBF_shadow[slot] = (uint8_t)((f_num >> 8) | S_BLOCK[slot]);  /* merge with KON/BLOCK bits */
            send_byte(voice, 0xb0, S_KBF_shadow[slot]);
        }
        S_update[slot] &= ~U_FREQ;
    }
}

static void update_priority(void)           /* Maintain synthesizer voice priority */
{
    int slot_cnt = 0, si, low_p = -1, high_p = -1;

    for (si = 0; si < NUM_SLOTS; si++) {        /* build adjusted priority table */
        int32_t priority;
        unsigned int chan;

        if (S_status[si] == FREE) continue;
        slot_cnt++;
        chan = S_channel[si] & 0x0f;
        priority = MIDI_vprot[chan] >= 64 ? 0xffff : S_p_val[si];   /* max if voice protection on */
        priority -= MIDI_voices[chan];          /* priority -= # of voices in channel */
        S_V_priority[si] = (uint16_t)(priority < 0 ? 0 : priority);
    }

    while (slot_cnt > 0) {
        unsigned int high = 0, low = 0xffff;    /* unvoiced highest, voiced lowest */
        int voice;

        for (si = 0; si < NUM_SLOTS; si++) {
            if (S_status[si] == FREE) continue;
            if (S_voice[si] == -1) {
                if (S_V_priority[si] < high) continue;
                high = S_V_priority[si];
                high_p = si;                    /* highest-priority unvoiced slot */
            } else {
                if (S_V_priority[si] > low) continue;
                low = S_V_priority[si];
                low_p = si;                     /* lowest-priority voiced slot */
            }
        }
        if (high < low || high == 0) return;    /* priorities valid, or no unvoiced slot */

        voice = S_voice[low_p];                 /* steal voice from the slot */
        release_voice(low_p);
        S_voice[high_p] = (int8_t)voice;
        MIDI_voices[S_channel[high_p]]++;
        V_channel[voice] = (int8_t)S_channel[high_p];
        S_update[high_p] = U_ALL_REGS;
        update_voice(high_p);
        slot_cnt--;
    }
}

static void BNK_phase(int slot)             /* Set up BNK slot parameters */
{
    const uint32_t t = S_timbre[slot];
    uint8_t value;

    S_BLOCK[slot] = 0x20;                       /* set KON, clear BLOCK mask */
    S_type[slot] = BNK_INST;
    S_p_val[slot] = 32767;                      /* BNK instrument priority = average */

    value = byte_at(t + B_fb_c);                /* feedback/connection values */
    S_FBC[slot] = value & 1;
    S_fb_val[slot] = (uint16_t)(value << 12);

    value = byte_at(t + B_mod_KSLTL);           /* key scale/total level values */
    S_KSLTL_0[slot] = value & 0xc0;
    S_v0_val[slot] = (uint16_t)((~value & 0x3f) << 10);
    value = byte_at(t + B_car_KSLTL);
    S_KSLTL_1[slot] = value & 0xc0;
    S_v1_val[slot] = (uint16_t)((~value & 0x3f) << 10);

    value = byte_at(t + B_mod_AVEKM);           /* AM/Vib/EG/KSR/Multi bits */
    S_AVEKM_0[slot] = value & 0xf0;
    S_m0_val[slot] = (uint16_t)(value << 12);
    value = byte_at(t + B_car_AVEKM);
    S_AVEKM_1[slot] = value & 0xf0;
    S_m1_val[slot] = (uint16_t)(value << 12);

    S_AD_0[slot] = byte_at(t + B_mod_AD);       /* envelope parms */
    S_SR_0[slot] = byte_at(t + B_mod_SR);
    S_AD_1[slot] = byte_at(t + B_car_AD);
    S_SR_1[slot] = byte_at(t + B_car_SR);

    S_ws_val[slot] = (uint16_t)(byte_at(t + B_car_WS) | (byte_at(t + B_mod_WS) << 8));

    S_scale_01[slot] = S_FBC[slot] | 2;         /* C-bit; 2-op carrier always scaled */
    S_update[slot] = U_ALL_REGS;
}

static void note_off(unsigned int chan, unsigned int note)
{
    int si;

    for (si = 0; si < NUM_SLOTS; si++) {        /* all slots in which the note is playing */
        if (S_status[si] != KEYON || S_keynum[si] != note || S_channel[si] != chan) continue;
        if (MIDI_sus[chan] >= 64) {
            S_sustain[si] = 1;
            continue;
        }
        if (S_type[si] == OPL3_INST || S_type[si] == BNK_INST) {
            release_voice(si);
            S_status[si] = FREE;
        }
    }
}

static void note_on(unsigned int chan, unsigned int note, unsigned int velocity)
{
    int timbre = MIDI_timbre[chan], si;
    uint32_t t;

    if (chan == 9) {                            /* channel under RBS control */
        timbre = RBS_timbres[note];
        if (timbre == -1) {                     /* validate RBS entry first */
            timbre = index_timbre((127u << 8) | note);  /* bank 127 reserved for RBS timbres */
            RBS_timbres[note] = (int16_t)timbre;
        }
    }
    if (timbre == -1) return;                   /* timbre not loaded */
    t = cache_base + timb_offsets[timbre];
    timb_hist[timbre] = ++note_event;           /* timbre cache LRU counters */

    for (si = 0; si < NUM_SLOTS && S_status[si] != FREE; si++) {}
    if (si == NUM_SLOTS) return;                /* no virtual voice available */

    S_channel[si] = (uint8_t)chan;
    S_keynum[si] = (uint8_t)note;
    if (chan == 9) {                            /* the rhythm timbre has its own note */
        S_note[si] = byte_at(t + B_transpose);
        S_transpose[si] = 0;
    } else {
        S_note[si] = (uint8_t)note;
        S_transpose[si] = (int8_t)byte_at(t + B_transpose);
    }
    S_velocity[si] = vel_graph[(velocity & 0x7f) >> 3];   /* reduce perceived playback noise */
    S_timbre[si] = t;
    S_status[si] = KEYON;
    S_sustain[si] = 0;

    S_voice[si] = -1;
    if (word_at(t + B_length) != BNK_SIZE) {    /* OPL3 and TVFX timbres: not on a YM3812 */
        S_type[si] = BNK_INST;                  /* (the note off frees the slot) */
        return;
    }
    BNK_phase(si);
    assign_voice(si);
}

static void release_sustain(unsigned int chan)
{
    int si;

    for (si = 0; si < NUM_SLOTS; si++)
        if (S_status[si] != FREE && S_channel[si] == chan && S_sustain[si] != 0) note_off(chan, S_note[si]);
}

static void flag_updates(unsigned int chan, unsigned int flags)
{
    int si;

    for (si = 0; si < NUM_SLOTS; si++) {        /* mark the voice parameters as "changed" */
        if (S_status[si] == FREE || S_channel[si] != chan) continue;
        S_update[si] |= flags;
        update_voice(si);
    }
}

/* Send MIDI Channel Voice message */
static void send_MIDI_message(unsigned int stat, unsigned int d1, unsigned int d2)
{
    const unsigned int chan = stat & 0x0f;
    int si;

    d1 &= 0xff;
    d2 &= 0xff;
    switch (stat & 0xf0) {
    case 0x90:
        if (chan < MIN_TRUE_CHAN - 1 || chan > MAX_REC_CHAN - 1) return;
        if (d2 != 0) {
            note_on(chan, d1, d2);
            return;
        }
        /* implicit Note Off if velocity==0 */
        /* fall through */
    case 0x80:
        note_off(chan, d1);
        return;
    case 0xe0:
        MIDI_pitch_l[chan] = (uint8_t)d1;
        MIDI_pitch_h[chan] = (uint8_t)d2;
        flag_updates(chan, U_FREQ);
        return;
    case 0xc0:
        MIDI_program[chan] = (int16_t)d1;
        MIDI_timbre[chan] = (int16_t)index_timbre((MIDI_bank[chan] << 8) | d1);
        return;
    case 0xb0:
        break;
    default:
        return;
    }

    switch (d1) {                               /* Control Change */
    case PATCH_BANK_SEL:
        MIDI_bank[chan] = (uint8_t)d2;
        return;
    case VOICE_PROTECT:
        MIDI_vprot[chan] = (uint8_t)d2;
        return;
    case TIMBRE_PROTECT:
        if (MIDI_timbre[chan] == -1) return;
        timb_attribs[MIDI_timbre[chan]] = (uint8_t)((timb_attribs[MIDI_timbre[chan]] & 0xbf) | (d2 >= 64 ? 0x40 : 0));
        return;
    case MODULATION:
        MIDI_mod[chan] = (uint8_t)d2;
        flag_updates(chan, U_AVEKM);
        return;
    case PART_VOLUME:
        MIDI_vol[chan] = (uint8_t)d2;
        flag_updates(chan, U_KSLTL);
        return;
    case EXPRESSION:
        MIDI_express[chan] = (uint8_t)d2;
        flag_updates(chan, U_KSLTL);
        return;
    case PANPOT:
        MIDI_pan[chan] = (uint8_t)d2;
        flag_updates(chan, U_KSLTL);
        return;
    case SUSTAIN:
        MIDI_sus[chan] = (uint8_t)d2;
        if (d2 < 64) release_sustain(chan);     /* turn off any sustained notes */
        return;
    case RESET_ALL_CTRLS:
        MIDI_sus[chan] = 0;
        release_sustain(chan);
        MIDI_mod[chan] = 0;                     /* emulate Roland LAPC-1 RAC message */
        MIDI_express[chan] = 127;
        MIDI_pitch_l[chan] = DEF_PITCH_L;
        MIDI_pitch_h[chan] = DEF_PITCH_H;
        flag_updates(chan, U_AVEKM | U_KSLTL | U_FREQ);
        return;
    case ALL_NOTES_OFF:
        for (si = 0; si < NUM_SLOTS; si++)      /* turn off all notes playing in channel */
            if (S_status[si] == KEYON && S_channel[si] == chan) note_off(chan, S_note[si]);
        return;
    default:
        return;
    }
}

/* ---------------------------------------------------------------------------
 * XMIDI.ASM: the interpreter
 * ------------------------------------------------------------------------- */

static int valid_handle(int sequence)
{
    return sequence >= 0 && sequence < NSEQS && sequence_state[sequence].registered;
}

/* the variable-length number at *address, which it moves past */
static uint32_t read_VLN(uint32_t *address)
{
    uint32_t value = 0;
    uint8_t byte;
    do {
        byte = byte_at((*address)++);
        value = (value << 7) | (byte & 0x7f);
    } while (byte & 0x80);
    return value;
}

/* Find FORM SeqNum in IFF CAT/FORM XMID file; 0 if not found */
static uint32_t find_seq(uint32_t xmid, uint32_t seq_num)
{
    uint32_t si = xmid, cx = seq_num + 1;
    int32_t end_addr;
    int guard;

    for (guard = 0;; guard++) {
        if (guard == 64 || !(tag_at(si, "CAT ") || tag_at(si, "FORM"))) return 0;  /* not an IFF image */
        if (tag_at(si + 8, "XMID")) break;
        si += be32_at(si + 4) + 8;              /* find first XMID chunk */
    }
    end_addr = (int32_t)(be32_at(si + 4) - 5);  /* last byte of all FORMs */
    if (tag_at(si, "FORM")) return cx == 1 ? si : 0;

    si += 12;                                   /* index first FORM chunk */
    for (;;) {
        uint32_t length;
        if (tag_at(si + 8, "XMID") && --cx == 0) return si;
        length = be32_at(si + 4) + 8;           /* else add length of FORM + 8 */
        end_addr -= (int32_t)length;
        if (end_addr < 0) return 0;             /* EOF reached */
        si += length;
    }
}

static void rewind_seq(state_table *s)      /* Reset sequence pointer and state */
{
    int i;

    for (i = 0; i < FOR_NEST; i++) s->FOR_loop_cnt[i] = -1;
    for (i = 0; i < NUM_CHANS; i++) {
        s->chan_map[i] = (uint8_t)i;
        s->chan_program[i] = 0xff;
        s->chan_pitch_l[i] = 0xff;
        s->chan_pitch_h[i] = 0xff;
        s->chan_indirect[i] = 0xff;
    }
    memset(s->chan_controls, 0xff, sizeof(s->chan_controls));
    memset(s->note_chan, 0xff, sizeof(s->note_chan));
    s->cur_callback = -1;
    s->interval_cnt = 0;
    s->note_count = 0;
    s->vol_percent = DEF_SYNTH_VOL;
    s->vol_target = DEF_SYNTH_VOL;
    s->tempo_percent = 100;
    s->tempo_target = 100;
    s->tempo_error = 0;
    s->beat_count = 0;
    s->measure_count = -1;
    s->beat_fraction = 0;
    s->time_fraction = 0;
    s->time_numerator = 4;                      /* default to 4/4 time */
    s->time_per_beat = 0x7a1200;                /* 500000 us/beat*16 (120 beats/min) */
}

/* one queued note off */
static void queue_note_off(state_table *s, int entry)
{
    const unsigned int chan = s->chan_map[s->note_chan[entry]];

    s->note_chan[entry] = 0xff;
    active_notes[chan]--;                       /* dec # of active notes in channel */
    send_MIDI_message(0x80 | chan, s->note_num[entry], 0);
}

static void flush_channel_notes(unsigned int chan)  /* every sequence's notes off in a channel */
{
    int sequence, entry;

    for (sequence = 0; sequence < NSEQS; sequence++) {
        state_table *s = &sequence_state[sequence];
        if (!s->registered || s->note_count == 0) continue;
        for (entry = 0; entry < MAX_NOTES; entry++) {
            if (s->note_chan[entry] != chan) continue;
            queue_note_off(s, entry);
            s->note_count--;
        }
    }
}

static void flush_note_queue(state_table *s)    /* Turn all queued notes off */
{
    int entry;

    for (entry = 0; entry < MAX_NOTES; entry++)
        if (s->note_chan[entry] != 0xff) queue_note_off(s, entry);
    s->note_count = 0;
}

static void reset_sequence(state_table *s)  /* Abandon all sequence-owned resources */
{
    unsigned int chan;

    for (chan = 0; chan < NUM_CHANS; chan++) {
        if ((int8_t)s->chan_controls[SUS][chan] >= 64) {
            global_controls[SUS][chan] = 0;
            send_MIDI_message(0xb0 | chan, SUSTAIN, 0);
        }
        if ((int8_t)s->chan_controls[C_LOCK][chan] >= 64) {
            flush_channel_notes(chan);
            release_channel(s->chan_map[chan] + 1);
            s->chan_map[chan] = (uint8_t)chan;
        }
        if ((int8_t)s->chan_controls[C_PROT][chan] >= 64) lock_status[chan] &= 0xbf;
        if ((int8_t)s->chan_controls[V_PROT][chan] >= 64) send_MIDI_message(0xb0 | chan, VOICE_PROTECT, 0);
    }
}

static int XMIDI_control(state_table *s, unsigned int chan, unsigned int con, unsigned int val);

static void restore_sequence(state_table *s)    /* Reassert all "owned" controls */
{
    unsigned int chan, con;

    for (chan = 0; chan < NUM_CHANS; chan++) {  /* re-lock any channels formerly locked */
        if (s->chan_controls[C_LOCK][chan] == 0xff || (int8_t)s->chan_controls[C_LOCK][chan] < 64) continue;
        int locked = lock_channel() - 1;
        s->chan_map[chan] = (uint8_t)(locked == -1 ? (int)chan : locked);
    }
    for (con = 0; con < NUM_CONTROLS; con++) {  /* re-establish all logged controller values */
        if (logged_ctrls[con] == CHAN_LOCK) continue;
        for (chan = 0; chan < NUM_CHANS; chan++)
            if (s->chan_controls[con][chan] != 0xff) XMIDI_control(s, chan, logged_ctrls[con], s->chan_controls[con][chan]);
    }
    for (chan = 0; chan < NUM_CHANS; chan++) {  /* restore pitch/program # values */
        if (s->chan_pitch_l[chan] != 0xff && s->chan_pitch_h[chan] != 0xff)
            send_MIDI_message(0xe0 | s->chan_map[chan], s->chan_pitch_l[chan], s->chan_pitch_h[chan]);
        if (s->chan_program[chan] != 0xff) send_MIDI_message(0xc0 | s->chan_map[chan], s->chan_program[chan], 0);
    }
}

static void XMIDI_volume(state_table *s)    /* Send updated volume control messages */
{
    unsigned int chan;

    for (chan = 0; chan < NUM_CHANS; chan++) {
        unsigned int value;
        if (s->chan_controls[PV][chan] == 0xff) continue;
        value = s->chan_controls[PV][chan] * (unsigned int)s->vol_percent / 100;
        if (value > 127) value = 127;
        global_controls[PV][chan] = (uint8_t)value;
        if (lock_status[chan] & 0x80) continue; /* (logical channel locked) */
        send_MIDI_message(0xb0 | s->chan_map[chan], PART_VOLUME, value);
    }
}

/* Process XMIDI Control Change message */
static int XMIDI_control(state_table *s, unsigned int chan, unsigned int con, unsigned int val)
{
    int index, i;

    chan &= 0x0f;
    con &= 0xff;
    val &= 0xff;
    if (s->chan_indirect[chan] != 0xff) {       /* get value from controller table */
        const unsigned int entry = s->chan_indirect[chan];
        s->chan_indirect[chan] = 0xff;
        val = byte_at(s->ctrl_ptr + entry);
    }
    index = ctrl_hash[con];
    if (index != -1) {                          /* copy controller value to state tables */
        global_controls[index][chan] = (uint8_t)val;
        s->chan_controls[index][chan] = (uint8_t)val;
    }

    switch (con) {
    case PART_VOLUME:
        if (s->vol_percent != 100) {
            val = val * (unsigned int)s->vol_percent / 100;
            if (val > 127) val = 127;
            global_controls[PV][chan] = (uint8_t)val;
        }
        break;
    case CLEAR_BEAT_BAR:
        s->beat_count = 0;
        s->measure_count = 0;
        s->beat_fraction = -s->time_fraction;
        return 3;
    case CALLBACK_TRIG:
        s->cur_callback = (int32_t)val;
        return 3;
    case FOR_LOOP:
        for (i = 0; i < FOR_NEST && s->FOR_loop_cnt[i] != -1; i++) {}
        if (i < FOR_NEST) {
            s->FOR_loop_cnt[i] = (int32_t)val;
            s->FOR_ptrs[i] = s->EVNT_ptr;       /* (NEXT controller will skip FOR) */
        }
        return 3;
    case NEXT_LOOP:
        if (val < 64) return 3;                 /* BREAK controller */
        for (i = FOR_NEST - 1; i >= 0 && s->FOR_loop_cnt[i] == -1; i--) {}
        if (i < 0) return 3;
        if (s->FOR_loop_cnt[i] != 0 && --s->FOR_loop_cnt[i] == 0) {    /* FOR value 0 = infinite loop */
            s->FOR_loop_cnt[i] = -1;
            return 3;
        }
        s->EVNT_ptr = s->FOR_ptrs[i];
        return 3;
    case CHAN_PROTECT:
        lock_status[chan] |= 0x40;
        if (val < 64) lock_status[chan] &= 0xbf;
        return 3;
    case CHAN_LOCK:
        if (val >= 64) {                        /* lock new channel and map to current */
            const int locked = lock_channel() - 1;
            s->chan_map[chan] = (uint8_t)(locked == -1 ? (int)chan : locked);
        } else {                                /* release and unmap locked channel */
            flush_channel_notes(chan);
            release_channel(s->chan_map[chan] + 1);
            s->chan_map[chan] = (uint8_t)chan;
        }
        return 3;
    case INDIRECT_C_PFX:
        s->chan_indirect[chan] = (uint8_t)val;
        return 3;
    default:
        break;
    }
    if (!(lock_status[chan] & 0x80)) send_MIDI_message(0xb0 | s->chan_map[chan], con, val);
    return 3;
}

/* Turn XMIDI note on, add to note queue; the size of the event */
static uint32_t XMIDI_note_on(state_table *s)
{
    uint32_t p = s->EVNT_ptr + 3, duration;
    const unsigned int chan = byte_at(s->EVNT_ptr) & 0x0f, note = byte_at(s->EVNT_ptr + 1);
    const unsigned int vel = byte_at(s->EVNT_ptr + 2);
    int entry;

    duration = read_VLN(&p);                    /* in q-intervals */
    if (lock_status[chan] & 0x80) return p - s->EVNT_ptr;   /* (logical channel locked) */

    for (entry = 0; entry < MAX_NOTES && s->note_chan[entry] != 0xff; entry++) {}
    if (entry == MAX_NOTES) entry = 0;          /* overwrite entry 0 if queue full */
    else s->note_count++;
    s->note_chan[entry] = (uint8_t)chan;
    s->note_num[entry] = (uint8_t)note;
    s->note_time[entry] = (int32_t)duration - 1;    /* (the queue watches for negative durations) */
    active_notes[s->chan_map[chan]]++;
    send_MIDI_message(0x90 | s->chan_map[chan], note, vel);
    return p - s->EVNT_ptr;
}

static void release_seq(int sequence);

static uint32_t XMIDI_meta(state_table *s)  /* XMIDI meta-event interpreter */
{
    const unsigned int event_type = byte_at(s->EVNT_ptr + 1);
    uint32_t p = s->EVNT_ptr + 2, length;
    int32_t fraction;

    length = read_VLN(&p);
    switch (event_type) {
    case 0x2f:                                  /* end of track */
        reset_sequence(s);
        s->status = SEQ_DONE;
        if (s->post_release) release_seq(current_handle);
        break;
    case 0x58: {                                /* time signature */
        const int denominator = byte_at(p + 1);
        s->time_numerator = byte_at(p);
        if (denominator >= 2) fraction = QUANT_TIME_16 << (denominator - 2);
        else fraction = QUANT_TIME_16 >> (2 - denominator);
        s->time_fraction = fraction;
        s->beat_fraction = -fraction;
        s->beat_count = 0;
        s->measure_count++;
        break;
    }
    case 0x51:                                  /* tempo, us per quarter note, *16 */
        s->time_per_beat = (int32_t)(((uint32_t)byte_at(p) << 16 | (uint32_t)byte_at(p + 1) << 8 | byte_at(p + 2)) << 4);
        break;
    default:
        break;
    }
    return p - s->EVNT_ptr + length;
}

static uint32_t XMIDI_sysex(state_table *s) /* XMIDI System Exclusive interpreter */
{
    uint32_t p = s->EVNT_ptr + 1, length;

    length = read_VLN(&p);
    return p - s->EVNT_ptr + length;
}

/* bar (high 16 bits) and beat, "advanced" by QUANT_ADVANCE intervals */
static uint32_t advance_count(state_table *s)
{
    int32_t beat = s->beat_count, bar = s->measure_count, fraction = s->beat_fraction;
    int i;

    for (i = 0; i < QUANT_ADVANCE; i++) {
        fraction += s->time_fraction;
        if (fraction < s->time_per_beat) continue;
        fraction -= s->time_per_beat;
        if (++beat >= s->time_numerator) {
            beat = 0;
            bar++;
        }
    }
    if (bar < 0) bar = 0;                       /* don't return negative measure counts */
    return ((uint32_t)bar << 16) | (uint16_t)beat;
}

/* the events of an interval: until the next interval count or the end */
static void play_events(state_table *s)
{
    while (s->status == SEQ_PLAYING) {
        const unsigned int status = byte_at(s->EVNT_ptr);
        const unsigned int chan = status & 0x0f;
        const unsigned int d1 = byte_at(s->EVNT_ptr + 1), d2 = byte_at(s->EVNT_ptr + 2);
        uint32_t size;

        if (status < 0x80) {                    /* XMIDI interval count */
            s->EVNT_ptr++;
            s->interval_cnt = (int32_t)status;
            return;
        }
        switch (status & 0xf0) {
        case 0xf0:
            size = chan == 0x0f ? XMIDI_meta(s) : XMIDI_sysex(s);
            break;
        case 0xe0:
            s->chan_pitch_l[chan] = global_pitch_l[chan] = (uint8_t)d1;
            s->chan_pitch_h[chan] = global_pitch_h[chan] = (uint8_t)d2;
            size = 3;
            if (!(lock_status[chan] & 0x80)) send_MIDI_message(0xe0 | s->chan_map[chan], d1, d2);
            break;
        case 0xd0:
            size = 2;
            if (!(lock_status[chan] & 0x80)) send_MIDI_message(0xd0 | s->chan_map[chan], d1, d2);
            break;
        case 0xc0:
            s->chan_program[chan] = global_program[chan] = (uint8_t)d1;
            size = 2;
            if (!(lock_status[chan] & 0x80)) send_MIDI_message(0xc0 | s->chan_map[chan], d1, d2);
            break;
        case 0xb0:
            size = (uint32_t)XMIDI_control(s, chan, d1, d2);
            break;
        case 0xa0:
            size = 3;
            if (!(lock_status[chan] & 0x80)) send_MIDI_message(0xa0 | s->chan_map[chan], d1, d2);
            break;
        default:                                /* 80, 90: note on with its duration */
            size = XMIDI_note_on(s);
            break;
        }
        s->EVNT_ptr += size;
    }
}

/* the gradients of the tempo and of the volume: the change of an interval */
static int32_t gradient(int32_t *accum, int32_t period)
{
    int32_t value = *accum + QUANT_TIME / 100, steps = -1;

    do {
        steps++;
        *accum = value;
        value -= period;
    } while (value >= 0);
    return steps;
}

static int32_t step_to(int32_t percent, int32_t target, int32_t steps)
{
    if (percent < target) return percent + steps <= target ? percent + steps : target;
    return percent - steps >= target ? percent - steps : target;
}

static void serve_driver(void *user)        /* Periodic driver service routine */
{
    (void)user;
    if (service_active) return;
    service_active++;

    for (current_handle = 0; current_handle < NSEQS; current_handle++) {
        state_table *s = &sequence_state[current_handle];
        int32_t error;

        if (!s->registered || s->status != SEQ_PLAYING) continue;

        error = s->tempo_error + s->tempo_percent;
        s->tempo_error = error;
        while ((error -= 100) >= 0) {           /* the intervals of this tick */
            int entry;

            s->tempo_error = error;
            if (s->note_count != 0) {           /* turn off any expired notes */
                for (entry = 0; entry < MAX_NOTES && s->note_count != 0; entry++) {
                    if (s->note_chan[entry] == 0xff || --s->note_time[entry] >= 0) continue;
                    queue_note_off(s, entry);
                    s->note_count--;
                }
            }
            if (--s->interval_cnt <= 0) {       /* next interval ready, play it */
                play_events(s);
                if (s->status != SEQ_PLAYING) break;
            }
            s->beat_fraction += s->time_fraction;
            if (s->beat_fraction >= s->time_per_beat) {
                s->beat_fraction -= s->time_per_beat;
                if (++s->beat_count >= s->time_numerator) {
                    s->beat_count = 0;
                    s->measure_count++;
                }
            }
            error = s->tempo_error;
        }
        if (s->status != SEQ_PLAYING) continue;

        if (s->tempo_percent != s->tempo_target) {
            const int32_t steps = gradient(&s->tempo_accum, s->tempo_period);
            if (steps != 0) s->tempo_percent = step_to(s->tempo_percent, s->tempo_target, steps);
        }
        if (s->vol_percent != s->vol_target) {
            const int32_t steps = gradient(&s->vol_accum, s->vol_period);
            if (steps != 0) {
                s->vol_percent = step_to(s->vol_percent, s->vol_target, steps);
                XMIDI_volume(s);
            }
        }
    }
    service_active--;
}

static void init_driver(void)
{
    unsigned int con, chan;

    service_active = 0;
    memset(global_controls, 0xff, sizeof(global_controls));
    memset(global_program, 0xff, sizeof(global_program));
    memset(global_pitch_l, 0xff, sizeof(global_pitch_l));
    memset(global_pitch_h, 0xff, sizeof(global_pitch_h));
    memset(ctrl_hash, -1, sizeof(ctrl_hash));
    memset(sequence_state, 0, sizeof(sequence_state));
    memset(lock_status, 0, sizeof(lock_status));
    memset(active_notes, 0, sizeof(active_notes));
    for (con = 0; con < NUM_CONTROLS; con++) ctrl_hash[logged_ctrls[con]] = (int8_t)con;

    reset_synth();
    init_synth();

    for (con = 0; con < NUM_CONTROLS; con++)    /* init MIDI/XMIDI controllers */
        for (chan = MIN_TRUE_CHAN - 1; chan <= MAX_REC_CHAN - 1; chan++) {
            global_controls[con][chan] = ctrl_default[con];
            send_MIDI_message(0xb0 | chan, logged_ctrls[con], ctrl_default[con]);
        }
    for (chan = MIN_TRUE_CHAN - 1; chan <= MAX_REC_CHAN - 1; chan++) {  /* init pitch/programs */
        global_pitch_l[chan] = DEF_PITCH_L;
        global_pitch_h[chan] = DEF_PITCH_H;
        send_MIDI_message(0xe0 | chan, DEF_PITCH_L, DEF_PITCH_H);
        if (prg_default[chan - (MIN_TRUE_CHAN - 1)] == 0xff) continue;
        global_program[chan] = prg_default[chan - (MIN_TRUE_CHAN - 1)];
        send_MIDI_message(0xc0 | chan, global_program[chan], 0);
    }
    init_OK = 1;
}

static uint32_t register_seq(uint32_t xmid, uint32_t num, uint32_t ctrl)
{
    state_table *s;
    uint32_t di, chunk_len = 12, end;
    int handle;

    for (handle = 0; handle < NSEQS && sequence_state[handle].registered; handle++) {}
    if (handle == NSEQS) return 0xffffffffu;    /* no sequence handles available */

    di = find_seq(xmid, num);
    if (di == 0) return 0xffffffffu;
    end = di + 8 + be32_at(di + 4);
    s = &sequence_state[handle];
    memset(s, 0, sizeof(*s));
    for (;;) {                                  /* skip FORM <len> XMID header, log the chunks */
        di += chunk_len;
        if (di + 8 > end) return 0xffffffffu;
        chunk_len = be32_at(di + 4) + 8;
        if (tag_at(di, "TIMB")) s->TIMB = di;   /* list of required timbres (optional) */
        else if (tag_at(di, "RBRN")) s->RBRN = di;  /* list of branch points (optional) */
        else if (tag_at(di, "EVNT")) break;     /* MIDI event list (mandatory, last) */
    }
    s->EVNT = di;
    s->ctrl_ptr = ctrl;
    s->post_release = 0;
    s->seq_started = 0;
    s->status = SEQ_STOPPED;
    s->registered = 1;
    rewind_seq(s);
    return (uint32_t)handle;
}

static void release_seq(int sequence)
{
    if (!valid_handle(sequence)) return;
    if (sequence_state[sequence].status == SEQ_PLAYING) sequence_state[sequence].post_release = 1;
    else sequence_state[sequence].registered = 0;
}

static void stop_seq(int sequence)
{
    state_table *s;

    if (!valid_handle(sequence)) return;
    s = &sequence_state[sequence];
    if (s->status != SEQ_PLAYING) return;
    flush_note_queue(s);
    reset_sequence(s);
    s->status = SEQ_STOPPED;
}

static void start_seq(int sequence)
{
    state_table *s;

    if (!valid_handle(sequence)) return;
    s = &sequence_state[sequence];
    if (s->status == SEQ_PLAYING) stop_seq(sequence);
    rewind_seq(s);
    s->EVNT_ptr = s->EVNT + 8;
    s->status = SEQ_PLAYING;
    s->seq_started = 1;
}

static void resume_seq(int sequence)
{
    state_table *s;

    if (!valid_handle(sequence)) return;
    s = &sequence_state[sequence];
    if (s->status != SEQ_STOPPED || !s->seq_started) return;
    restore_sequence(s);
    s->status = SEQ_PLAYING;
}

static void branch_index(int sequence, unsigned int marker)
{
    state_table *s;
    uint32_t di;
    unsigned int count;

    if (!valid_handle(sequence)) return;
    s = &sequence_state[sequence];
    if (s->RBRN == 0 || !tag_at(s->RBRN, "RBRN")) return;   /* no branch points */
    di = s->RBRN + 10;
    for (count = word_at(s->RBRN + 8); count != 0; count--, di += 6) {
        if (byte_at(di) != (marker & 0xff)) continue;
        s->EVNT_ptr = s->EVNT + 8 + (word_at(di + 2) | ((uint32_t)word_at(di + 4) << 16));
        s->interval_cnt = 0;
        flush_note_queue(s);
        memset(s->FOR_loop_cnt, 0xff, sizeof(s->FOR_loop_cnt));  /* BRANCH_EXIT */
        return;
    }
}

static void set_gradient(int32_t *percent, int32_t *target, int32_t *period, int32_t *accum,
                         int32_t value, uint32_t grad)
{
    int32_t delta;
    uint64_t steps;

    *target = value;
    if (grad == 0) {
        *percent = value;
        return;
    }
    delta = *target - *percent;
    if (delta == 0) return;
    if (delta < 0) delta = -delta;
    steps = (uint64_t)10 * grad / (uint32_t)delta;  /* # of 100us periods/step */
    *period = steps == 0 ? 1 : steps > 0x7fffffff ? 0x7fffffff : (int32_t)steps;
    *accum = 0;
}

/* return 0 if no channel available for locking, else the channel 1-16 */
static int lock_channel(void)
{
    int mask, di, si = -1;

    for (mask = 0xc0; si == -1; mask = 0x80) {  /* skip locked and protected channels */
        unsigned int lowest = 0xff;
        for (di = MAX_TRUE_CHAN - 1; di >= MIN_TRUE_CHAN - 1; di--) {
            if ((lock_status[di] & mask) || active_notes[di] >= lowest) continue;   /* (priority to higher chans) */
            lowest = active_notes[di];
            si = di;
        }
        if (si == -1 && mask == 0x80) return 0;
    }
    send_MIDI_message(0xb0 | (unsigned int)si, SUSTAIN, 0);
    flush_channel_notes((unsigned int)si);
    active_notes[si] = 0;
    lock_status[si] |= 0x80;
    return si + 1;
}

static void release_channel(int chan)
{
    unsigned int si = (unsigned int)(chan - 1) & 0x0f, con;

    if (!(lock_status[si] & 0x80)) return;      /* channel not locked */
    lock_status[si] &= 0x7f;
    active_notes[si] = 0;                       /* silence the channel */
    send_MIDI_message(0xb0 | si, SUSTAIN, 0);
    send_MIDI_message(0xb0 | si, ALL_NOTES_OFF, 0);
    for (con = 0; con < NUM_CONTROLS; con++)    /* update channel controller values */
        if (global_controls[con][si] != 0xff) send_MIDI_message(0xb0 | si, logged_ctrls[con], global_controls[con][si]);
    if (global_program[si] != 0xff) send_MIDI_message(0xc0 | si, global_program[si], 0);
    if (global_pitch_l[si] != 0xff && global_pitch_h[si] != 0xff)
        send_MIDI_message(0xe0 | si, global_pitch_l[si], global_pitch_h[si]);
}

static void shutdown_driver(void)
{
    int sequence;

    if (!init_OK) return;
    for (sequence = 0; sequence < NSEQS; sequence++) {
        if (!sequence_state[sequence].registered) continue;
        stop_seq(sequence);
        release_seq(sequence);
    }
    reset_synth();
    init_OK = 0;
}

/* ---------------------------------------------------------------------------
 * the driver's functions
 * ------------------------------------------------------------------------- */

int Game_AIL2_FmOpen(void)
{
    if (output_open || !Game_OplOpen(serve_driver, NULL, QUANT_RATE)) return 0;
    output_open = 1;
    return 1;
}

uint32_t Game_AIL2_FmCall(uint32_t function, const uint32_t *args)
{
    const int sequence = (int)args[1];
    uint32_t result = 0;
    state_table *s;

    Game_OplLock();
    s = valid_handle(sequence) ? &sequence_state[sequence] : NULL;
    switch (function) {
    case AIL_INIT_DRVR:
        init_driver();
        break;
    case AIL_SERVE_DRVR:                        /* the audio thread serves it */
        break;
    case AIL_SHUTDOWN_DRVR:
        shutdown_driver();
        break;
    case AIL_STATE_TAB_SIZE:
        /* the game allocates a table per sequence; the state is kept here */
        result = 16;
        break;
    case AIL_REG_SEQ:
        result = register_seq(args[1], args[2], args[4]);
        break;
    case AIL_REL_SEQ_HND:
        release_seq(sequence);
        break;
    case AIL_T_CACHE_SIZE:
        result = DEF_TC_SIZE;
        break;
    case AIL_DEFINE_T_CACHE:
        cache_base = args[1];
        cache_size = args[2] & 0xffff;
        cache_end = 0;
        break;
    case AIL_T_REQ:
        result = get_request(sequence);
        break;
    case AIL_INSTALL_T:
        install_timbre(args[1], args[2], args[3]);
        break;
    case AIL_PROTECT_T:
    case AIL_UNPROTECT_T:
        protect_timbre(args[1], args[2], function == AIL_PROTECT_T);
        break;
    case AIL_T_STATUS:
        result = timbre_status(args[1], args[2]);
        break;
    case AIL_START_SEQ:
        start_seq(sequence);
        break;
    case AIL_STOP_SEQ:
        stop_seq(sequence);
        break;
    case AIL_RESUME_SEQ:
        resume_seq(sequence);
        break;
    case AIL_SEQ_STAT:
        result = s != NULL ? (uint32_t)s->status : SEQ_DONE;
        break;
    case AIL_REL_VOL:
        result = s != NULL ? (uint32_t)s->vol_percent : 0xffffffffu;
        break;
    case AIL_REL_TEMPO:
        result = s != NULL ? (uint32_t)s->tempo_percent : 0xffffffffu;
        break;
    case AIL_SET_REL_VOL:
        if (s == NULL) break;
        set_gradient(&s->vol_percent, &s->vol_target, &s->vol_period, &s->vol_accum, (int32_t)args[2], args[3]);
        if (args[3] == 0) XMIDI_volume(s);
        break;
    case AIL_SET_REL_TEMPO:
        if (s != NULL) set_gradient(&s->tempo_percent, &s->tempo_target, &s->tempo_period, &s->tempo_accum, (int32_t)args[2], args[3]);
        break;
    case AIL_BEAT_CNT:
        result = s != NULL ? advance_count(s) & 0xffff : 0xffffffffu;
        break;
    case AIL_BAR_CNT:
        result = s != NULL ? advance_count(s) >> 16 : 0xffffffffu;
        break;
    case AIL_BRA_INDEX:
        branch_index(sequence, args[2]);
        break;
    case AIL_CON_VAL:
        if (s == NULL) result = 0xffffffffu;
        else if (args[3] == CALLBACK_TRIG) result = (uint32_t)s->cur_callback;   /* the last callback trigger */
        else if (args[3] < 256 && ctrl_hash[args[3]] != -1)
            result = (uint32_t)(int32_t)(int8_t)s->chan_controls[ctrl_hash[args[3]]][(args[2] - 1) & 0x0f];
        else result = 0xffffffffu;
        break;
    case AIL_SET_CON_VAL:
        if (s != NULL) XMIDI_control(s, args[2] - 1, args[3], args[4]);
        break;
    case AIL_CHAN_NOTES:
        if (s == NULL) result = 0xffffffffu;
        else {
            int entry;
            for (entry = 0; entry < MAX_NOTES; entry++) result += s->note_chan[entry] == ((args[2] - 1) & 0xff);
        }
        break;
    case AIL_MAP_SEQ_CHAN:
        if (s != NULL) s->chan_map[(args[2] - 1) & 0x0f] = (uint8_t)(args[3] - 1);
        break;
    case AIL_TRUE_SEQ_CHAN:
        result = s != NULL ? (uint32_t)s->chan_map[(args[2] - 1) & 0x0f] + 1 : 0xffffffffu;
        break;
    case AIL_SEND_CV_MSG:
        send_MIDI_message(args[1], args[2], args[3]);
        break;
    case AIL_SEND_SYSEX_MSG:
    case AIL_WRITE_DISP:
        break;
    case AIL_INSTALL_CB:
        if (!callback_warned && args[1] != 0) {
            callback_warned = 1;
            fprintf(stderr, "AIL2: the XMIDI callbacks are not called (the trigger can be polled)\n");
        }
        break;
    case AIL_CANCEL_CB:
        break;
    case AIL_LOCK_CHAN:
        result = (uint32_t)lock_channel();
        break;
    case AIL_RELEASE_CHAN:
        release_channel((int)args[1]);
        break;
    default:
        break;
    }
    Game_OplUnlock();
    return result;
}

void Game_AIL2_FmClose(void)
{
    if (!output_open) return;
    Game_OplLock();
    shutdown_driver();
    Game_OplUnlock();
    Game_OplClose();
    output_open = 0;
}
