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


#if !defined(_GAME_MIDI_H_INCLUDED_)
#define _GAME_MIDI_H_INCLUDED_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MIDI music, rendered by TinySoundFont from soundfont.sf2 of the data
 * directory and mixed into the SDL_mixer music stream (Game_AudioInit must
 * have opened the device).  One song plays at a time. */

/* The number-th sequence of XMIDI data (FORM XDIR / CAT XMID) as a standard
 * MIDI file, in a malloc'ed block (free() it); NULL if there is none.
 * *length is its size; *loop is set when the sequence loops for ever. */
uint8_t *Game_MidiFromXmidi(const uint8_t *xmi, uint32_t size, uint32_t number, size_t *length, int *loop);

/* Plays a standard MIDI file (the data is copied), looping or once; stops the
 * song before.  Returns 0 if the soundfont or the data is unusable. */
int Game_MidiPlay(const uint8_t *midi, size_t length, int loop);
void Game_MidiStop(void);
/* pauses (1) or resumes (0) the song */
void Game_MidiPause(int paused);
/* 1 once a song that does not loop has played to its end */
int Game_MidiFinished(void);
/* stops the song and frees the synthesizer */
void Game_MidiShutdown(void);

/* A stream of MIDI messages instead of a song (the winmm MIDI stream of the
 * Win32 games).  While the stream is open, the audio thread calls pump before
 * each slice of at most 1.5 ms it renders, with the slice's length in ms; pump
 * sends the messages due by then with Game_MidiShortMsg.  Game_MidiLock keeps
 * the audio thread out (it is held around pump too, and it nests).
 * Game_MidiStreamOpen stops the song; it returns 0 without a soundfont.
 * The stream takes SDL_mixer's music (the CD music too) only between
 * Game_MidiStreamPlay(1) and Game_MidiStreamPlay(0): a game may stop its stream
 * and play the CD with the stream still open.  Not under Game_MidiLock. */
typedef void (*Game_MidiPump)(void *user, double ms);
int Game_MidiStreamOpen(Game_MidiPump pump, void *user);
void Game_MidiStreamPlay(int playing);
void Game_MidiStreamClose(void);
/* a short message: status in the low byte, then the two data bytes */
void Game_MidiShortMsg(uint32_t msg);
/* all notes off at once, the controllers and the programs reset */
void Game_MidiReset(void);
void Game_MidiLock(void);
void Game_MidiUnlock(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_MIDI_H_INCLUDED_ */
