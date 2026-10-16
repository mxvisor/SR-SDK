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

/* Music: a game's soundtrack as Red Book audio tracks of its CD.
 *
 * The game asks MSCDEX to PLAY AUDIO (84h) from a track's sector for a number
 * of sectors, and to STOP AUDIO (85h) or RESUME AUDIO (88h).  The game
 * runtime's handle_mscdex turns the request into (track, offset, sectors) and
 * this file plays it from the CD's cue sheet (Game_cdrom.c):
 *
 *  - raw CDDA in the image (2352-byte sectors of 44.1 kHz 16-bit stereo) goes
 *    to SDL_mixer as a WAV stream: a RIFF header made up here, followed by
 *    the sectors read straight out of the image;
 *  - a track that is a file of its own in the cue sheet (ogg, wav, ...) is
 *    decoded by SDL_mixer and seeked to the requested sector.  A cue sheet
 *    with ogg tracks, pointed to by $<GAME>_CD_IMAGE, is how the port takes the
 *    soundtrack as ogg. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_hooks.h"
#include "Game_cdrom.h"
#include "Game_music.h"
#include "Game_sound.h"

extern "C" {

#define CD_SECTOR_BYTES 2352
#define WAV_HEADER_BYTES 44

typedef struct {
    FILE *file;
    int64_t base;
    int64_t length;
    int64_t position;       /* in the WAV stream, header included */
    int swap;
    uint8_t header[WAV_HEADER_BYTES];
} cdda_stream;

static int music_enabled;
static Mix_Music *music;
static int music_paused;
static int music_track;
static int32_t music_first_frame;   /* absolute sector where the play started */
static uint32_t music_sectors;
static uint32_t music_started;      /* SDL_GetTicks() when the position was 0 */
static uint32_t music_pause_ticks;  /* the position, in ms, while paused */

static int trace_music(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_SOUND")) != NULL;
    return enabled;
}

static int64_t file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (int64_t)st.st_size;
}

/* ---- raw CDDA as a WAV stream ---- */

static Sint64 SDLCALL cdda_size(SDL_RWops *rw)
{
    return WAV_HEADER_BYTES + ((cdda_stream *)rw->hidden.unknown.data1)->length;
}

static Sint64 SDLCALL cdda_seek(SDL_RWops *rw, Sint64 offset, int whence)
{
    cdda_stream *stream = (cdda_stream *)rw->hidden.unknown.data1;
    Sint64 target = whence == RW_SEEK_SET ? offset :
                    whence == RW_SEEK_CUR ? stream->position + offset :
                    WAV_HEADER_BYTES + stream->length + offset;
    if (target < 0) return SDL_SetError("seek before start");
    stream->position = target;
    return target;
}

static size_t SDLCALL cdda_read(SDL_RWops *rw, void *ptr, size_t size, size_t maxnum)
{
    cdda_stream *stream = (cdda_stream *)rw->hidden.unknown.data1;
    if (size == 0) return 0;
    int64_t end = WAV_HEADER_BYTES + stream->length;
    int64_t wanted = (int64_t)(size * maxnum);
    if (stream->position + wanted > end) wanted = end - stream->position;
    if (wanted <= 0) return 0;
    uint8_t *out = (uint8_t *)ptr;
    int64_t done = 0;
    if (stream->position < WAV_HEADER_BYTES) {
        int64_t part = WAV_HEADER_BYTES - stream->position;
        if (part > wanted) part = wanted;
        memcpy(out, stream->header + stream->position, (size_t)part);
        done = part;
    }
    if (done < wanted) {
        int64_t at = stream->base + (stream->position + done - WAV_HEADER_BYTES);
        if (fseeko(stream->file, (off_t)at, SEEK_SET) == 0) {
            size_t got = fread(out + done, 1, (size_t)(wanted - done), stream->file);
            if (stream->swap) {
                /* whole 16-bit samples only: the data part starts even */
                for (size_t i = 0; i + 1 < got; i += 2) {
                    uint8_t t = out[done + i];
                    out[done + i] = out[done + i + 1];
                    out[done + i + 1] = t;
                }
            }
            done += (int64_t)got;
        }
    }
    stream->position += done;
    return (size_t)done / size;
}

static size_t SDLCALL cdda_write(SDL_RWops *rw, const void *ptr, size_t size, size_t num)
{
    (void)rw; (void)ptr; (void)size; (void)num;
    SDL_SetError("read-only");
    return 0;
}

static int SDLCALL cdda_close(SDL_RWops *rw)
{
    cdda_stream *stream = (cdda_stream *)rw->hidden.unknown.data1;
    fclose(stream->file);
    free(stream);
    SDL_FreeRW(rw);
    return 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static SDL_RWops *open_cdda(const char *path, int64_t base, int64_t length, int swap)
{
    int64_t size = file_size(path);
    if (size < 0 || base >= size) return NULL;
    if (length > size - base) length = size - base;
    length -= length % 4;
    if (length <= 0) return NULL;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    cdda_stream *stream = (cdda_stream *)calloc(1, sizeof(*stream));
    SDL_RWops *rw = SDL_AllocRW();
    if (stream == NULL || rw == NULL) {
        free(stream);
        if (rw != NULL) SDL_FreeRW(rw);
        fclose(file);
        return NULL;
    }
    stream->file = file;
    stream->base = base;
    stream->length = length;
    stream->swap = swap;
    uint8_t *h = stream->header;
    memcpy(h, "RIFF", 4);
    put32(h + 4, (uint32_t)(36 + length));
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);               /* PCM */
    put16(h + 22, 2);               /* stereo */
    put32(h + 24, 44100);
    put32(h + 28, 44100 * 4);
    put16(h + 32, 4);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, (uint32_t)length);
    rw->type = SDL_RWOPS_UNKNOWN;
    rw->size = cdda_size;
    rw->seek = cdda_seek;
    rw->read = cdda_read;
    rw->write = cdda_write;
    rw->close = cdda_close;
    rw->hidden.unknown.data1 = stream;
    return rw;
}

