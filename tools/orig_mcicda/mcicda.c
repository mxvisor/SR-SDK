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

/* A "cdaudio" MCI driver for the original Windows game under Wine: it plays
 * the audio tracks of a disc image through waveOut, with no CD drive.
 *
 * Wine's own mcicda opens the drive with ioctls (READ_TOC), so a directory in
 * the role of D: does not do.  This driver takes the place of mcicda.dll
 * (see run_orig_common.sh): the image is the cue sheet in $ORIG_CDAUDIO_CUE
 * (one file for the whole disc, or one file per track), and the commands are
 * the ones winmm passes to a driver (the game's mciSendString/mciSendCommand
 * text is parsed by winmm).  Positions are the disc's: the first track starts
 * at 00:02:00 in MSF, as on a drive.
 *
 * Build: i686-w64-mingw32-gcc -shared -o mcicda.dll mcicda.c mcicda.def -lwinmm */

#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES_PER_SECOND 75
#define SECTOR 2352
#define LEAD_IN 150             /* frames before the first track in MSF */
#define MAX_TRACKS 99
#define BUFFERS 8
#define BUFFER_FRAMES 8         /* sectors per waveOut buffer: 8/75 s */

typedef struct {
    char path[MAX_PATH];
    int audio;
    LONG start;                 /* the first sector of the track in the disc, from track 1 */
    LONG length;
    LONG file_start;            /* the sector of the track's index 01 in its file */
} Track;

static Track track[MAX_TRACKS + 1];
static int track_count;
static CRITICAL_SECTION lock;
static int loaded;

static int opened;
static DWORD time_format = MCI_FORMAT_MSF;
static DWORD mode = MCI_MODE_STOP;
static LONG stopped_at;         /* the position while not playing, absolute sectors from track 1 */

static HANDLE player;
static HANDLE stop_event;
static LONG play_from, play_to;
static UINT device_id;
static volatile LONG played;    /* sectors played since play_from, kept by the player */
static volatile LONG generation;

static int trace;

static void say(const char *format, ...)
{
    va_list args;
    if (!trace) return;
    va_start(args, format);
    fputs("[mcicda ", stderr);
    vfprintf(stderr, format, args);
    fputs("]\n", stderr);
    va_end(args);
}

/* ---- the disc: the cue sheet ---- */

static LONG parse_msf(const char *text)
{
    int minutes = 0, seconds = 0, frames = 0;
    sscanf(text, "%d:%d:%d", &minutes, &seconds, &frames);
    return (minutes * 60 + seconds) * FRAMES_PER_SECOND + frames;
}

static LONG file_sectors(const char *path)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    LONG sectors = 0;
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (GetFileSizeEx(file, &size)) sectors = (LONG)(size.QuadPart / SECTOR);
    CloseHandle(file);
    return sectors;
}

static int load_cue(void)
{
    char cue[MAX_PATH], dir[MAX_PATH], line[512], file_path[MAX_PATH] = "";
    LONG disc = 0;
    int in_track = 0;
    FILE *f;
    char *slash;

    if (GetEnvironmentVariableA("ORIG_CDAUDIO_CUE", cue, sizeof(cue)) == 0) return 0;
    f = fopen(cue, "r");
    if (f == NULL) return 0;
    strcpy(dir, cue);
    slash = strrchr(dir, '/');
    if (strrchr(dir, '\\') > slash) slash = strrchr(dir, '\\');
    if (slash != NULL) slash[1] = '\0'; else dir[0] = '\0';

    track_count = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "FILE", 4) == 0) {
            char *q = strchr(p, '"'), *e = q ? strchr(q + 1, '"') : NULL;
            if (q == NULL || e == NULL) continue;
            *e = '\0';
            snprintf(file_path, sizeof(file_path), "%s%s", dir, q + 1);
            in_track = 0;
        } else if (_strnicmp(p, "TRACK", 5) == 0) {
            int number = atoi(p + 5);
            if (number < 1 || number > MAX_TRACKS || file_path[0] == '\0') continue;
            track_count = number;
            snprintf(track[number].path, sizeof(track[number].path), "%s", file_path);
            track[number].audio = strstr(p, "AUDIO") != NULL;
            in_track = number;
        } else if (_strnicmp(p, "INDEX 01", 8) == 0 && in_track != 0) {
            track[in_track].file_start = parse_msf(p + 8);
        }
    }
    fclose(f);

    /* starts and lengths: a track runs to the next index 01 in its file, or the file's end */
    for (int i = 1; i <= track_count; i++) {
        LONG end = file_sectors(track[i].path);
        if (i < track_count && strcmp(track[i].path, track[i + 1].path) == 0) end = track[i + 1].file_start;
        track[i].length = end - track[i].file_start;
        if (track[i].length < 0) track[i].length = 0;
    }
    /* the disc position of a track: the lengths before it, with the pregaps of a
     * track's own file counted in (redump: one file per track, pregap in front) */
    for (int i = 1; i <= track_count; i++) {
        if (i == 1 || strcmp(track[i].path, track[i - 1].path) != 0) {
            track[i].start = disc + track[i].file_start;
        } else {
            track[i].start = track[i - 1].start + track[i - 1].length;
        }
        disc = track[i].start + track[i].length;
    }
    say("cue %s: %d tracks, %ld sectors", cue, track_count, (long)disc);
    return track_count > 0;
}

