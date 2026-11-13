/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */


/* MIDI music: a standard MIDI file (or XMIDI, turned into one) is played by
 * TinySoundFont from the soundfont.sf2 of the data directory.  The synthesizer
 * runs in the SDL_mixer music hook, so it needs the audio device
 * (Game_AudioInit) and costs nothing while no MIDI is playing. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_midi.h"
#include "Game_osd.h"
#include "Game_vars.h"
#include "Game_vfs.h"
#include "tsf.h"
#include "tml.h"

extern "C" int Game_TsfChannelNoteOn(tsf *f, int channel, int key, float vel);
extern "C" void Game_TsfLevelRegions(tsf *f);
extern "C" int Game_TsfChannelMidiControl(tsf *f, int channel, int controller, int value);
extern "C" void Game_TsfRenderSends(tsf *f, float *out, float *reverb, float *chorus, int samples,
                                    const float *reverb_level, const float *chorus_level);
#define GAME_TSF_SEND_FRAMES 256

#define MIDI_LOG(...) do { if (getenv("GAME_MIDI_LOG") != NULL) fprintf(stderr, "midi: " __VA_ARGS__); } while (0)

/* --- XMIDI to a standard MIDI file --- */

typedef struct {
    uint8_t *data;
    size_t length, capacity;
} midi_buffer;

static void midi_put(midi_buffer *b, const void *data, size_t n)
{
    if (b->length + n > b->capacity)
    {
        size_t cap = b->capacity ? b->capacity * 2 : 4096;
        while (cap < b->length + n) cap *= 2;
        b->data = (uint8_t *)realloc(b->data, cap);
        b->capacity = cap;
    }
    memcpy(b->data + b->length, data, n);
    b->length += n;
}

static void midi_put_byte(midi_buffer *b, int v)
{
    uint8_t c = (uint8_t)v;
    midi_put(b, &c, 1);
}

static void midi_put_vlq(midi_buffer *b, uint32_t v)
{
    uint8_t tmp[5];
    int n = 0;

    tmp[n++] = v & 0x7f;
    while ((v >>= 7) != 0) tmp[n++] = (uint8_t)((v & 0x7f) | 0x80);
    while (n > 0) midi_put_byte(b, tmp[--n]);
}

typedef struct { uint32_t time; uint8_t channel, note; } midi_noteoff;

#define AIL_MAX_NOTEOFF 256

/* The events of one XMIDI sequence (an EVNT chunk) to one MIDI track.  The time
 * of XMIDI is 120 ticks a second: the file has 60 ticks a quarter note and
 * 120 quarter notes a minute.  *infinite is set when the sequence loops for ever. */
static void midi_xmidi_events(const uint8_t *ev, uint32_t size, midi_buffer *track, int *infinite)
{
    midi_noteoff off[AIL_MAX_NOTEOFF];
    int noff = 0, i;
    uint32_t pos = 0, now = 0, last = 0;

    while (pos < size)
    {
        uint32_t delay = 0, due;
        int status;

        while (pos < size && ev[pos] < 0x80) delay += ev[pos++];
        due = now + delay;

        /* the notes that end before the next event */
        for (;;)
        {
            int best = -1;

            for (i = 0; i < noff; i++)
            {
                if (off[i].time <= due && (best < 0 || off[i].time < off[best].time)) best = i;
            }
            if (best < 0) break;
            midi_put_vlq(track, off[best].time - last);
            last = off[best].time;
            midi_put_byte(track, 0x80 | off[best].channel);
            midi_put_byte(track, off[best].note);
            midi_put_byte(track, 0);
            off[best] = off[--noff];
        }

        now = due;
        if (pos >= size) break;
        status = ev[pos++];

        if ((status & 0xf0) == 0x90)
        {
            uint32_t length = 0;
            int note, velocity;

            if (pos + 2 > size) break;
            note = ev[pos++];
            velocity = ev[pos++];
            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            midi_put_vlq(track, now - last);
            last = now;
            midi_put_byte(track, status);
            midi_put_byte(track, note);
            midi_put_byte(track, velocity);
            if (noff < AIL_MAX_NOTEOFF)
            {
                off[noff].time = now + length;
                off[noff].channel = (uint8_t)(status & 15);
                off[noff].note = (uint8_t)note;
                noff++;
            }
        }
        else if (status == 0xff)
        {
            uint32_t length = 0;
            int type;

            if (pos >= size) break;
            type = ev[pos++];
            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            if (type == 0x2f) break;
            /* the tempo is the one of the header; the text is dropped */
            pos += length;
        }
        else if (status == 0xf0 || status == 0xf7)
        {
            uint32_t length = 0;

            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            midi_put_vlq(track, now - last);
            last = now;
            midi_put_byte(track, status);
            midi_put_vlq(track, length);
            if (pos + length <= size) midi_put(track, ev + pos, length);
            pos += length;
        }
        else if (status >= 0x80)
        {
            int count = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;
            int a, b = 0;

            if (pos + count > size) break;
            a = ev[pos++];
            if (count == 2) b = ev[pos++];

            if ((status & 0xf0) == 0xb0 && (a == 116 || a == 117 || a == 118 || a == 119))
            {
                /* the loop controllers of XMIDI: a loop with the count 0 is endless */
                if (a == 116 && b == 0) *infinite = 1;
                continue;
            }
            midi_put_vlq(track, now - last);
            last = now;
            midi_put_byte(track, status);
            midi_put_byte(track, a);
            if (count == 2) midi_put_byte(track, b);
        }
    }

    /* the notes still sounding end with the sequence */
    for (i = 0; i < noff; i++)
    {
        uint32_t t = off[i].time > last ? off[i].time : last;

        midi_put_vlq(track, t - last);
        last = t;
        midi_put_byte(track, 0x80 | off[i].channel);
        midi_put_byte(track, off[i].note);
        midi_put_byte(track, 0);
    }
    midi_put_vlq(track, 0);
    midi_put_byte(track, 0xff);
    midi_put_byte(track, 0x2f);
    midi_put_byte(track, 0);
}

