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

/* winmm's MIDI stream (midiStream*) and the MIDI output calls a stream takes
 * (midiOut*), played by the synthesizer of Game_midi.c.
 *
 * One stream, on the one device there is.  The game queues buffers of
 * MIDIEVENTs (delta time in ticks, stream id, event) with midiStreamOut; the
 * audio thread plays them through the pump of Game_MidiStreamOpen, at the
 * time division and tempo the stream has.  A buffer played to its end is
 * marked done and its MOM_DONE callback goes, as the timer callbacks of
 * WinApi-winmm.c do, through the input queue of the window's thread
 * (EC_WINAPI_MIDI): the game's callback runs on the guest CPU there, between
 * its messages, and not on the audio thread.  Only CALLBACK_FUNCTION and
 * CALLBACK_NULL are served.  WINAPI_MIDI_LOG=1 logs the calls. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_midi.h"
#include "Game_hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

void Winapi_InputPut(const SDL_Event *event);

#define MMSYSERR_NOERROR        0
#define MMSYSERR_BADDEVICEID    2
#define MMSYSERR_ALLOCATED      4
#define MMSYSERR_INVALHANDLE    5
#define MMSYSERR_NODRIVER       6
#define MMSYSERR_INVALFLAG      10
#define MMSYSERR_INVALPARAM     11
#define MIDIERR_UNPREPARED      64
#define MIDIERR_STILLPLAYING    65

#define CALLBACK_TYPEMASK       0x00070000
#define CALLBACK_NULL           0x00000000
#define CALLBACK_FUNCTION       0x00030000

#define MOM_OPEN                0x3C7
#define MOM_CLOSE               0x3C8
#define MOM_DONE                0x3C9
#define MOM_POSITIONCB          0x3CA

#define MHDR_DONE               0x00000001
#define MHDR_PREPARED           0x00000002
#define MHDR_INQUEUE            0x00000004
#define MHDR_ISSTRM             0x00000008

#define MEVT_F_LONG             0x80000000
#define MEVT_F_CALLBACK         0x40000000
#define MEVT_SHORTMSG           0x00
#define MEVT_TEMPO              0x01

#define MIDIPROP_SET            0x80000000
#define MIDIPROP_GET            0x40000000
#define MIDIPROP_TIMEDIV        0x00000001
#define MIDIPROP_TEMPO          0x00000002

#define TIME_MS                 0x0001
#define TIME_TICKS              0x0020

/* the stream's handle, as the game sees it */
#define STREAM_HANDLE           0x4D53   /* "SM" */

/* MIDIHDR of the guest: lpData, dwBufferLength, dwBytesRecorded, dwUser,
 * dwFlags, lpNext, reserved, dwOffset, dwReserved[8] */
#define HDR_DATA(h)             (((uint32_t *)GAME_GUEST32_TO_PTR(h))[0])
#define HDR_RECORDED(h)         (((uint32_t *)GAME_GUEST32_TO_PTR(h))[2])
#define HDR_FLAGS(h)            (((uint32_t *)GAME_GUEST32_TO_PTR(h))[4])
#define HDR_OFFSET(h)           (((uint32_t *)GAME_GUEST32_TO_PTR(h))[7])

#define QUEUE_SIZE              256

static struct {
    int open;
    int playing;
    uint32_t callback, instance, callback_type;
    uint32_t timediv;           /* ticks per quarter note */
    uint32_t tempo;             /* microseconds per quarter note */
    uint32_t queue[QUEUE_SIZE]; /* the guest's MIDIHDRs, the first one playing */
    unsigned int first, count;
    uint32_t offset;            /* in the first buffer */
    int have_wait;
    double wait;                /* ticks before the event at offset */
    double ticks, ms;           /* the position */
} stream;

static int midi_log(void)
{
    static int on = -1;

    if (on < 0) on = (getenv("WINAPI_MIDI_LOG") != NULL);
    return on;
}

