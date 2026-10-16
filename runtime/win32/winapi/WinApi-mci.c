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

/* The MCI "cdaudio" device and its aux volume control, over the CD image of
 * Game_cdrom.c, played by Game_music.c.
 *
 * The game's CD code (cd_play, cd_pause, ... and the level music) opens
 * "cdaudio" with the TMSF time format, asks for the number of tracks, their
 * type and length, plays one track with MCI_FROM/MCI_TO, polls MCI_STATUS_MODE
 * to restart it when it has ended (cd_repeat), and pauses by seeking to the
 * position it read before stopping.  Its volume (cd_volume) is the CD line of
 * the aux mixer.
 *
 * Positions are kept here, from the time play started: a CD drive reports
 * them the same way, a sector at a time.  MCI_NOTIFY is not sent; the game
 * does not ask for it. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL.h>

#include "WinApi-game.h"
#include "Game_hooks.h"
#include "Game_memory.h"
#include "Game_cdrom.h"
#include "Game_music.h"

#if defined(PTROFS_64BIT)
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))
#else
#define GUEST_MEM(a) ((void *)(uintptr_t)(a))
#endif

#define MCI_OPEN                0x0803
#define MCI_CLOSE               0x0804
#define MCI_PLAY                0x0806
#define MCI_SEEK                0x0807
#define MCI_STOP                0x0808
#define MCI_PAUSE               0x0809
#define MCI_INFO                0x080A
#define MCI_GETDEVCAPS          0x080B
#define MCI_SET                 0x080D
#define MCI_STATUS              0x0814
#define MCI_RESUME              0x0855

#define MCI_NOTIFY              0x00000001
#define MCI_FROM                0x00000004
#define MCI_TO                  0x00000008
#define MCI_TRACK               0x00000010
#define MCI_OPEN_TYPE           0x00002000
#define MCI_OPEN_TYPE_ID        0x00001000
#define MCI_SEEK_TO_START       0x00000100
#define MCI_SEEK_TO_END         0x00000200
#define MCI_STATUS_ITEM         0x00000100
#define MCI_SET_DOOR_OPEN       0x00000100
#define MCI_SET_DOOR_CLOSED     0x00000200
#define MCI_SET_TIME_FORMAT     0x00000400

#define MCI_STATUS_LENGTH           1
#define MCI_STATUS_POSITION         2
#define MCI_STATUS_NUMBER_OF_TRACKS 3
#define MCI_STATUS_MODE             4
#define MCI_STATUS_MEDIA_PRESENT    5
#define MCI_STATUS_TIME_FORMAT      6
#define MCI_STATUS_READY            7
#define MCI_STATUS_CURRENT_TRACK    8
#define MCI_CDA_STATUS_TYPE_TRACK   0x00004001

#define MCI_MODE_STOP           525
#define MCI_MODE_PLAY           526
#define MCI_MODE_PAUSE          529
#define MCI_CDA_TRACK_AUDIO     1088
#define MCI_CDA_TRACK_OTHER     1089

#define MCI_FORMAT_MILLISECONDS 0
#define MCI_FORMAT_MSF          2
#define MCI_FORMAT_TMSF         10

#define MCI_DEVTYPE_CD_AUDIO    516

#define MCIERR_INVALID_DEVICE_ID    257
#define MCIERR_UNRECOGNIZED_COMMAND 261
#define MCIERR_INVALID_DEVICE_NAME  263
#define MCIERR_DEVICE_NOT_READY     276
#define MCIERR_OUTOFRANGE           282
#define MCIERR_BAD_TIME_FORMAT      293
#define MCIERR_NULL_PARAMETER_BLOCK 297

#define MMSYSERR_BADDEVICEID    2

#define AUXCAPS_CDAUDIO         1
#define AUXCAPS_VOLUME          0x0001
#define AUXCAPS_LRVOLUME        0x0002

#define CD_DEVICE_ID 1
#define FRAMES_PER_SECOND 75

#ifdef __cplusplus
extern "C" {
#endif

static int cd_open;
static uint32_t time_format = MCI_FORMAT_MSF;
static int mode = MCI_MODE_STOP;
/* while playing: absolute disc frames, from the first track's start */
static int32_t play_from, play_to;
static uint32_t play_ticks;     /* SDL_GetTicks() at play_from */
static int32_t stopped_at;      /* the position while not playing */
static uint32_t cd_volume = 0xFFFFFFFFu;

static int trace_mci(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("DEBUG_MCI")) != NULL;
    return enabled;
}

