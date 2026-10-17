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

/* The default SR_CheckTimer of a DOS game: the translated code calls it from
 * every ACTION_* macro (at backward jumps and calls), so it is where the host
 * gets a turn: the window may have been closed, the keyboard is drained and its
 * interrupt delivered, the PIT's channel 0 interrupt (INT 8) is due, and the
 * screen is presented about 30 times a second.  A game that has to do more
 * at INT 8 (a sound driver's timer) defines its own SR_CheckTimer and lists
 * Game_dostimer.c in runtime_skip. */

static void pit_irq(void)
{
    Game_CallGuestInterrupt(0x08);
    if (Thread_Exit) Game_StopMain();
}

void SR_CheckTimer(void)
{
    static uint32_t last_present;
    uint32_t now;

    if (Thread_Exit) Game_StopMain();

    Game_DosKbdDrain();
    now = SDL_GetTicks();

    Game_DosPitRun(pit_irq);

    Game_DosKbdDeliverIrq();

    if (now - last_present >= 33)
    {
        Game_DosRequestPresent();
        last_present = now;
    }
}