static LONG disc_length(void)
{
    return track_count ? track[track_count].start + track[track_count].length : 0;
}

static int track_at(LONG sector)
{
    for (int i = track_count; i > 1; i--) if (sector >= track[i].start) return i;
    return 1;
}

/* ---- time formats ---- */

static DWORD pack_msf(LONG frames)
{
    if (frames < 0) frames = 0;
    return (DWORD)MCI_MAKE_MSF(frames / (60 * FRAMES_PER_SECOND), frames / FRAMES_PER_SECOND % 60, frames % FRAMES_PER_SECOND);
}

/* a position in the current time format, as sectors from track 1 */
static LONG from_format(DWORD value)
{
    switch (time_format) {
    case MCI_FORMAT_MILLISECONDS:
        return (LONG)((unsigned long long)value * FRAMES_PER_SECOND / 1000);
    case MCI_FORMAT_TMSF: {
        int n = MCI_TMSF_TRACK(value);
        if (n < 1 || n > track_count) return -1;
        return track[n].start + (MCI_TMSF_MINUTE(value) * 60 + MCI_TMSF_SECOND(value)) * FRAMES_PER_SECOND + MCI_TMSF_FRAME(value);
    }
    default:
        return ((MCI_MSF_MINUTE(value) * 60 + MCI_MSF_SECOND(value)) * FRAMES_PER_SECOND + MCI_MSF_FRAME(value)) - LEAD_IN;
    }
}

static DWORD to_format(LONG sector)
{
    switch (time_format) {
    case MCI_FORMAT_MILLISECONDS:
        return (DWORD)(((unsigned long long)sector * 1000 + FRAMES_PER_SECOND - 1) / FRAMES_PER_SECOND);
    case MCI_FORMAT_TMSF: {
        int n = track_at(sector);
        LONG in = sector - track[n].start;
        if (in < 0) in = 0;
        return MCI_MAKE_TMSF(n, in / (60 * FRAMES_PER_SECOND), in / FRAMES_PER_SECOND % 60, in % FRAMES_PER_SECOND);
    }
    default:
        return pack_msf(sector + LEAD_IN);
    }
}

/* a length: TMSF gives it as MSF, from zero */
static DWORD length_format(LONG frames)
{
    return time_format == MCI_FORMAT_MILLISECONDS ? (DWORD)((unsigned long long)frames * 1000 / FRAMES_PER_SECOND) : pack_msf(frames);
}

/* ---- play ---- */

static LONG current_position(void)
{
    LONG at = play_from + played;
    if (mode == MCI_MODE_STOP || mode == MCI_MODE_PAUSE) return stopped_at;
    return at < play_to ? at : play_to;
}

typedef struct {
    LONG from, to, token;
    DWORD notify;
} Play;

/* reads sectors [from, from + count) of the disc into out; silence where there is no audio */
static void read_sectors(LONG from, int count, BYTE *out)
{
    memset(out, 0, (size_t)count * SECTOR);
    for (int done = 0; done < count;) {
        int n = track_at(from + done);
        LONG at = from + done, in_track = at - track[n].start;
        int run = count - done;
        HANDLE file;
        DWORD got;
        LARGE_INTEGER offset;

        if (in_track < 0 || in_track >= track[n].length) { done++; continue; }
        if (run > track[n].length - in_track) run = (int)(track[n].length - in_track);
        file = CreateFileA(track[n].path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            offset.QuadPart = (long long)(track[n].file_start + in_track) * SECTOR;
            SetFilePointerEx(file, offset, NULL, FILE_BEGIN);
            ReadFile(file, out + (size_t)done * SECTOR, (DWORD)run * SECTOR, &got, NULL);
            CloseHandle(file);
        }
        done += run;
    }
}

