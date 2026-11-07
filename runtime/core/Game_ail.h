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

/* The Miles Sound System (AIL 3.x) of the DOS and the Win32 games, without its
 * entry points: digital samples on the channels of the port's SDL_mixer device
 * and XMIDI sequences played by Game_midi.  A sample or a sequence is a number
 * of the pool here; the entries (WinApi-wail32.c for WAIL32.DLL, the DOS
 * game's own glue) make the game's handles of it and turn its addresses into
 * pointers. */

#if !defined(_GAME_AIL_H_INCLUDED_)
#define _GAME_AIL_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the states of a sample and of a sequence (AIL.H: SMP_*, SEQ_*) */
#define GAME_AIL_FREE       0x0001
#define GAME_AIL_DONE       0x0002
#define GAME_AIL_PLAYING    0x0004
#define GAME_AIL_STOPPED    0x0008

/* AIL_set_sample_type: format bit 0 16-bit, bit 1 stereo; flags bit 0 signed
 * 8-bit data (16-bit data is signed) */
#define GAME_AIL_F_16BITS   1
#define GAME_AIL_F_STEREO   2
#define GAME_AIL_PCM_SIGN   1

#define GAME_AIL_SAMPLES    32
#define GAME_AIL_SEQUENCES  8

/* AIL_set_preference: the old value; DIG_DEFAULT_VOLUME (5) is the volume of
 * a sample AIL_init_sample sets up */
int32_t Game_AilSetPreference(uint32_t number, int32_t value);
/* the rate of the digital driver, the playback rate of a sample
 * AIL_init_sample sets up (11025 Hz until set) */
void Game_AilSetDriverRate(int32_t rate);
/* ends every sample and sequence and frees them */
void Game_AilShutdown(void);

/* a sample of the pool, set up as AIL_init_sample does; -1 when none is free */
int Game_AilAllocateSample(void);
void Game_AilReleaseSample(int sample);
void Game_AilInitSample(int sample);
/* PCM of AIL_set_sample_type's type */
void Game_AilSetSampleAddress(int sample, const void *data, uint32_t length);
/* a WAV image: PCM, its type and its rate; 0 for a file of another kind */
int Game_AilSetSampleWav(int sample, const void *wav);
void Game_AilSetSampleType(int sample, int32_t format, uint32_t flags);
void Game_AilSetSampleRate(int sample, int32_t rate);
/* 0-127 */
void Game_AilSetSampleVolume(int sample, int32_t volume);
/* 0 left, 64 middle, 127 right */
void Game_AilSetSamplePan(int sample, int32_t pan);
/* 0 plays forever */
void Game_AilSetSampleLoopCount(int sample, int32_t count);
void Game_AilStartSample(int sample);
/* stops it where it is; resume goes on from there */
void Game_AilStopSample(int sample);
void Game_AilResumeSample(int sample);
void Game_AilEndSample(int sample);
uint32_t Game_AilSampleStatus(int sample);

/* a sequence of the pool, done; -1 when none is free */
int Game_AilAllocateSequence(void);
void Game_AilReleaseSequence(int sequence);
/* the number-th sequence of XMIDI data (FORM XDIR, CAT XMID); 0 for data
 * of another kind */
int Game_AilInitSequence(int sequence, const void *xmidi, uint32_t number);
/* one sequence plays at a time: starting one ends the other */
void Game_AilStartSequence(int sequence);
void Game_AilStopSequence(int sequence);
void Game_AilResumeSequence(int sequence);
void Game_AilEndSequence(int sequence);
uint32_t Game_AilSequenceStatus(int sequence);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_AIL_H_INCLUDED_ */