/* ---- the disc's layout, in frames from track 1 ---- */

static int last_track(void)
{
    return Game_CdLastTrack();
}

static int32_t track_start(int track)
{
    int32_t start = 0;
    for (int number = 1; number < track; number++) start += Game_CdTrackSectors(number);
    return start;
}

static int32_t disc_length(void)
{
    return track_start(last_track() + 1);
}

static int track_at(int32_t frames)
{
    int last = last_track();
    for (int number = 1; number < last; number++) {
        if (frames < track_start(number + 1)) return number;
    }
    return last;
}

/* ---- time formats ---- */

static uint32_t pack_msf(int32_t frames)
{
    if (frames < 0) frames = 0;
    return (uint32_t)(frames / (60 * FRAMES_PER_SECOND)) |
           ((uint32_t)(frames / FRAMES_PER_SECOND % 60) << 8) |
           ((uint32_t)(frames % FRAMES_PER_SECOND) << 16);
}

static int32_t unpack_msf(uint32_t value)
{
    return ((int32_t)(value & 0xFF) * 60 + (int32_t)((value >> 8) & 0xFF)) * FRAMES_PER_SECOND +
           (int32_t)((value >> 16) & 0xFF);
}

/* a position in the current time format, as absolute frames */
static int32_t from_format(uint32_t value)
{
    switch (time_format) {
    case MCI_FORMAT_MILLISECONDS:
        return (int32_t)((uint64_t)value * FRAMES_PER_SECOND / 1000);
    case MCI_FORMAT_TMSF:
        return track_start((int)(value & 0xFF)) +
               ((int32_t)((value >> 8) & 0xFF) * 60 + (int32_t)((value >> 16) & 0xFF)) * FRAMES_PER_SECOND +
               (int32_t)((value >> 24) & 0xFF);
    default:
        return unpack_msf(value);
    }
}

/* absolute frames in the current time format */
static uint32_t to_format(int32_t frames)
{
    switch (time_format) {
    case MCI_FORMAT_MILLISECONDS:
        return (uint32_t)((uint64_t)frames * 1000 / FRAMES_PER_SECOND);
    case MCI_FORMAT_TMSF: {
        int track = track_at(frames);
        return (uint32_t)track | (pack_msf(frames - track_start(track)) << 8);
    }
    default:
        return pack_msf(frames);
    }
}

/* a length: TMSF has none of its own and gives it as MSF */
static uint32_t length_format(int32_t frames)
{
    return (time_format == MCI_FORMAT_MILLISECONDS) ? (uint32_t)((uint64_t)frames * 1000 / FRAMES_PER_SECOND)
                                                     : pack_msf(frames);
}

/* ---- play ---- */

static int32_t current_position(void)
{
    if (mode != MCI_MODE_PLAY) return stopped_at;
    int32_t frames = play_from + (int32_t)((uint64_t)(SDL_GetTicks() - play_ticks) * FRAMES_PER_SECOND / 1000);
    return (frames < play_to) ? frames : play_to;
}

/* Game_music ends a track by itself; that is the end of play here too */
static void update_mode(void)
{
    if (mode == MCI_MODE_PLAY && !Game_MusicPlaying()) {
        stopped_at = play_to;
        mode = MCI_MODE_STOP;
    }
}

static uint32_t start_play(int32_t from, int32_t to)
{
    int track = track_at(from);

    if (from < 0 || from >= disc_length() || to <= from) return MCIERR_OUTOFRANGE;
    if (!Game_CdTrackIsAudio(track)) return MCIERR_OUTOFRANGE;
    /* the audio tracks of one image follow each other: play runs across them */
    Game_MusicPlay(track, (uint32_t)(from - track_start(track)), (uint32_t)(to - from));
    play_from = from;
    play_to = to;
    play_ticks = SDL_GetTicks();
    mode = MCI_MODE_PLAY;
    if (trace_mci()) fprintf(stderr, "[mci play track %d +%d frames, %d frames]\n", track, from - track_start(track), to - from);
    return 0;
}

static void stop_play(int pause)
{
    stopped_at = current_position();
    if (pause && mode == MCI_MODE_PLAY) {
        Game_MusicPause();
        mode = MCI_MODE_PAUSE;
    } else {
        Game_MusicHalt();
        mode = MCI_MODE_STOP;
    }
}

/* ---- commands ---- */

