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
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "display.h"
#include "Game_dosvga.h"
#include "Game_dosmem.h"
#include "Game_dosrt.h"
#include "Game_hooks.h"

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

/* Mode 12h, 640x480 in 16 colours: four bit planes of 38400 bytes each at
 * A0000h, a byte of a plane being eight pixels.  The translated code writes the
 * VGA window as plain memory, so the sequencer cannot route a write to the
 * planes of its Map Mask; the window holds one plane at a time instead.  When
 * the Map Mask or the Read Map Select of the graphics controller changes, the
 * window goes back into the planes it was written for and the plane now
 * addressed comes into it: the plane to read if the Read Map Select was set
 * after the Map Mask (a game reading the planes one by one, to save what it
 * draws over), else the plane of the Map Mask (with several planes there, the
 * one to read if it is one of them).  A write reaches the planes of the Map
 * Mask, as far as it changed the byte of the plane the window held.  Only write mode 0 with all bits of the Bit Mask
 * is there (no latches, no set/reset): what a game drawing whole bytes per
 * plane uses.  The picture is the four planes through the attribute
 * controller's 16 palette registers into the DAC; the flip thread makes it
 * under planes_lock, which the game's thread holds while the window changes
 * planes (a game copying a picture tile by tile changes them hundreds of times
 * a frame), so a picture never has a plane that is halfway into the window. */
#define PLANAR_WIDTH 640
#define PLANAR_HEIGHT 480
#define PLANE_BYTES (PLANAR_WIDTH / 8 * PLANAR_HEIGHT)

static uint8_t planes[4][PLANE_BYTES];
static uint8_t snapshot[PLANE_BYTES];   /* the window as it was loaded */
static int planar;                      /* mode 12h is set */
static int window_plane = -1;           /* the plane in the window, -1: none */
static uint8_t window_mask;             /* the planes its writes go to */
static SDL_mutex *planes_lock;
static uint8_t seq_index, map_mask = 0x0f;
static int reading;                     /* Read Map Select set after the Map Mask */
static uint8_t gc_index, gc[9] = { 0, 0, 0, 0, 0, 0, 0, 0x0f, 0xff };
static uint8_t attribute[16];
static Game_Flip_Procedure display_flip;
static uint32_t display_width, display_height;

static void window_flush(void)
{
    const uint8_t *window = Game_FrameBuffer;
    unsigned int plane, index;

    if (window_plane < 0 || window_mask == 0) return;
    if (window_mask == (1u << window_plane)) {
        memcpy(planes[window_plane], window, PLANE_BYTES);
        return;
    }
    for (index = 0; index < PLANE_BYTES; index++) {
        if (window[index] == snapshot[index]) continue;
        for (plane = 0; plane < 4; plane++)
            if (window_mask & (1u << plane)) planes[plane][index] = window[index];
    }
}

/* the plane the window shows for the Map Mask and the Read Map Select */
static void window_select(void)
{
    const uint8_t mask = map_mask & 0x0f, read_map = gc[4] & 3;
    int plane;

    if (reading) plane = read_map;
    else if (mask == 1 || mask == 2 || mask == 4 || mask == 8) plane = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
    else if (mask == 0 || (mask & (1u << read_map))) plane = read_map;
    else plane = (mask & 1) ? 0 : (mask & 2) ? 1 : (mask & 4) ? 2 : 3;
    if (plane == window_plane && mask == window_mask) return;
    SDL_LockMutex(planes_lock);
    window_flush();
    window_plane = plane;
    window_mask = mask;
    memcpy(Game_FrameBuffer, planes[plane], PLANE_BYTES);
    if (mask != (1u << plane)) memcpy(snapshot, Game_FrameBuffer, PLANE_BYTES);
    SDL_UnlockMutex(planes_lock);
}

/* The flip thread: mode 12h from the planes (the plane in the window from the
 * window, where the game may have written since), the other modes as the
 * display shows them. */