static void post_callback(uint32_t msg, uint32_t param1)
{
    SDL_Event event;

    if ((stream.callback_type != CALLBACK_FUNCTION) || (stream.callback == 0)) return;

    SDL_zero(event);
    event.type = SDL_USEREVENT;
    event.user.code = EC_WINAPI_MIDI;
    event.user.windowID = msg;
    event.user.data1 = (void *)(uintptr_t)stream.callback;
    event.user.data2 = (void *)(uintptr_t)param1;
    Winapi_InputPut(&event);
}

/* the arguments of the callback of an EC_WINAPI_MIDI event, for WinApi-user32.c:
 * hmo, wMsg, dwInstance, dwParam1, dwParam2 */
void Winapi_MidiCallbackArgs(const SDL_UserEvent *event, uint32_t *args)
{
    args[0] = STREAM_HANDLE;
    args[1] = event->windowID;
    args[2] = stream.instance;
    args[3] = (uint32_t)(uintptr_t)event->data2;
    args[4] = 0;
}

/* the first buffer is done; Game_MidiLock is held */
static void buffer_done(void)
{
    uint32_t hdr = stream.queue[stream.first];

    HDR_FLAGS(hdr) = (HDR_FLAGS(hdr) & ~MHDR_INQUEUE) | MHDR_DONE;
    stream.first = (stream.first + 1) % QUEUE_SIZE;
    stream.count--;
    stream.offset = 0;
    stream.have_wait = 0;
    if (midi_log()) fprintf(stderr, "midi: buffer %08x done\n", hdr);
    post_callback(MOM_DONE, hdr);
}

static void notes_off(void)
{
    int ch;

    for (ch = 0; ch < 16; ch++)
    {
        Game_MidiShortMsg(0xB0 | ch | (123 << 8));  /* all notes off */
        Game_MidiShortMsg(0xB0 | ch | (64 << 8));   /* sustain off */
    }
}

/* the audio thread: plays ms more of the stream */
static void stream_pump(void *user, double ms)
{
    double avail;

    (void)user;
    if (!stream.playing) return;
    if (stream.count == 0) return;  /* starved: the time waits for the next buffer */

    avail = ms * 1000.0 * (double)stream.timediv / (double)stream.tempo;
    stream.ms += ms;

    while (stream.count != 0)
    {
        uint32_t hdr = stream.queue[stream.first];
        uint32_t size = HDR_RECORDED(hdr);
        const uint8_t *data = (const uint8_t *)GUEST_MEM(HDR_DATA(hdr));
        uint32_t delta, event, length;

        if ((data == NULL) || (stream.offset + 12 > size))
        {
            buffer_done();
            continue;
        }

        memcpy(&delta, data + stream.offset, 4);
        memcpy(&event, data + stream.offset + 8, 4);
        if (!stream.have_wait)
        {
            stream.wait = delta;
            stream.have_wait = 1;
        }
        if (stream.wait > avail)
        {
            stream.wait -= avail;
            stream.ticks += avail;
            return;
        }
        avail -= stream.wait;
        stream.ticks += stream.wait;
        stream.have_wait = 0;

        length = 12;
        if (event & MEVT_F_LONG)
        {
            length += ((event & 0xFFFFFF) + 3) & ~3u;   /* sysex, comments: skipped */
        }
        else
        {
            switch ((event >> 24) & 0x3F)
            {
            case MEVT_SHORTMSG:
                Game_MidiShortMsg(event & 0xFFFFFF);
                break;
            case MEVT_TEMPO:
                if ((event & 0xFFFFFF) != 0) stream.tempo = event & 0xFFFFFF;
                break;
            default:    /* MEVT_NOP */
                break;
            }
        }
        if (event & MEVT_F_CALLBACK)
        {
            HDR_OFFSET(hdr) = stream.offset;
            post_callback(MOM_POSITIONCB, hdr);
        }
        stream.offset += length;
    }
}