static uint32_t cd_status(uint32_t flags, uint32_t *parms)
{
    /* MCI_STATUS_PARMS: dwCallback, dwReturn, dwItem, dwTrack */
    uint32_t track = parms[3];
    int last = last_track();

    if (!(flags & MCI_STATUS_ITEM)) return MCIERR_UNRECOGNIZED_COMMAND;
    if ((flags & MCI_TRACK) && (track < 1 || (int)track > last)) return MCIERR_OUTOFRANGE;
    update_mode();
    switch (parms[2]) {
    case MCI_STATUS_LENGTH:
        parms[1] = length_format((flags & MCI_TRACK) ? Game_CdTrackSectors((int)track) : disc_length());
        return 0;
    case MCI_STATUS_POSITION:
        parms[1] = to_format((flags & MCI_TRACK) ? track_start((int)track) : current_position());
        return 0;
    case MCI_STATUS_NUMBER_OF_TRACKS:
        parms[1] = (uint32_t)last;
        return 0;
    case MCI_STATUS_MODE:
        parms[1] = (uint32_t)mode;
        return 0;
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY:
        parms[1] = 1;
        return 0;
    case MCI_STATUS_TIME_FORMAT:
        parms[1] = time_format;
        return 0;
    case MCI_STATUS_CURRENT_TRACK:
        parms[1] = (uint32_t)track_at(current_position());
        return 0;
    case MCI_CDA_STATUS_TYPE_TRACK:
        if (!(flags & MCI_TRACK)) return MCIERR_OUTOFRANGE;
        parms[1] = Game_CdTrackIsAudio((int)track) ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER;
        return 0;
    }
    return MCIERR_UNRECOGNIZED_COMMAND;
}

uint32_t CCALL mciSendCommandA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t *parms = (uint32_t *)GUEST_MEM(a3);
    uint32_t result;

    if (a1 == MCI_OPEN) {
        /* MCI_OPEN_PARMS: dwCallback, wDeviceID, lpstrDeviceType, lpstrElementName, lpstrAlias */
        int cdaudio;

        if (parms == NULL) return MCIERR_NULL_PARAMETER_BLOCK;
        if (a2 & MCI_OPEN_TYPE_ID) {
            cdaudio = (parms[2] & 0xFFFF) == MCI_DEVTYPE_CD_AUDIO;
        } else {
            cdaudio = (a2 & MCI_OPEN_TYPE) && parms[2] != 0 && strcasecmp((const char *)GUEST_MEM(parms[2]), "cdaudio") == 0;
        }
        if (!cdaudio) result = MCIERR_INVALID_DEVICE_NAME;
        /* no image, or one without music, or music off: a drive with no audio CD in it */
        else if (disc_length() == 0 || !Game_MusicEnabled()) result = MCIERR_DEVICE_NOT_READY;
        else {
            if (!cd_open) {
                time_format = MCI_FORMAT_MSF;
                mode = MCI_MODE_STOP;
                stopped_at = 0;
            }
            cd_open = 1;
            parms[1] = CD_DEVICE_ID;
            result = 0;
        }
        if (trace_mci()) fprintf(stderr, "[mci open flags %x -> %u, %d tracks]\n", a2, result, last_track());
        return result;
    }

    if (a0 != CD_DEVICE_ID || !cd_open) return MCIERR_INVALID_DEVICE_ID;

    switch (a1) {
    case MCI_CLOSE:
        stop_play(0);
        cd_open = 0;
        result = 0;
        break;
    case MCI_SET:
        result = 0;
        if (a2 & MCI_SET_TIME_FORMAT) {
            /* MCI_SET_PARMS: dwCallback, dwTimeFormat, dwAudio */
            if (parms == NULL) return MCIERR_NULL_PARAMETER_BLOCK;
            if (parms[1] != MCI_FORMAT_MILLISECONDS && parms[1] != MCI_FORMAT_MSF && parms[1] != MCI_FORMAT_TMSF) result = MCIERR_BAD_TIME_FORMAT;
            else time_format = parms[1];
        }
        /* door, audio channels: nothing to do */
        break;
    case MCI_STATUS:
        if (parms == NULL) return MCIERR_NULL_PARAMETER_BLOCK;
        result = cd_status(a2, parms);
        break;
    case MCI_PLAY: {
        /* MCI_PLAY_PARMS: dwCallback, dwFrom, dwTo */
        int32_t from, to;
        update_mode();
        from = (parms != NULL && (a2 & MCI_FROM)) ? from_format(parms[1]) : current_position();
        to = (parms != NULL && (a2 & MCI_TO)) ? from_format(parms[2]) : disc_length();
        result = start_play(from, to);
        break;
    }
    case MCI_SEEK:
        /* MCI_SEEK_PARMS: dwCallback, dwTo */
        stop_play(0);
        if (a2 & MCI_SEEK_TO_START) stopped_at = track_start(1);
        else if (a2 & MCI_SEEK_TO_END) stopped_at = disc_length();
        else if ((a2 & MCI_TO) && parms != NULL) stopped_at = from_format(parms[1]);
        result = (stopped_at >= 0 && stopped_at <= disc_length()) ? 0 : MCIERR_OUTOFRANGE;
        if (result != 0) stopped_at = 0;
        break;
    case MCI_STOP:
        stop_play(0);
        result = 0;
        break;
    case MCI_PAUSE:
        update_mode();
        stop_play(1);
        result = 0;
        break;
    case MCI_RESUME:
        result = 0;
        if (mode == MCI_MODE_PAUSE) {
            Game_MusicResume();
            play_from = stopped_at;
            play_ticks = SDL_GetTicks();
            mode = MCI_MODE_PLAY;
        }
        break;
    default:
        result = MCIERR_UNRECOGNIZED_COMMAND;
        break;
    }

    if (a2 & MCI_NOTIFY) {
        static int reported;
        if (!reported++) fprintf(stderr, "mciSendCommand: MCI_NOTIFY is not sent\n");
    }
    if (trace_mci() && a1 != MCI_STATUS) fprintf(stderr, "[mci %x flags %x -> %u]\n", a1, a2, result);
    return result;
}

