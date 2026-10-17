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

/* Channel 0 of the PIT on the host clock.  The count is a down counter from
 * the reload value minus 1 to 0 at 1193182 Hz, restarted when a reload value
 * is written (modes 2 and 3 do so on the 8253), so a game that reprograms it
 * from its interrupt handler, locked to vertical retrace, gets the next
 * interrupt when it asked for it.  One clock, counted from local midnight,
 * runs the counter and the BIOS ticks too: a game that adds the fraction the
 * counter has reached to the BIOS ticks sees them advance as the counter
 * wraps. */

#include <stdint.h>
#include <time.h>
#include <SDL.h>

#include "Game_dospit.h"

#define PIT_FREQUENCY 1193182ull
/* interrupts missed beyond this many periods are dropped, not caught up */
#define PIT_MAX_BEHIND 8u

static uint32_t divisor = 0x10000u;
static uint64_t count_start;   /* clock at the last reload */
static uint64_t next_irq;      /* clock of the next interrupt */
static uint8_t access_mode = 3; /* 1 = low byte, 2 = high byte, 3 = low then high */
static uint8_t write_high;      /* access 3: the next write to 40h is the high byte */
static uint8_t write_low;
static uint8_t read_high;       /* access 3: the next read from 40h is the high byte */
static int latched;
static uint16_t latch;

/* PIT clocks since local midnight.  SDL's counter rather than clock_gettime:
 * on Windows that call comes from libwinpthread, and an older copy of that
 * DLL lacks clock_gettime64. */
static uint64_t pit_clock(void)
{
    static uint64_t base;
    static int have_base;
    const uint64_t counter = SDL_GetPerformanceCounter();
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t elapsed = (counter / frequency) * PIT_FREQUENCY + (counter % frequency) * PIT_FREQUENCY / frequency;

    if (!have_base) {
        const time_t now = time(NULL);
        struct tm local;

#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        base = ((uint64_t)local.tm_hour * 3600u + (uint64_t)local.tm_min * 60u + (uint64_t)local.tm_sec) * PIT_FREQUENCY - elapsed;
        have_base = 1;
    }
    return base + elapsed;
}

static uint16_t pit_count(void)
{
    return (uint16_t)(divisor - 1u - (uint32_t)((pit_clock() - count_start) % divisor));
}

static void pit_reload(uint32_t value)
{
    divisor = value == 0 ? 0x10000u : value;
    count_start = pit_clock();
    next_irq = count_start + divisor;
}

void Game_DosPitInit(void)
{
    const uint64_t now = pit_clock();

    divisor = 0x10000u;
    count_start = now - now % divisor;
    next_irq = count_start + divisor;
    access_mode = 3;
    write_high = 0;
    read_high = 0;
    latched = 0;
}

uint8_t Game_DosPitIn(uint16_t port)
{
    if (port != 0x40) return 0;
    /* without a latch command the low byte's read latches the count, so
     * that the high byte goes with it */
    if (!latched) {
        latch = pit_count();
        latched = 1;
    }
    if (access_mode == 1 || (access_mode == 3 && !read_high)) {
        if (access_mode == 3) read_high = 1;
        else latched = 0;
        return (uint8_t)latch;
    }
    read_high = 0;
    latched = 0;
    return (uint8_t)(latch >> 8);
}

void Game_DosPitOut(uint16_t port, uint8_t value)
{
    if (port == 0x43) {
        /* bits 7-6 channel, 5-4 access (0 = latch), 3-1 mode */
        if ((value & 0xc0u) != 0) return;
        if ((value & 0x30u) == 0) {
            latch = pit_count();
            latched = 1;
        } else {
            access_mode = (uint8_t)((value >> 4) & 3u);
            write_high = 0;
        }
        read_high = 0;
        return;
    }
    if (port != 0x40) return;
    if (access_mode == 3 && !write_high) {
        write_low = value;
        write_high = 1;
        return;
    }
    write_high = 0;
    if (access_mode == 3) pit_reload(write_low | ((uint32_t)value << 8));
    else if (access_mode == 2) pit_reload((uint32_t)value << 8);
    else pit_reload(value);
}

void Game_DosPitProgram(uint32_t value)
{
    Game_DosPitOut(0x43, 0x36);
    Game_DosPitOut(0x40, (uint8_t)value);
    Game_DosPitOut(0x40, (uint8_t)(value >> 8));
}

void Game_DosPitRun(void (*irq)(void))
{
    const uint64_t now = pit_clock();

    if (now < next_irq) return;
    if (now - next_irq > PIT_MAX_BEHIND * (uint64_t)divisor) next_irq = now;
    while (now >= next_irq) {
        next_irq += divisor;
        irq();
    }
}

uint32_t Game_DosPitBiosTicks(void)
{
    return (uint32_t)(pit_clock() >> 16);
}