static uint32_t midi_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* XMIDI data (FORM XDIR, CAT XMID with FORM XMID sequences) to a MIDI file in memory */
uint8_t *Game_MidiFromXmidi(const uint8_t *xmi, uint32_t size, uint32_t number, size_t *length, int *infinite)
{
    midi_buffer track = {NULL, 0, 0}, file = {NULL, 0, 0};
    uint32_t pos = 0, index = 0;
    static const uint8_t tempo[] = {0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20};
    static const uint8_t header[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60};
    uint8_t len4[4];

    *infinite = 0;

    /* find the number-th "FORM....XMID" and its EVNT chunk */
    while (pos + 12 <= size)
    {
        uint32_t chunk = midi_be32(xmi + pos + 4);

        if (memcmp(xmi + pos, "FORM", 4) == 0 && memcmp(xmi + pos + 8, "XMID", 4) == 0)
        {
            if (index++ == number)
            {
                uint32_t p = pos + 12, end = pos + 8 + chunk;

                if (end > size) end = size;
                while (p + 8 <= end)
                {
                    uint32_t clen = midi_be32(xmi + p + 4);

                    if (memcmp(xmi + p, "EVNT", 4) == 0)
                    {
                        if (p + 8 + clen > end) clen = end - p - 8;
                        midi_put(&track, tempo, sizeof(tempo));
                        midi_xmidi_events(xmi + p + 8, clen, &track, infinite);
                        goto done;
                    }
                    p += 8 + clen + (clen & 1);
                }
            }
            pos += 8 + chunk + (chunk & 1);
        }
        else if (memcmp(xmi + pos, "FORM", 4) == 0 || memcmp(xmi + pos, "CAT ", 4) == 0)
        {
            pos += 12;     /* into the container */
        }
        else
        {
            pos += 8 + chunk + (chunk & 1);
        }
    }

done:
    if (track.length == 0)
    {
        free(track.data);
        return NULL;
    }

    midi_put(&file, header, sizeof(header));
    midi_put(&file, "MTrk", 4);
    len4[0] = (uint8_t)(track.length >> 24); len4[1] = (uint8_t)(track.length >> 16);
    len4[2] = (uint8_t)(track.length >> 8); len4[3] = (uint8_t)track.length;
    midi_put(&file, len4, 4);
    midi_put(&file, track.data, track.length);
    free(track.data);
    *length = file.length;
    return file.data;
}

