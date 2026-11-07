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


#include <stdint.h>
#include <SDL.h>

#include "Game_vars.h"
#include "Game_thread.h"
#include "Game_dosint.h"
#include "Game_doskbd.h"
#include "Game_dospit.h"
#include "Game_dosrt.h"
#include "Game_hooks.h"
#ifdef GAME_DOS_SOUNDBLASTER
#include "Game_dossb.h"
#endif

/* The default SR_CheckTimer of a DOS game: the translated code calls it from
 * every ACTION_* macro (at backward jumps and calls), so it is where the host
 * gets a turn: the window may have been closed, the keyboard is drained and its
 * interrupt delivered, the PIT's channel 0 interrupt (INT 8) is due, and the
 * screen is presented about 30 times a second, and the IRQ of the Sound
 * Blaster is due (GAME_DOS_SOUNDBLASTER).  A game that has to do something
 * else at INT 8 (a sound driver's timer) says what in GAME_DOS_TIMER_TICK()
 * of Game_hooks.h, in place of Game_DosTimerTick (the guest's INT 8 and
 * INT 1Ch handlers). */

#ifndef GAME_DOS_TIMER_TICK
#define GAME_DOS_TIMER_TICK() Game_DosTimerTick()
#endif

static void pit_irq(void)
{
    GAME_DOS_TIMER_TICK();
    if (Thread_Exit) Game_StopMain();
}

extern "C" void SR_CheckTimer(void)
{
    static uint32_t last_present;
    uint32_t now;

    if (Thread_Exit) Game_StopMain();

    Game_DosKbdDrain();
    now = SDL_GetTicks();

    Game_DosPitRun(pit_irq);

    Game_DosKbdDeliverIrq();

#ifdef GAME_DOS_SOUNDBLASTER
    Game_DosSbDeliverIrq();
#endif

    if (now - last_present >= 33)
    {
        Game_DosRequestPresent();
        last_present = now;
    }
}