/* ---- playback ---- */

static void end_music(void)
{
    if (music == NULL) return;
    Mix_HaltMusic();
    Mix_FreeMusic(music);
    music = NULL;
    music_paused = 0;
    music_track = 0;
}

void Game_MusicInit(void)
{
    const char *nomusic = getenv(GAME_ENV("NOMUSIC"));
    /* <GAME>_NOMUSIC, when set, over the game's Game_Music (its config file) */
    if (nomusic != NULL && nomusic[0] != 0) music_enabled = Game_AudioIsOpen() && nomusic[0] == '0';
    else music_enabled = Game_AudioIsOpen() && Game_Music != 0;
    if (!music_enabled) return;
    /* the formats a cue sheet's track files may have; raw CDDA and wav need
     * none of these */
    const int formats = MIX_INIT_FLAC | MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_OPUS;
    int loaded = Mix_Init(formats);
    if (loaded != formats && trace_music()) {
        fprintf(stderr, "MUSIC SDL_mixer lacks some of flac/ogg/mp3/opus (%x of %x): %s\n",
                loaded, formats, Mix_GetError());
    }
}

void Game_MusicDeinit(void)
{
    end_music();
    if (music_enabled) Mix_Quit();
    music_enabled = 0;
}

void Game_MusicPlay(int track, uint32_t offset, uint32_t sectors)
{
    game_cd_audio audio;

    end_music();
    if (!music_enabled || Game_CdAudioTrack(track, &audio) != 0) {
        if (music_enabled) fprintf(stderr, GAME_NAME ": no music for CD track %d\n", track);
        return;
    }

    uint32_t seek = 0;
    if (audio.compressed) {
        music = Mix_LoadMUS(audio.path);
        if (music == NULL) fprintf(stderr, GAME_NAME ": %s: %s\n", audio.path, Mix_GetError());
        seek = (uint32_t)audio.start_frames + offset;
    } else {
        SDL_RWops *rw = open_cdda(audio.path, audio.offset + (int64_t)offset * CD_SECTOR_BYTES,
                                  (int64_t)sectors * CD_SECTOR_BYTES, audio.swap);
        if (rw != NULL) music = Mix_LoadMUS_RW(rw, 1);
    }
    if (music == NULL) {
        fprintf(stderr, GAME_NAME ": no music for CD track %d\n", track);
        return;
    }
    /* loops = 0: once, as a CD plays it; the game restarts the track itself */
    if (Mix_PlayMusic(music, 0) != 0) {
        fprintf(stderr, GAME_NAME ": CD track %d: %s\n", track, Mix_GetError());
        end_music();
        return;
    }
    {
        int32_t at;
        int is_audio;

        music_track = track;
        music_first_frame = Game_CdTrackInfo(track, &at, &is_audio) == 0 ? at + (int32_t)offset : (int32_t)offset;
        music_sectors = sectors;
        music_started = SDL_GetTicks();
    }
    /* raw CDDA starts at the offset already; a decoded file seeks to it */
    if (seek != 0) Mix_SetMusicPosition(seek / 75.0);
    if (trace_music()) {
        fprintf(stderr, "MUSIC track %d +%u sectors, %u sectors, from %s\n", track, offset, sectors, audio.path);
    }
}

void Game_MusicStop(void)
{
    if (music == NULL) return;
    if (music_paused || !Mix_PlayingMusic()) {
        end_music();
        return;
    }
    Mix_PauseMusic();
    music_paused = 1;
    music_pause_ticks = SDL_GetTicks() - music_started;
}

void Game_MusicResume(void)
{
    if (music == NULL || !music_paused) return;
    Mix_ResumeMusic();
    music_paused = 0;
    music_started = SDL_GetTicks() - music_pause_ticks;
}

int Game_MusicStatus(int *track, int32_t *frame, uint32_t *remaining, int *paused)
{
    uint32_t played;

    if (music == NULL || (!music_paused && !Mix_PlayingMusic())) return 0;
    played = (music_paused ? music_pause_ticks : SDL_GetTicks() - music_started) * 75u / 1000u;
    if (played > music_sectors) played = music_sectors;
    *track = music_track;
    *frame = music_first_frame + (int32_t)played;
    *remaining = music_sectors - played;
    *paused = music_paused;
    return 1;
}

}

int Game_MusicEnabled(void)
{
    return music_enabled;
}

void Game_MusicHalt(void)
{
    end_music();
}

void Game_MusicPause(void)
{
    if (music == NULL || music_paused) return;
    Mix_PauseMusic();
    music_pause_ticks = SDL_GetTicks() - music_started;
    music_paused = 1;
}

int Game_MusicPlaying(void)
{
    return music != NULL && !music_paused && Mix_PlayingMusic();
}

void Game_MusicVolume(int volume)
{
    if (music_enabled) Mix_VolumeMusic(volume);
}
