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

#include "display.h"
#include "Game_dosvga.h"
#include "Game_dosrt.h"

/* The DAC as the game programs it: 6-bit R, G, B per entry.  Kept apart from
 * the display's 8-bit copy (Set_Palette_Value) because a game reads colours
 * back, e.g. to save the whole DAC before a fade. */
static uint8_t dac[256][3];
static uint8_t dac_write_index;
static uint8_t dac_write_component;
static uint8_t dac_read_index;
static uint8_t dac_read_component;
static uint8_t dac_state; /* port 3C7h read: 0 after 3C8h (writing), 3 after 3C7h (reading) */

/* 6-bit DAC level -> 8 bits, linear and rounded to nearest (63 -> 255).  This
 * is dosbox-staging's mapping, so port frames match the reference pixel for
 * pixel; (level << 2) | (level >> 4) is up to one step off it. */
static uint32_t dac_level_to_8bit(uint8_t level)
{
    return ((uint32_t)level * 255u + 31u) / 63u;
}

/* Input Status #1 bit 3 for mode 13h: a 70.086 Hz frame of 449 lines at 800
 * dots of 25.175 MHz, with the vertical sync pulse on lines 412-414 (CRTC
 * 10h/11h = 9Ch/8Eh), timed as dosbox-staging times it.  A game whose frame is
 * locked to the retrace runs at this rate; loops that wait for the retrace to
 * start, and then to end, both finish. */
static uint8_t input_status1(void)
{
    static uint64_t frame, shown = UINT64_MAX;
    if (frame == 0) frame = SDL_GetPerformanceFrequency() * (449u * 800u) / 25175000u;
    const uint64_t now = SDL_GetPerformanceCounter(), position = now % frame;
    if (position >= frame * 412u / 449u && position < frame * 414u / 449u) {
        /* A game that waits for the retrace has finished the frame before
         * (it shows the next one at the retrace): present it, once a
         * retrace, not by the timer (SR_CheckTimer) */
        if (now / frame != shown) {
            shown = now / frame;
            Game_DosRequestPresent();
        }
        return 0x08;
    }
    return 0x00;
}

int Game_DosVgaIn(uint16_t port, uint32_t *value)
{
    switch (port) {
    case 0x3c7:
        *value = dac_state;
        return 1;
    case 0x3c8:
        *value = dac_write_index;
        return 1;
    case 0x3c9:
        *value = dac[dac_read_index][dac_read_component];
        if (++dac_read_component == 3) {
            dac_read_component = 0;
            dac_read_index++;
        }
        return 1;
    case 0x3da:
        *value = input_status1();
        return 1;
    default:
        return 0;
    }
}

int Game_DosVgaOut(uint16_t port, uint8_t value)
{
    switch (port) {
    case 0x3c7:
        dac_read_index = value;
        dac_read_component = 0;
        dac_state = 3;
        return 1;
    case 0x3c8:
        dac_write_index = value;
        dac_write_component = 0;
        dac_state = 0;
        return 1;
    case 0x3c9:
        dac[dac_write_index][dac_write_component] = value & 0x3f;
        if (++dac_write_component == 3) {
            const uint8_t *colour = dac[dac_write_index];
            Set_Palette_Value(dac_write_index, dac_level_to_8bit(colour[0]),
                              dac_level_to_8bit(colour[1]), dac_level_to_8bit(colour[2]));
            dac_write_component = 0;
            dac_write_index++;
        }
        return 1;
    default:
        return 0;
    }
}