static void flip_planar(void *src, void *dst)
{
    static uint64_t spread[256];
    const uint32_t *palette = Game_GetPalette();
    const uint8_t *window = Game_FrameBuffer;
    uint32_t *out = (uint32_t *)dst;
    uint32_t colours[16];
    const uint8_t *source[4];
    unsigned int index, plane, bit;

    if (!planar) {
        Render_Width = display_width;
        Render_Height = display_height;
        display_flip(src, dst);
        return;
    }
    Render_Width = PLANAR_WIDTH;
    Render_Height = PLANAR_HEIGHT;
    if (spread[1] == 0) {
        /* bit 7 of a byte is its first pixel: to byte 0 of the eight */
        for (index = 0; index < 256; index++)
            for (bit = 0; bit < 8; bit++)
                if (index & (0x80u >> bit)) spread[index] |= (uint64_t)1 << (bit * 8);
    }
    for (index = 0; index < 16; index++) colours[index] = palette[attribute[index] & 0x3f];
    SDL_LockMutex(planes_lock);
    for (plane = 0; plane < 4; plane++)
        source[plane] = (window_plane == (int)plane && window_mask == (1u << plane)) ? window : planes[plane];
    for (index = 0; index < PLANE_BYTES; index++) {
        const uint64_t pixels = spread[source[0][index]] | (spread[source[1][index]] << 1) |
                                (spread[source[2][index]] << 2) | (spread[source[3][index]] << 3);
        for (bit = 0; bit < 8; bit++) out[bit] = colours[(pixels >> (bit * 8)) & 0x0f];
        out += 8;
    }
    SDL_UnlockMutex(planes_lock);
}

void Game_DosVgaSetMode(uint8_t mode, int clear)
{
    static const uint8_t ega_palette[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
                                             0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
    /* the BIOS clears the video memory when it sets a mode: a game leaving
     * mode 12h does not see its planes in the window as a picture */
    if (mode != 0x12) {
        if (planes_lock != NULL) SDL_LockMutex(planes_lock);
        planar = 0;
        window_plane = -1;
        if (clear) memset(Game_FrameBuffer, 0, DOS_VGA_SIZE);
        if (planes_lock != NULL) SDL_UnlockMutex(planes_lock);
        return;
    }
    if (display_flip == NULL) {
        planes_lock = SDL_CreateMutex();
        display_flip = Display_Flip_Procedure;
        display_width = Render_Width;
        display_height = Render_Height;
        Display_Flip_Procedure = &flip_planar;
    }
    memcpy(attribute, ega_palette, sizeof(attribute));
    SDL_LockMutex(planes_lock);
    if (clear) {
        memset(planes, 0, sizeof(planes));
        memset(Game_FrameBuffer, 0, DOS_VGA_SIZE);
    }
    map_mask = 0x0f;
    gc[4] = 0;
    reading = 0;
    window_plane = -1;
    window_mask = 0;
    planar = 1;
    SDL_UnlockMutex(planes_lock);
    window_select();
}

void Game_DosVgaSetAttribute(uint8_t index, uint8_t value)
{
    if (index < 16) attribute[index] = value;
}

uint8_t Game_DosVgaAttribute(uint8_t index)
{
    return index < 16 ? attribute[index] : 0;
}

/* the sequencer (3C4h/3C5h) and the graphics controller (3CEh/3CFh) */
static void register_out(uint16_t port, uint8_t value)
{
    static int warned;

    switch (port) {
    case 0x3c4: seq_index = value; break;
    case 0x3c5:
        if (seq_index == 2) {
            map_mask = value;
            reading = 0;
            if (planar) window_select();
        }
        break;
    case 0x3ce: gc_index = value; break;
    case 0x3cf:
        if (gc_index < sizeof(gc)) gc[gc_index] = value;
        if (gc_index == 4) {
            reading = 1;
            if (planar) window_select();
        }
        if (planar && !warned && ((gc[5] & 3) != 0 || gc[8] != 0xff || gc[1] != 0)) {
            warned = 1;
            fprintf(stderr, GAME_NAME ": VGA write mode %u, bit mask %02x, set/reset %02x not emulated\n", gc[5] & 3, gc[8], gc[1]);
        }
        break;
    }
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
    case 0x3c4: *value = seq_index; return 1;
    case 0x3c5: *value = seq_index == 2 ? map_mask : 0; return 1;
    case 0x3ce: *value = gc_index; return 1;
    case 0x3cf: *value = gc_index < sizeof(gc) ? gc[gc_index] : 0; return 1;
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
    case 0x3c4:
    case 0x3c5:
    case 0x3ce:
    case 0x3cf:
        register_out(port, value);
        return 1;
    default:
        return 0;
    }
}