uint32_t CCALL mciGetErrorStringA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    char *text = (char *)GUEST_MEM(a1);
    const char *message;

    switch (a0) {
    case MCIERR_INVALID_DEVICE_ID:    message = "Invalid MCI device ID."; break;
    case MCIERR_UNRECOGNIZED_COMMAND: message = "The driver cannot recognize the specified command."; break;
    case MCIERR_INVALID_DEVICE_NAME:  message = "The specified device is not open or is not recognized by MCI."; break;
    case MCIERR_DEVICE_NOT_READY:     message = "The device is not ready (no CD image with audio tracks: see $" GAME_ENV("CD_IMAGE") ")."; break;
    case MCIERR_OUTOFRANGE:           message = "The specified parameter is out of range for the specified command."; break;
    case MCIERR_BAD_TIME_FORMAT:      message = "The specified value for the time format is invalid."; break;
    default:                          message = "Unknown MCI error."; break;
    }
    if (text == NULL || a2 == 0) return 0;
    snprintf(text, a2, "%s", message);
    return 1;
}

/* ---- the CD line of the aux mixer: the music volume ---- */

uint32_t CCALL auxGetNumDevs_c(void)
{
    return 1;
}

uint32_t CCALL auxGetDevCapsA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    /* AUXCAPSA: wMid, wPid, vDriverVersion, szPname[32], wTechnology, wReserved1, dwSupport */
    uint8_t caps[48];

    if (a0 != 0) return MMSYSERR_BADDEVICEID;
    memset(caps, 0, sizeof(caps));
    caps[4] = 1;                                    /* version 1.0 */
    strcpy((char *)caps + 8, "CD Audio");
    caps[40] = AUXCAPS_CDAUDIO;
    caps[44] = AUXCAPS_VOLUME | AUXCAPS_LRVOLUME;
    if (a1 != 0) memcpy(GUEST_MEM(a1), caps, (a2 < sizeof(caps)) ? a2 : sizeof(caps));
    return 0;
}

uint32_t CCALL auxGetVolume_c(uint32_t a0, uint32_t a1)
{
    if (a0 != 0) return MMSYSERR_BADDEVICEID;
    if (a1 != 0) *(uint32_t *)GUEST_MEM(a1) = cd_volume;
    return 0;
}

uint32_t CCALL auxSetVolume_c(uint32_t a0, uint32_t a1)
{
    uint32_t left, right;

    if (a0 != 0) return MMSYSERR_BADDEVICEID;
    cd_volume = a1;
    /* low word left, high word right; SDL_mixer has one music volume */
    left = a1 & 0xFFFF;
    right = a1 >> 16;
    Game_MusicVolume((int)(((left > right) ? left : right) * 128 / 0xFFFF));
    if (trace_mci()) fprintf(stderr, "[aux volume %08x]\n", a1);
    return 0;
}

#ifdef __cplusplus
}
#endif
