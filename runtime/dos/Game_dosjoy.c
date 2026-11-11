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

/* The game port: a write starts four one-shots, and a game times them by
 * counting its reads of the port until each axis bit drops (in a loop of
 * reads, not on the PIT).  So an axis here drops after a
 * number of reads that grows with its position: about JOY_READS_MIN for one
 * end, JOY_READS_MIN + JOY_READS_RANGE for the other, the middle between; the
 * games calibrate their centre and ends themselves.
 *
 * Joystick A (bits 0/1, buttons 4/5) is the left stick, the D-pad moves it
 * too; joystick B (bits 2/3, buttons 6/7) is the right stick.  The buttons
 * 1-4 (bits 4-7) are A, B, X, Y.  Without a pad the port reads FFh: the axes
 * never time out and no button is pressed, as with nothing plugged in. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "Game_dosjoy.h"
#include "Game_pad.h"
#include "Game_hooks.h"

#define JOY_READS_MIN   16
#define JOY_READS_RANGE 2000

static int present;             /* a pad was there at the last look */
static Game_PadState state;     /* as read at the last look */
static uint32_t looked;         /* SDL_GetTicks of the last look */
static uint32_t reads;          /* reads since the last write */
static uint32_t timeout[4];     /* the reads after which each axis drops */

/* the pad, read again at most once a millisecond: a game's timing loop reads
 * the port thousands of times */
static void look(int now)
{
    uint32_t ticks = SDL_GetTicks();
    unsigned int slots;
    int slot;

    if (!now && ticks == looked) return;
    looked = ticks;
    present = 0;
    slots = Game_PadSlots();
    for (slot = 0; slot < GAME_PAD_MAX; slot++)
    {
        if ((slots & (1u << slot)) != 0)
        {
            present = Game_PadGetState(slot, &state);
            break;
        }
    }
}

/* -32768..32767 (`flip`: Y up, so that up is the short end) as a count of reads */
static uint32_t axis_reads(int value, int flip)
{
    int position = flip ? 32767 - value : 32767 + value;    /* -1..65535 */
    if (position < 0) position = 0;
    return JOY_READS_MIN + (uint32_t)position * JOY_READS_RANGE / 65535u;
}

void Game_DosJoyOut(void)
{
    int x, y;

    look(1);
    reads = 0;
    if (!present) return;

    x = state.left_x;
    y = state.left_y;
    if (state.buttons & GAME_PAD_DPAD_LEFT) x = -32768;
    if (state.buttons & GAME_PAD_DPAD_RIGHT) x = 32767;
    if (state.buttons & GAME_PAD_DPAD_UP) y = 32767;
    if (state.buttons & GAME_PAD_DPAD_DOWN) y = -32768;

    timeout[0] = axis_reads(x, 0);
    timeout[1] = axis_reads(y, 1);
    timeout[2] = axis_reads(state.right_x, 0);
    timeout[3] = axis_reads(state.right_y, 1);

    if (getenv(GAME_ENV("DEBUG_PAD")) != NULL)
    {
        static uint32_t seen[4];
        static uint16_t seen_buttons;
        if (memcmp(seen, timeout, sizeof(seen)) != 0 || seen_buttons != state.buttons)
        {
            memcpy(seen, timeout, sizeof(seen));
            seen_buttons = state.buttons;
            fprintf(stderr, "game port: axes %u %u %u %u buttons %04x\n", timeout[0], timeout[1], timeout[2], timeout[3], state.buttons);
        }
    }
}

uint8_t Game_DosJoyIn(void)
{
    uint8_t value = 0;
    int axis;

    look(0);
    if (!present) return 0xff;

    for (axis = 0; axis < 4; axis++)
    {
        if (reads < timeout[axis]) value |= 1u << axis;
    }
    if (reads < 0xffffffffu) reads++;

    if (!(state.buttons & GAME_PAD_A)) value |= 0x10;
    if (!(state.buttons & GAME_PAD_B)) value |= 0x20;
    if (!(state.buttons & GAME_PAD_X)) value |= 0x40;
    if (!(state.buttons & GAME_PAD_Y)) value |= 0x80;
    return value;
}
