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

#ifndef _GAME_DOSAIL_H_INCLUDED_
#define _GAME_DOSAIL_H_INCLUDED_

#include <stdint.h>

/* The Miles Sound System (AIL 3.x) a DOS4GW game was linked with, replaced
 * natively (Game_dosail.c over core/Game_ail.c): its timer service on the
 * PIT and its digital driver.  The game's external_procedures.sci routes the
 * entry points it calls to the SR_AIL_* procedures of llasm/ail.llinc, which
 * its own llasm includes; they call these. */

#ifdef __cplusplus
extern "C" {
#endif

/* One PIT channel 0 interrupt for AIL's timers: a game whose AIL owns INT 8
 * calls it from its GAME_DOS_TIMER_TICK() before the guest's INT 8. */
void Game_AIL_TimerInterrupt(void);

void Game_AIL_startup(void);
void Game_AIL_shutdown(void);
int32_t Game_AIL_set_preference(uint32_t number, int32_t value);
int32_t Game_AIL_register_timer(uint32_t callback);
void Game_AIL_set_timer_period(int32_t timer, uint32_t microseconds);
void Game_AIL_start_timer(int32_t timer);
void Game_AIL_stop_timer(int32_t timer);
void Game_AIL_release_timer_handle(int32_t timer);
int32_t Game_AIL_install_DIG_INI(uint32_t *dig);
void *Game_AIL_allocate_sample_handle(void *dig);
void Game_AIL_release_sample_handle(void *S);
void Game_AIL_init_sample(void *S);
void Game_AIL_set_sample_address(void *S, void *start, uint32_t len);
void Game_AIL_set_sample_type(void *S, int32_t format, uint32_t flags);
void Game_AIL_set_sample_playback_rate(void *S, int32_t playback_rate);
void Game_AIL_set_sample_volume(void *S, int32_t volume);
void Game_AIL_set_sample_pan(void *S, int32_t pan);
void Game_AIL_set_sample_loop_count(void *S, int32_t loop_count);
void Game_AIL_start_sample(void *S);
void Game_AIL_stop_sample(void *S);
void Game_AIL_resume_sample(void *S);
void Game_AIL_end_sample(void *S);
uint32_t Game_AIL_sample_status(void *S);

#ifdef __cplusplus
}
#endif

#endif