static DWORD WINAPI player_main(LPVOID param)
{
    Play *p = param;
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0};
    HWAVEOUT out;
    WAVEHDR header[BUFFERS];
    BYTE *data = malloc((size_t)BUFFERS * BUFFER_FRAMES * SECTOR);
    HANDLE ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    LONG next = p->from;
    int stopped = 0, finished = 0, in_flight = 0;

    if (waveOutOpen(&out, WAVE_MAPPER, &format, (DWORD_PTR)ready, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        say("waveOutOpen failed");
        free(data);
        CloseHandle(ready);
        free(p);
        return 0;
    }
    memset(header, 0, sizeof(header));
    for (int i = 0; i < BUFFERS; i++) {
        header[i].lpData = (LPSTR)(data + (size_t)i * BUFFER_FRAMES * SECTOR);
        waveOutPrepareHeader(out, &header[i], sizeof(WAVEHDR));
        header[i].dwFlags |= WHDR_DONE;     /* preparing clears it: a buffer is free when it is set */
    }

    while (!stopped) {
        for (int i = 0; i < BUFFERS && next < p->to; i++) {
            int count;
            if (!(header[i].dwFlags & WHDR_DONE)) continue;
            count = p->to - next < BUFFER_FRAMES ? (int)(p->to - next) : BUFFER_FRAMES;
            read_sectors(next, count, (BYTE *)header[i].lpData);
            header[i].dwBufferLength = (DWORD)count * SECTOR;
            header[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(out, &header[i], sizeof(WAVEHDR));
            next += count;
        }
        {
            MMTIME time;
            time.wType = TIME_SAMPLES;
            if (waveOutGetPosition(out, &time, sizeof(time)) == MMSYSERR_NOERROR && time.wType == TIME_SAMPLES) {
                LONG sectors = (LONG)(time.u.sample / (44100 / FRAMES_PER_SECOND));
                if (InterlockedCompareExchange(&generation, 0, 0) == p->token) played = sectors;
            }
        }
        in_flight = 0;
        for (int i = 0; i < BUFFERS; i++) if (!(header[i].dwFlags & WHDR_DONE)) in_flight++;
        if (next >= p->to && in_flight == 0) { finished = 1; break; }
        {
            HANDLE wait[2] = {stop_event, ready};
            DWORD r = WaitForMultipleObjects(2, wait, FALSE, 100);
            if (r == WAIT_OBJECT_0) stopped = 1;
        }
    }

    waveOutReset(out);
    for (int i = 0; i < BUFFERS; i++) waveOutUnprepareHeader(out, &header[i], sizeof(WAVEHDR));
    waveOutClose(out);
    free(data);
    CloseHandle(ready);

    EnterCriticalSection(&lock);
    if (finished && InterlockedCompareExchange(&generation, 0, 0) == p->token && mode == MCI_MODE_PLAY) {
        stopped_at = p->to;
        mode = MCI_MODE_STOP;
        if (p->notify) mciDriverNotify((HANDLE)(DWORD_PTR)p->notify, device_id, MCI_NOTIFY_SUCCESSFUL);
    }
    LeaveCriticalSection(&lock);
    free(p);
    return 0;
}

/* called with the lock held */
static void halt(void)
{
    if (player != NULL) {
        HANDLE thread = player;
        InterlockedIncrement(&generation);
        player = NULL;
        SetEvent(stop_event);
        LeaveCriticalSection(&lock);
        WaitForSingleObject(thread, 2000);
        EnterCriticalSection(&lock);
        CloseHandle(thread);
        ResetEvent(stop_event);
    }
}

static void stop_play(int pause)
{
    stopped_at = current_position();
    halt();
    mode = pause ? MCI_MODE_PAUSE : MCI_MODE_STOP;
}

static DWORD start_play(LONG from, LONG to, DWORD notify)
{
    Play *p;
    int n;

    if (from < 0 || from >= disc_length() || to <= from) return MCIERR_OUTOFRANGE;
    if (to > disc_length()) to = disc_length();
    n = track_at(from);
    if (!track[n].audio) return MCIERR_OUTOFRANGE;
    halt();
    p = malloc(sizeof(*p));
    p->from = from;
    p->to = to;
    p->notify = notify;
    p->token = InterlockedIncrement(&generation);
    play_from = from;
    play_to = to;
    played = 0;
    mode = MCI_MODE_PLAY;
    player = CreateThread(NULL, 0, player_main, p, 0, NULL);
    say("play track %d +%ld, %ld sectors", n, (long)(from - track[n].start), (long)(to - from));
    return 0;
}

/* ---- commands ---- */

/* what status says about a time value: winmm writes MSF and TMSF as hh:mm:ss text */
static LRESULT time_return(int is_length)
{
    if (time_format == MCI_FORMAT_MSF || (is_length && time_format == MCI_FORMAT_TMSF)) return MCI_COLONIZED3_RETURN;
    if (time_format == MCI_FORMAT_TMSF) return MCI_COLONIZED4_RETURN;
    return 0;
}

static LRESULT cd_status(DWORD flags, MCI_STATUS_PARMS *parms)
{
    int n = (int)parms->dwTrack;

    if (!(flags & MCI_STATUS_ITEM)) return MCIERR_MISSING_PARAMETER;
    if ((flags & MCI_TRACK) && (n < 1 || n > track_count)) return MCIERR_OUTOFRANGE;
    switch (parms->dwItem) {
    case MCI_STATUS_LENGTH:
        parms->dwReturn = length_format((flags & MCI_TRACK) ? track[n].length : disc_length());
        return time_return(1);
    case MCI_STATUS_POSITION:
        if (flags & MCI_TRACK) parms->dwReturn = to_format(track[n].start);
        else if (flags & MCI_STATUS_START) parms->dwReturn = to_format(0);
        else parms->dwReturn = to_format(current_position());
        return time_return(0);
    case MCI_STATUS_NUMBER_OF_TRACKS:
        parms->dwReturn = (DWORD)track_count;
        return 0;
    case MCI_STATUS_MODE:
        parms->dwReturn = MAKEMCIRESOURCE(mode, mode);
        return MCI_RESOURCE_RETURNED;
    case MCI_STATUS_MEDIA_PRESENT:
        parms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
        return MCI_RESOURCE_RETURNED;
    case MCI_STATUS_READY:
        parms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
        return MCI_RESOURCE_RETURNED;
    case MCI_STATUS_TIME_FORMAT:
        parms->dwReturn = MAKEMCIRESOURCE(time_format, MCI_FORMAT_RETURN_BASE + time_format);
        return MCI_RESOURCE_RETURNED;
    case MCI_STATUS_CURRENT_TRACK:
        parms->dwReturn = (DWORD)track_at(current_position());
        return 0;
    case MCI_CDA_STATUS_TYPE_TRACK:
        if (!(flags & MCI_TRACK)) return MCIERR_MISSING_PARAMETER;
        parms->dwReturn = MAKEMCIRESOURCE(track[n].audio ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER,
                                          track[n].audio ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER);
        return MCI_RESOURCE_RETURNED | MCI_RESOURCE_DRIVER;     /* the words are in this DLL's resources */
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}

static void notify(DWORD flags, DWORD_PTR callback, UINT status)
{
    if (flags & MCI_NOTIFY) mciDriverNotify((HANDLE)callback, device_id, status);
}

static LRESULT command(UINT message, DWORD flags, void *pointer)
{
    DWORD_PTR *generic = pointer;       /* every MCI_*_PARMS starts with dwCallback */
    DWORD_PTR callback = generic ? generic[0] : 0;
    LRESULT result = 0;

    EnterCriticalSection(&lock);
    switch (message) {
    case MCI_OPEN_DRIVER:
        if (!loaded) loaded = load_cue();
        if (!loaded) { result = MCIERR_DEVICE_NOT_READY; break; }
        opened = 1;
        time_format = MCI_FORMAT_MSF;
        mode = MCI_MODE_STOP;
        stopped_at = 0;
        break;
    case MCI_CLOSE_DRIVER:
        stop_play(0);
        opened = 0;
        break;
    case MCI_SET:
        if (flags & MCI_SET_TIME_FORMAT) {
            MCI_SET_PARMS *s = pointer;
            if (s->dwTimeFormat != MCI_FORMAT_MILLISECONDS && s->dwTimeFormat != MCI_FORMAT_MSF && s->dwTimeFormat != MCI_FORMAT_TMSF) result = MCIERR_BAD_TIME_FORMAT;
            else time_format = s->dwTimeFormat;
        }
        break;
    case MCI_STATUS:
        result = cd_status(flags, pointer);
        break;
    case MCI_PLAY: {
        MCI_PLAY_PARMS *pp = pointer;
        LONG from = (pp && (flags & MCI_FROM)) ? from_format(pp->dwFrom) : current_position();
        LONG to = (pp && (flags & MCI_TO)) ? from_format(pp->dwTo) : disc_length();
        /* from the end of a finished play, play goes on from there */
        result = start_play(from, to, (flags & MCI_NOTIFY) ? (DWORD)callback : 0);
        if (result == 0) flags &= ~MCI_NOTIFY;      /* told when it ends, not now */
        break;
    }
    case MCI_SEEK: {
        MCI_SEEK_PARMS *sp = pointer;
        stop_play(0);
        if (flags & MCI_SEEK_TO_START) stopped_at = 0;
        else if (flags & MCI_SEEK_TO_END) stopped_at = disc_length();
        else if ((flags & MCI_TO) && sp) stopped_at = from_format(sp->dwTo);
        if (stopped_at < 0 || stopped_at > disc_length()) { stopped_at = 0; result = MCIERR_OUTOFRANGE; }
        break;
    }
    case MCI_STOP:
        stop_play(0);
        break;
    case MCI_PAUSE:
        if (mode == MCI_MODE_PLAY) stop_play(1);
        break;
    case MCI_RESUME:
        if (mode == MCI_MODE_PAUSE) result = start_play(stopped_at, play_to, 0);
        break;
    case MCI_GETDEVCAPS: {
        MCI_GETDEVCAPS_PARMS *c = pointer;
        if (!(flags & MCI_GETDEVCAPS_ITEM)) { result = MCIERR_MISSING_PARAMETER; break; }
        switch (c->dwItem) {
        case MCI_GETDEVCAPS_DEVICE_TYPE: c->dwReturn = MAKEMCIRESOURCE(MCI_DEVTYPE_CD_AUDIO, MCI_DEVTYPE_CD_AUDIO); result = MCI_RESOURCE_RETURNED; break;
        case MCI_GETDEVCAPS_HAS_AUDIO:
        case MCI_GETDEVCAPS_CAN_PLAY: c->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE); result = MCI_RESOURCE_RETURNED; break;
        case MCI_GETDEVCAPS_HAS_VIDEO:
        case MCI_GETDEVCAPS_COMPOUND_DEVICE:
        case MCI_GETDEVCAPS_CAN_RECORD:
        case MCI_GETDEVCAPS_CAN_SAVE:
        case MCI_GETDEVCAPS_USES_FILES: c->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE); result = MCI_RESOURCE_RETURNED; break;
        default: result = MCIERR_UNSUPPORTED_FUNCTION; break;
        }
        break;
    }
    default:
        result = MCIERR_UNRECOGNIZED_COMMAND;
        break;
    }
    if (message != MCI_STATUS) say("command %x flags %lx -> %ld", message, (unsigned long)flags, (long)result);
    LeaveCriticalSection(&lock);
    if (result == 0 || result == MCI_RESOURCE_RETURNED) notify(flags, callback, MCI_NOTIFY_SUCCESSFUL);
    return result;
}

LRESULT CALLBACK DriverProc(DWORD_PTR driver, HDRVR handle, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
        InitializeCriticalSection(&lock);
        stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        trace = GetEnvironmentVariableA("ORIG_CDAUDIO_TRACE", NULL, 0) != 0;
        return 1;
    case DRV_FREE:
        return 1;
    case DRV_OPEN:
        if (param2) {
            MCI_OPEN_DRIVER_PARMS *open = (MCI_OPEN_DRIVER_PARMS *)param2;
            open->wType = MCI_DEVTYPE_CD_AUDIO;
            open->wCustomCommandTable = MCI_NO_COMMAND_TABLE;
            device_id = open->wDeviceID;
            mciSetDriverData(open->wDeviceID, 1);
            return open->wDeviceID;
        }
        return 1;
    case DRV_CLOSE:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_QUERYCONFIGURE:
    case DRV_CONFIGURE:
    case DRV_INSTALL:
    case DRV_REMOVE:
        return 1;
    default:
        if (message >= MCI_FIRST && message < MCI_FIRST + 0x100) return command(message, (DWORD)param1, (void *)param2);
        return DefDriverProc(driver, handle, message, param1, param2);
    }
}
