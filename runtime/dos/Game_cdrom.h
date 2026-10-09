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

#ifndef _GAME_CDROM_H_INCLUDED_
#define _GAME_CDROM_H_INCLUDED_

#include <stdint.h>

#include "Game_file.h"

/* The game's CD, drive D: (Game_cdrom.c): an image from a cue sheet, or the
 * game directory itself when it is a copy of the disc. */

#define GAME_CD_SECTOR_DATA 2048
#define GAME_CD_MAX_TRACKS 99

typedef struct {
    char dos_name[13];      /* upper case, no ";1" */
    uint32_t lba;
    uint32_t size;
    int directory;
} game_cd_entry;

/* An audio track's source. `compressed`: a whole file SDL_mixer decodes
 * (ogg, wav, ...), starting `start_frames` CD frames (1/75 s) in. Otherwise
 * raw CDDA, 2352-byte sectors of 44.1 kHz 16-bit stereo, at byte `offset`,
 * big-endian when `swap`. */
typedef struct {
    const char *path;
    int compressed;
    int swap;
    int64_t offset;
    int32_t start_frames;
} game_cd_audio;

#ifdef __cplusplus
extern "C" {
#endif

/* Finds the disc: $<GAME>_CD_IMAGE (a cue sheet; GAME_ENV in Game_hooks.h), else <game_dir>/CD. 0 when
 * there is one. */
int Game_CdInit(const char *game_dir);
void Game_CdDeinit(void);
int Game_CdPresent(void);
const char *Game_CdCue(void);

/* `path` is relative to the CD's root, '\' or '/' separated, any case. */
int Game_CdFind(const char *path, game_cd_entry *entry);
/* Opens a file for reading; NULL when it is not there. */
game_file *Game_CdOpen(const char *path);
/* Calls `visit` for each entry of the directory `path` ("" = root), '.' and
 * '..' left out. */
int Game_CdList(const char *path, void (*visit)(const game_cd_entry *entry, void *arg), void *arg);
/* The 2048 data bytes of sector `lba` of an image's data track. */
int Game_CdReadSector(uint32_t lba, uint8_t *data);

int Game_CdAudioTrack(int track, game_cd_audio *audio);
/* The table of contents, as frames from the start of the disc (the cue
 * sheet's INDEX 01 positions); the lead-out is the end of the last file. */
int Game_CdTrackInfo(int track, int32_t *start_frames, int *audio);
int Game_CdTocRange(int *first, int *last, int32_t *leadout);

#ifdef __cplusplus
}
#endif

#endif
