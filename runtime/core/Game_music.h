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

#ifndef _GAME_MUSIC_H_INCLUDED_
#define _GAME_MUSIC_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CD audio for MSCDEX PLAY/STOP/RESUME AUDIO (the game runtime's handle_mscdex).
 * The music sources are looked up as Game_cdrom.c finds the disc
 * (Game_music.c). */
void Game_MusicInit(void);
void Game_MusicDeinit(void);
/* Play `sectors` sectors (1/75 s each) of Red Book track `track`, starting
 * `offset` sectors into it. */
void Game_MusicPlay(int track, uint32_t offset, uint32_t sectors);
/* MSCDEX semantics: the first STOP pauses, a second one ends play. */
void Game_MusicStop(void);
void Game_MusicResume(void);
/* Whether a track is playing (or paused): its number, the absolute sector now
 * under the head and the sectors left. */
int Game_MusicStatus(int *track, int32_t *frame, uint32_t *remaining, int *paused);

/* For an MCI CD audio device (the Win32 layer): whether music is on, stop at
 * once, pause, whether a track is still playing (not paused, not finished),
 * the music volume (0..128, kept for the next tracks). */
int Game_MusicEnabled(void);
void Game_MusicHalt(void);
void Game_MusicPause(void);
int Game_MusicPlaying(void);
void Game_MusicVolume(int volume);

#ifdef __cplusplus
}
#endif

#endif
