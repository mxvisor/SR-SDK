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

#ifndef _GAME_AUDIOSTREAM_H_INCLUDED_
#define _GAME_AUDIOSTREAM_H_INCLUDED_

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A stream of PCM that a game's sound hardware or API makes: the Sound Blaster
 * (Game_dossb.c), DirectSound's primary buffer (WinApi-dsound.c), a game's own
 * driver.  The calls are SDL_OpenAudioDevice's: the source fills its samples in
 * desired->callback, or, with no callback, queues them (Game_AudioSourceQueue).
 * It opens paused.  It plays on a device of its own, or on SDL_mixer's (Game_AudioInit)
 * after its mix: see Game_audiostream.c. */
typedef struct Game_AudioSource Game_AudioSource;

Game_AudioSource *Game_AudioSourceOpen(const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void Game_AudioSourceClose(Game_AudioSource *source);
/* Around changes to what the callback reads */
void Game_AudioSourceLock(Game_AudioSource *source);
void Game_AudioSourceUnlock(Game_AudioSource *source);
void Game_AudioSourcePause(Game_AudioSource *source, int pause);
/* With no callback: the samples to play, and the bytes queued and not played yet */
int Game_AudioSourceQueue(Game_AudioSource *source, const void *data, Uint32 len);
Uint32 Game_AudioSourceQueued(Game_AudioSource *source);
/* 1 when the source plays in SDL_mixer's output, a mixer buffer at a time */
int Game_AudioSourceOnMixer(const Game_AudioSource *source);

#ifdef __cplusplus
}
#endif

#endif