/* returns every queued buffer as done */
static void stream_flush(void)
{
    while (stream.count != 0) buffer_done();
    stream.ticks = 0.0;
    stream.ms = 0.0;
}

uint32_t CCALL midiStreamOpen_c(uint32_t phms, uint32_t puDeviceID, uint32_t cMidi, uint32_t dwCallback, uint32_t dwInstance, uint32_t fdwOpen)
{
    if (midi_log()) fprintf(stderr, "midi: midiStreamOpen dev %d n %u cb %08x inst %08x flags %08x\n",
                            puDeviceID ? (int)*(uint32_t *)GUEST_MEM(puDeviceID) : -2, cMidi, dwCallback, dwInstance, fdwOpen);
    if ((phms == 0) || (cMidi != 1)) return MMSYSERR_INVALPARAM;
    if (((fdwOpen & CALLBACK_TYPEMASK) != CALLBACK_FUNCTION) && ((fdwOpen & CALLBACK_TYPEMASK) != CALLBACK_NULL))
    {
        fprintf(stderr, "midiStreamOpen: callback type %08x is not served\n", fdwOpen & CALLBACK_TYPEMASK);
        return MMSYSERR_INVALFLAG;
    }
    if (stream.open) return MMSYSERR_NODRIVER;  /* the device is taken */

    memset(&stream, 0, sizeof(stream));
    stream.callback = dwCallback;
    stream.instance = dwInstance;
    stream.callback_type = fdwOpen & CALLBACK_TYPEMASK;
    stream.timediv = 96;
    stream.tempo = 500000;

    if (!Game_MidiStreamOpen(stream_pump, NULL))
    {
        fprintf(stderr, "midiStreamOpen: no soundfont.sf2, no MIDI\n");
        return MMSYSERR_NODRIVER;
    }
    stream.open = 1;    /* paused, as a new stream is */
    *(uint32_t *)GUEST_MEM(phms) = STREAM_HANDLE;
    post_callback(MOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamClose_c(uint32_t hms)
{
    if (midi_log()) fprintf(stderr, "midi: midiStreamClose\n");
    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if (stream.count != 0) return MIDIERR_STILLPLAYING;

    Game_MidiStreamClose();
    stream.open = 0;
    post_callback(MOM_CLOSE, 0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamOut_c(uint32_t hms, uint32_t pmh, uint32_t cbmh)
{
    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((pmh == 0) || (cbmh < 32)) return MMSYSERR_INVALPARAM;
    if (!(HDR_FLAGS(pmh) & MHDR_PREPARED)) return MIDIERR_UNPREPARED;
    if (HDR_FLAGS(pmh) & MHDR_INQUEUE) return MIDIERR_STILLPLAYING;

    Game_MidiLock();
    if (stream.count == QUEUE_SIZE)
    {
        Game_MidiUnlock();
        return MMSYSERR_INVALPARAM;
    }
    HDR_FLAGS(pmh) = (HDR_FLAGS(pmh) & ~MHDR_DONE) | MHDR_INQUEUE | MHDR_ISSTRM;
    HDR_OFFSET(pmh) = 0;
    stream.queue[(stream.first + stream.count) % QUEUE_SIZE] = pmh;
    stream.count++;
    Game_MidiUnlock();
    if (midi_log()) fprintf(stderr, "midi: midiStreamOut %08x, %u bytes\n", pmh, HDR_RECORDED(pmh));
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamPause_c(uint32_t hms)
{
    if (midi_log()) fprintf(stderr, "midi: midiStreamPause\n");
    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    Game_MidiLock();
    if (stream.playing) notes_off();
    stream.playing = 0;
    Game_MidiUnlock();
    Game_MidiStreamPlay(0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamRestart_c(uint32_t hms)
{
    if (midi_log()) fprintf(stderr, "midi: midiStreamRestart\n");
    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    Game_MidiLock();
    stream.playing = 1;
    Game_MidiUnlock();
    Game_MidiStreamPlay(1);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamStop_c(uint32_t hms)
{
    if (midi_log()) fprintf(stderr, "midi: midiStreamStop\n");
    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    Game_MidiLock();
    notes_off();
    stream_flush();
    stream.playing = 0;
    Game_MidiUnlock();
    Game_MidiStreamPlay(0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamPosition_c(uint32_t hms, uint32_t pmmt, uint32_t cbmmt)
{
    uint32_t *mmt;

    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((pmmt == 0) || (cbmmt < 8)) return MMSYSERR_INVALPARAM;

    mmt = (uint32_t *)GUEST_MEM(pmmt);
    Game_MidiLock();
    if (mmt[0] == TIME_TICKS)
    {
        mmt[1] = (uint32_t)stream.ticks;
    }
    else
    {
        mmt[0] = TIME_MS;
        mmt[1] = (uint32_t)stream.ms;
    }
    Game_MidiUnlock();
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiStreamProperty_c(uint32_t hms, uint32_t lppropdata, uint32_t dwProperty)
{
    uint32_t *prop;
    uint32_t *value;

    if ((hms != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if (lppropdata == 0) return MMSYSERR_INVALPARAM;
    if (((dwProperty & (MIDIPROP_SET | MIDIPROP_GET)) == 0) || ((dwProperty & (MIDIPROP_SET | MIDIPROP_GET)) == (MIDIPROP_SET | MIDIPROP_GET))) return MMSYSERR_INVALPARAM;

    prop = (uint32_t *)GUEST_MEM(lppropdata);  /* cbStruct, then the value */
    if (prop[0] < 8) return MMSYSERR_INVALPARAM;
    if ((dwProperty & 0xFFFF) == MIDIPROP_TIMEDIV) value = &stream.timediv;
    else if ((dwProperty & 0xFFFF) == MIDIPROP_TEMPO) value = &stream.tempo;
    else return MMSYSERR_INVALPARAM;

    Game_MidiLock();
    if (dwProperty & MIDIPROP_SET)
    {
        /* SMPTE divisions (the high bit) are not served: a quarter of 96 ticks */
        if (value == &stream.timediv) *value = (prop[1] & 0x8000) ? 96 : (prop[1] & 0x7FFF);
        else *value = prop[1];
        if (*value == 0) *value = (value == &stream.timediv) ? 96 : 500000;
    }
    else
    {
        prop[1] = *value;
    }
    Game_MidiUnlock();
    if (midi_log()) fprintf(stderr, "midi: midiStreamProperty %08x: %u\n", dwProperty, prop[1]);
    return MMSYSERR_NOERROR;
}

/* The MIDI output device (midiOut*): the one device, this synthesizer, also as
 * MIDI_MAPPER (-1).  midiOutOpen takes it as midiStreamOpen does, with no
 * buffers: the game plays it with midiOutShortMsg itself (from its timer).
 * Its technology tells a game which of its arrangements to play: MOD_SWSYNTH
 * (7), as Windows' own software synthesizer, or GAME_WINAPI_MIDI_TECHNOLOGY of
 * Game_hooks.h (MOD_SYNTH 2, MOD_FMSYNTH 4, MOD_WAVETABLE 6, ...). */
#ifndef GAME_WINAPI_MIDI_TECHNOLOGY
#define GAME_WINAPI_MIDI_TECHNOLOGY 7
#endif

uint32_t CCALL midiOutGetDevCapsA_c(uint32_t uDeviceID, uint32_t pmoc, uint32_t cbmoc)
{
    /* MIDIOUTCAPSA: wMid, wPid, vDriverVersion, szPname[32], wTechnology,
     * wVoices, wNotes, wChannelMask, dwSupport */
    uint8_t caps[52];
    static const char name[] = "General MIDI Synthesizer";

    if (pmoc == 0) return MMSYSERR_INVALPARAM;
    if ((uDeviceID != 0) && (uDeviceID != 0xFFFFFFFFu)) return MMSYSERR_BADDEVICEID;
    memset(caps, 0, sizeof(caps));
    caps[0] = 1;                                    /* MM_MICROSOFT */
    caps[5] = 1;                                    /* version 1.0 */
    memcpy(caps + 8, name, sizeof(name));
    caps[40] = GAME_WINAPI_MIDI_TECHNOLOGY;
    caps[42] = 32;                                  /* voices */
    caps[44] = 32;                                  /* notes */
    caps[46] = 0xFF; caps[47] = 0xFF;               /* all 16 channels */
    memcpy(GUEST_MEM(pmoc), caps, (cbmoc < sizeof(caps)) ? cbmoc : sizeof(caps));
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutOpen_c(uint32_t lphmo, uint32_t uDeviceID, uint32_t dwCallback, uint32_t dwInstance, uint32_t fdwOpen)
{
    if (midi_log()) fprintf(stderr, "midi: midiOutOpen dev %d cb %08x inst %08x flags %08x\n", (int)uDeviceID, dwCallback, dwInstance, fdwOpen);
    if (lphmo == 0) return MMSYSERR_INVALPARAM;
    if ((uDeviceID != 0) && (uDeviceID != 0xFFFFFFFFu)) return MMSYSERR_BADDEVICEID;
    if (((fdwOpen & CALLBACK_TYPEMASK) != CALLBACK_FUNCTION) && ((fdwOpen & CALLBACK_TYPEMASK) != CALLBACK_NULL))
    {
        fprintf(stderr, "midiOutOpen: callback type %08x is not served\n", fdwOpen & CALLBACK_TYPEMASK);
        return MMSYSERR_INVALFLAG;
    }
    if (stream.open) return MMSYSERR_ALLOCATED;

    memset(&stream, 0, sizeof(stream));
    stream.callback = dwCallback;
    stream.instance = dwInstance;
    stream.callback_type = fdwOpen & CALLBACK_TYPEMASK;
    stream.timediv = 96;
    stream.tempo = 500000;

    if (!Game_MidiStreamOpen(stream_pump, NULL))
    {
        fprintf(stderr, "midiOutOpen: no soundfont.sf2, no MIDI\n");
        return MMSYSERR_NODRIVER;
    }
    stream.open = 1;
    Game_MidiStreamPlay(1);     /* the synthesizer sounds from now on */
    *(uint32_t *)GUEST_MEM(lphmo) = STREAM_HANDLE;
    post_callback(MOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutClose_c(uint32_t hmo)
{
    if (midi_log()) fprintf(stderr, "midi: midiOutClose\n");
    if ((hmo != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if (stream.count != 0) return MIDIERR_STILLPLAYING;

    Game_MidiStreamClose();
    stream.open = 0;
    post_callback(MOM_CLOSE, 0);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutShortMsg_c(uint32_t hmo, uint32_t dwMsg)
{
    if ((hmo != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    Game_MidiShortMsg(dwMsg);
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutReset_c(uint32_t hmo)
{
    if (midi_log()) fprintf(stderr, "midi: midiOutReset\n");
    if ((hmo != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    Game_MidiLock();
    stream_flush();
    stream.playing = 0;
    Game_MidiReset();
    Game_MidiUnlock();
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutPrepareHeader_c(uint32_t hmo, uint32_t pmh, uint32_t cbmh)
{
    if ((hmo != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((pmh == 0) || (cbmh < 32)) return MMSYSERR_INVALPARAM;
    HDR_FLAGS(pmh) |= MHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

uint32_t CCALL midiOutUnprepareHeader_c(uint32_t hmo, uint32_t pmh, uint32_t cbmh)
{
    if ((hmo != STREAM_HANDLE) || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((pmh == 0) || (cbmh < 32)) return MMSYSERR_INVALPARAM;
    if (HDR_FLAGS(pmh) & MHDR_INQUEUE) return MIDIERR_STILLPLAYING;
    HDR_FLAGS(pmh) &= ~MHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

#ifdef __cplusplus
}
#endif