/* --- the effects: reverb and chorus (the mod Midi_Effects) ---
 *
 * TinySoundFont has none.  The General MIDI synthesizers have both, and the
 * MIDI driver of the originals played on one of them (or on the synthesizer
 * of the sound card); fluidsynth has them on by default, and they are made
 * here to sound as its own (2.x, its default settings): the reverb is Freeverb
 * with no damping, its decay, level and width fitted to fluidsynth's (whose
 * reverb is no Freeverb any more); the chorus has 3 voices, 0.3 Hz and 8 ms
 * deep, and fluidsynth's width 10 (each side less much of the other).  Each
 * channel sends to them as the default modulators of SoundFont 2 have it:
 * 20 % at the controller 127, of CC91 (reverb, 40 by default, as fluidsynth
 * and the GM synthesizers start it) and CC93 (chorus, 0 by default).  The
 * send generators of the regions are not taken: TinySoundFont drops them. */

#define FX_COMBS 8
#define FX_ALLPASSES 4
#define FX_SPREAD 23                    /* the right channel's tunings are longer */
#define FX_CHORUS_VOICES 3
/* fitted to fluidsynth 2.6 on the same music: one note dies away to -60 dB
 * in 2.7 s (the feedback of the combs; Freeverb's room 0.2 has 0.8 s); on a
 * song the reverb alone is 23.5 dB under the dry part with its sides
 * correlated 0.3 (the width), and the chorus at CC93 127 8 dB under it */
#define FX_FEEDBACK 0.92f
#define FX_WIDTH 0.65f
#define FX_REVERB_GAIN 0.33f
#define FX_CHORUS_GAIN 0.48f

