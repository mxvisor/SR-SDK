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


#if !defined(_GAME_OPL_H_INCLUDED_)
#define _GAME_OPL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

/* An OPL2 (YM3812, the FM chip of the Ad Lib and of the Sound Blaster) on
 * SDL_mixer's music, for the FM music drivers of the DOS games: emu8950
 * (extern/emu8950) at the Ad Lib's clock, its output resampled to the mixer's
 * rate.  One chip, one driver at a time; it takes SDL_mixer's music (as
 * Game_midi does) while it is open.
 *
 * The driver's service routine runs on the audio thread, `rate` times a second
 * of the music (the timer interrupt of a DOS driver: AIL's 120 Hz), between
 * the samples, so the music keeps its time whatever the game's frame rate is.
 * The routine writes the chip's registers with Game_OplWrite.  Everything else
 * that writes them or touches the driver's state takes Game_OplLock first (the
 * service routine runs under it); it nests. */
typedef void (*Game_OplServe)(void *user);

/* 0 when the chip cannot be made (no audio device) */
int Game_OplOpen(Game_OplServe serve, void *user, int rate);
/* silences and frees the chip */
void Game_OplClose(void);
/* Until the chip is closed, its output loses its DC as a card's output does
 * (its capacitor): the half-sine waveforms of the chip have one.  The FM chip
 * of Game_dossb.c (GAME_DOS_SB_OPL) sets it. */
void Game_OplDcBlock(void);
void Game_OplLock(void);
void Game_OplUnlock(void);
/* register 0x00-0xff of the chip */
void Game_OplWrite(unsigned int reg, unsigned int value);
/* The same from another thread than the audio one (a driver run by the game,
 * at its own timer): the write goes in at its time, a buffer of the output
 * later (no lock needed). */
void Game_OplWriteTimed(unsigned int reg, unsigned int value);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_OPL_H_INCLUDED_ */
