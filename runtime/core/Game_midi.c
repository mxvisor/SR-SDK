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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_midi.h"
#include "Game_vars.h"
#include "Game_vfs.h"
#include "tsf.h"
#include "tml.h"

extern "C" int Game_TsfChannelNoteOn(tsf *f, int channel, int key, float vel);
extern "C" void Game_TsfLevelRegions(tsf *f);
extern "C" int Game_TsfChannelMidiControl(tsf *f, int channel, int controller, int value);

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

/* --- the synthesizer --- */

static tsf *midi_synth = NULL;
static tml_message *midi_first = NULL;
static tml_message *midi_msg = NULL;
static double midi_time_ms = 0.0;
static int midi_loop = 0;
static SDL_atomic_t midi_paused;
static SDL_atomic_t midi_finished;
static char midi_soundfont_path_buf[1024];

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
            Game_TsfChannelMidiControl(midi_synth, ch, m->control, m->control_value);
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

    tsf_render_short(midi_synth, (short *)stream, frames, 0);
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
        Game_TsfChannelMidiControl(midi_synth, ch, d1, d2);
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
        tsf_render_short(midi_synth, out, n, 0);
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
}