static const int fx_comb_tuning[FX_COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int fx_allpass_tuning[FX_ALLPASSES] = { 556, 441, 341, 225 };

typedef struct {
    float *buffer;
    int size, index;
    float store;
} fx_comb;

typedef struct {
    float *buffer;
    int size, index;
} fx_allpass;

static SDL_atomic_t midi_effects;       /* Midi_Effects: 1 on */
static int fx_running;                  /* the audio thread's: the effects were on at the last block */
static int fx_ready;
static fx_comb fx_combs[2][FX_COMBS];
static fx_allpass fx_allpasses[2][FX_ALLPASSES];
static float fx_feedback, fx_damp, fx_wet1, fx_wet2;
static float *fx_chorus_line;
static int fx_chorus_size, fx_chorus_index;
static double fx_chorus_phase, fx_chorus_step;
static float fx_chorus_base, fx_chorus_depth;
static const float fx_chorus_wet1 = (10.0f / 2.0f + 0.5f) / (1.0f + 10.0f * 0.2f);
static const float fx_chorus_wet2 = ((1.0f - 10.0f) / 2.0f) / (1.0f + 10.0f * 0.2f);
static float midi_reverb_send[16], midi_chorus_send[16];   /* the channels' sends */

static void fx_set_send(int ch, int controller, int value)
{
    if (controller == 91) midi_reverb_send[ch] = 0.2f * value / 127.0f;
    else midi_chorus_send[ch] = 0.2f * value / 127.0f;
}

/* the sends as a synthesizer starts (and as a reset leaves them) */
static void fx_reset_sends(void)
{
    int ch;

    for (ch = 0; ch < 16; ch++)
    {
        fx_set_send(ch, 91, 40);
        fx_set_send(ch, 93, 0);
    }
}

static void fx_clear(void)
{
    int c, i;

    for (c = 0; c < 2; c++)
    {
        for (i = 0; i < FX_COMBS; i++)
        {
            memset(fx_combs[c][i].buffer, 0, sizeof(float) * fx_combs[c][i].size);
            fx_combs[c][i].store = 0.0f;
        }
        for (i = 0; i < FX_ALLPASSES; i++) memset(fx_allpasses[c][i].buffer, 0, sizeof(float) * fx_allpasses[c][i].size);
    }
    memset(fx_chorus_line, 0, sizeof(float) * fx_chorus_size);
}

static int fx_init(int rate)
{
    double scale = rate / 44100.0;
    int c, i;
    float wet = 0.9f * 3.0f * FX_REVERB_GAIN;   /* the level times Freeverb's scale of the wet */
    float width = FX_WIDTH;

    if (fx_ready) return 1;
    for (c = 0; c < 2; c++)
    {
        for (i = 0; i < FX_COMBS; i++)
        {
            int size = (int)((fx_comb_tuning[i] + c * FX_SPREAD) * scale);
            if (size < 1) size = 1;
            fx_combs[c][i].buffer = (float *)calloc(size, sizeof(float));
            fx_combs[c][i].size = size;
            if (fx_combs[c][i].buffer == NULL) return 0;
        }
        for (i = 0; i < FX_ALLPASSES; i++)
        {
            int size = (int)((fx_allpass_tuning[i] + c * FX_SPREAD) * scale);
            if (size < 1) size = 1;
            fx_allpasses[c][i].buffer = (float *)calloc(size, sizeof(float));
            fx_allpasses[c][i].size = size;
            if (fx_allpasses[c][i].buffer == NULL) return 0;
        }
    }
    fx_feedback = FX_FEEDBACK;
    fx_damp = 0.0f;
    fx_wet1 = wet * (width / 2.0f + 0.5f);
    fx_wet2 = wet * ((1.0f - width) / 2.0f);

    /* the chorus: a line of 2 + 8 ms, the voices 1/3 of a cycle apart */
    fx_chorus_base = (float)(0.002 * rate);
    fx_chorus_depth = (float)(0.008 * rate);
    fx_chorus_size = (int)(fx_chorus_base + fx_chorus_depth) + 4;
    fx_chorus_line = (float *)calloc(fx_chorus_size, sizeof(float));
    if (fx_chorus_line == NULL) return 0;
    fx_chorus_step = 2.0 * M_PI * 0.3 / rate;
    fx_ready = 1;
    return 1;
}

static void fx_free(void)
{
    int c, i;

    for (c = 0; c < 2; c++)
    {
        for (i = 0; i < FX_COMBS; i++) { free(fx_combs[c][i].buffer); fx_combs[c][i].buffer = NULL; }
        for (i = 0; i < FX_ALLPASSES; i++) { free(fx_allpasses[c][i].buffer); fx_allpasses[c][i].buffer = NULL; }
    }
    free(fx_chorus_line);
    fx_chorus_line = NULL;
    fx_ready = 0;
}

static inline float fx_flush(float x)
{
    return (x > -1e-20f && x < 1e-20f) ? 0.0f : x;
}

/* one frame of the reverb: the mono send in, the two sides out */
static void fx_reverb(float in, float *left, float *right)
{
    float input = in * 2.0f * 0.015f;   /* fluidsynth's 2 * in times Freeverb's fixed gain */
    float out[2];
    int c, i;

    for (c = 0; c < 2; c++)
    {
        float sum = 0.0f;

        for (i = 0; i < FX_COMBS; i++)
        {
            fx_comb *comb = &fx_combs[c][i];
            float y = comb->buffer[comb->index];

            comb->store = fx_flush(y * (1.0f - fx_damp) + comb->store * fx_damp);
            comb->buffer[comb->index] = input + comb->store * fx_feedback;
            if (++comb->index >= comb->size) comb->index = 0;
            sum += y;
        }
        for (i = 0; i < FX_ALLPASSES; i++)
        {
            fx_allpass *ap = &fx_allpasses[c][i];
            float b = ap->buffer[ap->index];

            ap->buffer[ap->index] = fx_flush(sum + b * 0.5f);
            if (++ap->index >= ap->size) ap->index = 0;
            sum = b - sum;
        }
        out[c] = sum;
    }
    *left = out[0] * fx_wet1 + out[1] * fx_wet2;
    *right = out[1] * fx_wet1 + out[0] * fx_wet2;
}

/* one frame of the chorus: the voices alternate between the sides */
static void fx_chorus(float in, float *left, float *right)
{
    float side[2] = { 0.0f, 0.0f };
    int v;

    fx_chorus_line[fx_chorus_index] = in;
    for (v = 0; v < FX_CHORUS_VOICES; v++)
    {
        double phase = fx_chorus_phase + v * (2.0 * M_PI / FX_CHORUS_VOICES);
        float delay = fx_chorus_base + fx_chorus_depth * 0.5f * (1.0f + (float)sin(phase));
        float pos = (float)fx_chorus_index - delay;
        int i0, i1;
        float frac;

        while (pos < 0.0f) pos += (float)fx_chorus_size;
        i0 = (int)pos;
        frac = pos - (float)i0;
        i1 = i0 + 1;
        if (i1 >= fx_chorus_size) i1 = 0;
        side[v & 1] += fx_chorus_line[i0] + (fx_chorus_line[i1] - fx_chorus_line[i0]) * frac;
    }
    if (++fx_chorus_index >= fx_chorus_size) fx_chorus_index = 0;
    fx_chorus_phase += fx_chorus_step;
    if (fx_chorus_phase >= 2.0 * M_PI) fx_chorus_phase -= 2.0 * M_PI;
    /* the width 10: each side less much of the other */
    side[0] *= FX_CHORUS_GAIN;
    side[1] *= FX_CHORUS_GAIN;
    *left = side[0] * fx_chorus_wet1 + side[1] * fx_chorus_wet2;
    *right = side[1] * fx_chorus_wet1 + side[0] * fx_chorus_wet2;
}

static inline short fx_to_short(float v)
{
    return (v < -1.00004566f ? (short)-32768 : (v > 1.00001514f ? (short)32767 : (short)(v * 32767.5f)));
}

/* --- the synthesizer --- */

static tsf *midi_synth = NULL;
static tml_message *midi_first = NULL;
static tml_message *midi_msg = NULL;
static double midi_time_ms = 0.0;
static int midi_loop = 0;
static SDL_atomic_t midi_paused;
static SDL_atomic_t midi_finished;
static char midi_soundfont_path_buf[1024];

/* the synthesizer's output for frames frames, with the effects when they are on */
static void midi_render(short *out, int frames)
{
    float dry[GAME_TSF_SEND_FRAMES * 2], reverb[GAME_TSF_SEND_FRAMES], chorus[GAME_TSF_SEND_FRAMES];
    int channels = Game_AudioChannels == 2 ? 2 : 1;
    int on = SDL_AtomicGet(&midi_effects) && fx_ready;

    if (on && !fx_running) fx_clear();  /* no tail of the last time they were on */
    fx_running = on;
    if (!on)
    {
        tsf_render_short(midi_synth, out, frames, 0);
        return;
    }
    while (frames > 0)
    {
        int n = frames < GAME_TSF_SEND_FRAMES ? frames : GAME_TSF_SEND_FRAMES, i;

        Game_TsfRenderSends(midi_synth, dry, reverb, chorus, n, midi_reverb_send, midi_chorus_send);
        for (i = 0; i < n; i++)
        {
            float rl, rr, cl, cr;

            fx_reverb(reverb[i], &rl, &rr);
            fx_chorus(chorus[i], &cl, &cr);
            if (channels == 2)
            {
                out[2 * i] = fx_to_short(dry[2 * i] + rl + cl);
                out[2 * i + 1] = fx_to_short(dry[2 * i + 1] + rr + cr);
            }
            else
            {
                out[i] = fx_to_short(dry[i] + 0.5f * (rl + rr + cl + cr));
            }
        }
        out += n * channels;
        frames -= n;
    }
}

/* a controller: the sends are the effects', the rest TinySoundFont's */
static void midi_control(int ch, int controller, int value)
{
    if (controller == 91 || controller == 93) fx_set_send(ch & 15, controller, value);
    else Game_TsfChannelMidiControl(midi_synth, ch, controller, value);
}

static void midi_dispatch(double until_ms)
{
    while (midi_msg != NULL && (double)midi_msg->time <= until_ms)
    {
        tml_message *m = midi_msg;
        int ch = m->channel;

        switch (m->type)
        {
        case TML_NOTE_ON:
            if (m->velocity > 0)
                Game_TsfChannelNoteOn(midi_synth, ch, m->key, m->velocity / 127.0f);
            else
                tsf_channel_note_off(midi_synth, ch, m->key);
            break;
        case TML_NOTE_OFF:
            tsf_channel_note_off(midi_synth, ch, m->key);
            break;
        case TML_PROGRAM_CHANGE:
            tsf_channel_set_presetnumber(midi_synth, ch, m->program, ch == 9);
            break;
        case TML_CONTROL_CHANGE:
            midi_control(ch, m->control, m->control_value);
            break;
        case TML_PITCH_BEND:
            tsf_channel_set_pitchwheel(midi_synth, ch, m->pitch_bend);
            break;
        default:
            break;
        }

        midi_msg = m->next;
    }
}

static void midi_mix(void *udata, Uint8 *stream, int len)
{
    int frames;
    double block_ms;

    (void)udata;
    if (midi_synth == NULL || SDL_AtomicGet(&midi_paused)) return;

    frames = len / (int)(sizeof(short) * Game_AudioChannels);
    block_ms = frames * 1000.0 / Game_AudioRate;

    midi_dispatch(midi_time_ms);

    if (midi_msg == NULL)
    {
        if (midi_loop)
        {
            int ch;

            for (ch = 0; ch < 16; ch++) tsf_channel_sounds_off_all(midi_synth, ch);
            midi_msg = midi_first;
            midi_time_ms = 0.0;
            midi_dispatch(0.0);
        }
        else
        {
            SDL_AtomicSet(&midi_finished, 1);
        }
    }

    midi_render((short *)stream, frames);
    midi_time_ms += block_ms;
}

void Game_MidiStop(void)
{
    Mix_HookMusic(NULL, NULL);
    if (midi_synth != NULL)
    {
        int ch;

        for (ch = 0; ch < 16; ch++) tsf_channel_sounds_off_all(midi_synth, ch);
    }
    tml_free(midi_first);
    midi_first = NULL;
    midi_msg = NULL;
    fx_reset_sends();
    SDL_AtomicSet(&midi_paused, 0);
    SDL_AtomicSet(&midi_finished, 0);
}

static const char *midi_soundfont_path(void)
{
    const char *root = Game_DataRoot();
    FILE *f;

    snprintf(midi_soundfont_path_buf, sizeof(midi_soundfont_path_buf),
             "%s/soundfont.sf2", root ? root : ".");
    f = fopen(midi_soundfont_path_buf, "rb");
    if (f == NULL) return NULL;
    fclose(f);
    return midi_soundfont_path_buf;
}

static int midi_load_synth(void)
{
    const char *path = midi_soundfont_path();
    int ch;

    if (path == NULL) { MIDI_LOG("no soundfont.sf2\n"); return 0; }
    midi_synth = tsf_load_filename(path);
    if (midi_synth == NULL) { MIDI_LOG("cannot load %s\n", path); return 0; }
    Game_TsfLevelRegions(midi_synth);

    tsf_set_output(midi_synth,
                   Game_AudioChannels == 2 ? TSF_STEREO_INTERLEAVED : TSF_MONO,
                   Game_AudioRate, 0.0f);
    if (!tsf_set_max_voices(midi_synth, 64)) MIDI_LOG("tsf_set_max_voices failed\n");

    for (ch = 0; ch < 16; ch++)
        tsf_channel_set_presetnumber(midi_synth, ch, 0, ch == 9);

    tsf_set_volume(midi_synth, 0.5f);   /* as the old Mix_VolumeMusic(MIX_MAX_VOLUME / 2) */
    if (!fx_init(Game_AudioRate)) { MIDI_LOG("no memory for the effects\n"); fx_free(); }
    fx_reset_sends();
    MIDI_LOG("soundfont %s\n", path);
    return 1;
}

int Game_MidiPlay(const uint8_t *midi, size_t length, int infinite)
{
    const char *dump = getenv("GAME_MIDI_DUMP");

    if (dump != NULL && dump[0] != 0)
    {
        FILE *f = fopen(dump, "wb");     /* the song as a standard MIDI file, for debugging */

        if (f != NULL)
        {
            fwrite(midi, 1, length, f);
            fclose(f);
        }
    }
    if (midi_synth == NULL && !midi_load_synth()) return 0;
    Game_MidiStop();

    midi_first = tml_load_memory(midi, (int)length);
    if (midi_first == NULL) { MIDI_LOG("tml: bad MIDI\n"); return 0; }

    midi_msg = midi_first;
    midi_time_ms = 0.0;
    midi_loop = infinite;
    SDL_AtomicSet(&midi_paused, 0);
    SDL_AtomicSet(&midi_finished, 0);
    Mix_HookMusic(midi_mix, NULL);
    return 1;
}

void Game_MidiPause(int paused)
{
    SDL_AtomicSet(&midi_paused, paused != 0);
}

int Game_MidiFinished(void)
{
    return SDL_AtomicGet(&midi_finished);
}

/* --- a stream of messages --- */

static Game_MidiPump midi_pump = NULL;
static void *midi_pump_user = NULL;
static SDL_mutex *midi_lock = NULL;

void Game_MidiLock(void)
{
    if (midi_lock == NULL) midi_lock = SDL_CreateMutex();
    SDL_LockMutex(midi_lock);
}

void Game_MidiUnlock(void)
{
    SDL_UnlockMutex(midi_lock);
}

void Game_MidiShortMsg(uint32_t msg)
{
    int ch = msg & 0x0f;
    int d1 = (msg >> 8) & 0x7f;
    int d2 = (msg >> 16) & 0x7f;

    if (midi_synth == NULL) return;
    Game_MidiLock();
    switch (msg & 0xf0)
    {
    case 0x80:
        tsf_channel_note_off(midi_synth, ch, d1);
        break;
    case 0x90:
        if (d2 > 0)
            Game_TsfChannelNoteOn(midi_synth, ch, d1, d2 / 127.0f);
        else
            tsf_channel_note_off(midi_synth, ch, d1);
        break;
    case 0xb0:
        midi_control(ch, d1, d2);
        break;
    case 0xc0:
        tsf_channel_set_presetnumber(midi_synth, ch, d1, ch == 9);
        break;
    case 0xe0:
        tsf_channel_set_pitchwheel(midi_synth, ch, d1 | (d2 << 7));
        break;
    default:    /* aftertouch and the system messages: TinySoundFont has no use for them */
        break;
    }
    Game_MidiUnlock();
}

void Game_MidiReset(void)
{
    int ch;

    if (midi_synth == NULL) return;
    Game_MidiLock();
    for (ch = 0; ch < 16; ch++)
    {
        tsf_channel_sounds_off_all(midi_synth, ch);
        Game_TsfChannelMidiControl(midi_synth, ch, 121, 0);  /* reset all controllers */
        tsf_channel_set_presetnumber(midi_synth, ch, 0, ch == 9);
    }
    fx_reset_sends();
    Game_MidiUnlock();
}

static int midi_stream_hooked;   /* midi_stream_mix is SDL_mixer's music */

static void midi_stream_mix(void *udata, Uint8 *stream, int len)
{
    short *out = (short *)stream;
    int frames = len / (int)(sizeof(short) * Game_AudioChannels);
    int slice = Game_AudioRate * 3 / 2000;   /* 1.5 ms: the timing of the messages */

    (void)udata;
    if (slice < 1) slice = 1;
    Game_MidiLock();
    while (frames > 0)
    {
        int n = (frames < slice) ? frames : slice;

        if (midi_pump != NULL) midi_pump(midi_pump_user, n * 1000.0 / Game_AudioRate);
        midi_render(out, n);
        out += n * Game_AudioChannels;
        frames -= n;
    }
    Game_MidiUnlock();
}

int Game_MidiStreamOpen(Game_MidiPump pump, void *user)
{
    if (midi_synth == NULL && !midi_load_synth()) return 0;
    Game_MidiStop();
    Game_MidiLock();
    midi_pump = pump;
    midi_pump_user = user;
    Game_MidiUnlock();
    Game_MidiReset();
    MIDI_LOG("stream open\n");
    return 1;
}

void Game_MidiStreamPlay(int playing)
{
    playing = playing && (midi_pump != NULL);
    if (playing == midi_stream_hooked) return;
    midi_stream_hooked = playing;
    Mix_HookMusic(playing ? midi_stream_mix : NULL, NULL);
}

void Game_MidiStreamClose(void)
{
    if (midi_pump == NULL) return;
    Game_MidiStreamPlay(0);
    Game_MidiLock();
    midi_pump = NULL;
    midi_pump_user = NULL;
    Game_MidiUnlock();
    Game_MidiReset();
    MIDI_LOG("stream closed\n");
}

void Game_MidiShutdown(void)
{
    Game_MidiStreamClose();
    Game_MidiStop();
    if (midi_synth != NULL)
    {
        tsf_close(midi_synth);
        midi_synth = NULL;
    }
    fx_free();
}

/* --- the settings --- */

int Game_MidiConfig(const char *key, const char *value)
{
    if (strcasecmp(key, "Midi_Effects") != 0) return 0;
    if (strcasecmp(value, "on") == 0) SDL_AtomicSet(&midi_effects, 1);
    else if (strcasecmp(value, "off") == 0) SDL_AtomicSet(&midi_effects, 0);
    else fprintf(stderr, "midi: unknown Midi_Effects: %s\n", value);
    return 1;
}

static void effects_press(void)
{
    SDL_AtomicSet(&midi_effects, !SDL_AtomicGet(&midi_effects));
}

static const char *effects_state(void)
{
    return SDL_AtomicGet(&midi_effects) ? "on" : "off";
}

static const Game_OsdItem effects_item = { "MIDI reverb and chorus", NULL, effects_press, effects_state, "Midi_Effects", NULL, NULL, NULL, NULL };

void Game_MidiAddEffectsItem(void)
{
    Game_OsdAddItem(&effects_item);
}
