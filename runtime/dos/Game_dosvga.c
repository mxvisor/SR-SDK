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
#include <stdlib.h>
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

/* The VESA BIOS, VBE 1.2: the 256-colour modes of vbe_modes, packed pixels in
 * a frame of VBE_MEMORY bytes (the card's memory) of which the 64 KB window at
 * A0000h shows one bank, as the card's window A does with a granularity of
 * 64 KB.  The translated code writes the window as plain memory; when the bank
 * changes (4F05h) the window goes back into the frame and the new bank comes
 * into it.  The picture is the frame from the display start (4F07h), a line
 * every vbe_pitch bytes (4F06h), the bank in the window from the window; the
 * flip thread makes it under planes_lock, which the game's thread holds while
 * the window changes banks.  No linear frame buffer (VBE 2.0), no window B,
 * no window function to call (WinFuncPtr 0): a game changes banks with
 * INT 10h. */
#define VBE_MEMORY (2u * 1024u * 1024u)
#define VBE_BANK 0x10000u
static const struct { uint16_t mode, width, height; } vbe_modes[] = {
    { 0x100, 640, 400 }, { 0x101, 640, 480 }, { 0x103, 800, 600 }, { 0x105, 1024, 768 }
};
static uint8_t *vbe_frame;
static int vbe_mode = -1;               /* the index of the mode set in vbe_modes, -1: none */
static uint32_t vbe_bank;               /* the bank in the window */
static uint32_t vbe_pitch;              /* bytes from a line to the next */
static uint32_t vbe_start;              /* the frame's byte at the top left of the picture */

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

/* the window into the bank of the frame it shows (under planes_lock) */
static void vbe_window_flush(void)
{
    const uint32_t offset = vbe_bank * VBE_BANK;
    if (offset < VBE_MEMORY) memcpy(vbe_frame + offset, Game_FrameBuffer, VBE_MEMORY - offset < VBE_BANK ? VBE_MEMORY - offset : VBE_BANK);
}

static void vbe_set_bank(uint32_t bank)
{
    const uint32_t offset = bank * VBE_BANK;
    SDL_LockMutex(planes_lock);
    vbe_window_flush();
    vbe_bank = bank;
    if (offset < VBE_MEMORY) memcpy(Game_FrameBuffer, vbe_frame + offset, VBE_MEMORY - offset < VBE_BANK ? VBE_MEMORY - offset : VBE_BANK);
    else memset(Game_FrameBuffer, 0, VBE_BANK);
    SDL_UnlockMutex(planes_lock);
}

/* The flip thread: a VESA mode from the frame through the palette */
static void flip_vbe(uint32_t *out)
{
    const uint32_t *palette = Game_GetPalette();
    const uint32_t width = vbe_modes[vbe_mode].width, height = vbe_modes[vbe_mode].height;
    uint32_t x, y;

    Render_Width = width;
    Render_Height = height;
    SDL_LockMutex(planes_lock);
    vbe_window_flush();
    for (y = 0; y < height; y++) {
        const uint32_t line = vbe_start + y * vbe_pitch;
        const uint8_t *pixels = vbe_frame + line;
        if (line + width > VBE_MEMORY) {
            memset(out, 0, (size_t)(height - y) * width * sizeof(*out));
            break;
        }
        for (x = 0; x < width; x++) out[x] = palette[pixels[x]];
        out += width;
    }
    SDL_UnlockMutex(planes_lock);
}

/* The flip thread: mode 12h from the planes (the plane in the window from the
 * window, where the game may have written since), a VESA mode from its frame,
 * the other modes as the display shows them. */
static void flip_planar(void *src, void *dst)
{
    static uint64_t spread[256];
    const uint32_t *palette = Game_GetPalette();
    const uint8_t *window = Game_FrameBuffer;
    uint32_t *out = (uint32_t *)dst;
    uint32_t colours[16];
    const uint8_t *source[4];
    unsigned int index, plane, bit;

    if (vbe_mode >= 0) {
        flip_vbe((uint32_t *)dst);
        return;
    }
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

/* the flip of mode 12h and of the VESA modes in place of the display's */
static void install_flip(void)
{
    if (display_flip != NULL) return;
    planes_lock = SDL_CreateMutex();
    display_flip = Display_Flip_Procedure;
    display_width = Render_Width;
    display_height = Render_Height;
    Display_Flip_Procedure = &flip_planar;
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
        vbe_mode = -1;
        if (clear) memset(Game_FrameBuffer, 0, DOS_VGA_SIZE);
        if (planes_lock != NULL) SDL_UnlockMutex(planes_lock);
        return;
    }
    install_flip();
    vbe_mode = -1;
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

static void put16(uint8_t *block, unsigned int offset, uint16_t value) { memcpy(block + offset, &value, 2); }
static void put32(uint8_t *block, unsigned int offset, uint32_t value) { memcpy(block + offset, &value, 4); }

static int vbe_find(uint16_t mode)
{
    unsigned int index;
    for (index = 0; index < sizeof(vbe_modes) / sizeof(vbe_modes[0]); index++)
        if (vbe_modes[index].mode == (mode & 0x1ffu)) return (int)index;
    return -1;
}

/* AH=4Fh.  The block of 4F00h (VbeInfoBlock, 256 bytes) and of 4F01h
 * (ModeInfoBlock, 256 bytes) at buffer; the mode list of 4F00h is in the
 * reserved part of its block, at offset E0h. */
int Game_DosVbe(Game_DosRegs *regs, uint32_t buffer)
{
    static int warned;
    uint8_t *block;
    int index;

    switch (Game_DosAL(regs)) {
    case 0x00: {
        /* the controller: "VESA", 1.2, the modes, the memory */
        static const char oem[] = GAME_NAME " VBE";
        unsigned int count;
        block = (uint8_t *)Game_DosMemPtr(buffer, 256);
        if (block == NULL) break;
        memset(block, 0, 256);
        memcpy(block, "VESA", 4);
        put16(block, 0x04, 0x0102);
        memcpy(block + 0xc0, oem, sizeof(oem) < 0x20 ? sizeof(oem) : 0x1f);
        put32(block, 0x06, ((buffer >> 4) << 16) | ((buffer & 0x0f) + 0xc0));
        put32(block, 0x0a, 0);                  /* capabilities: a 6-bit DAC */
        put32(block, 0x0e, ((buffer >> 4) << 16) | ((buffer & 0x0f) + 0xe0));
        for (count = 0; count < sizeof(vbe_modes) / sizeof(vbe_modes[0]); count++) put16(block, 0xe0 + 2 * count, vbe_modes[count].mode);
        put16(block, 0xe0 + 2 * count, 0xffff);
        put16(block, 0x12, VBE_MEMORY / VBE_BANK);
        Game_DosSetAX(regs, 0x004f);
        return 1;
    }
    case 0x01:
        /* a mode: CX */
        index = vbe_find(Game_DosCX(regs));
        block = (uint8_t *)Game_DosMemPtr(buffer, 256);
        if (index < 0 || block == NULL) break;
        memset(block, 0, 256);
        put16(block, 0x00, 0x001b);             /* supported, extra information, colour, graphics */
        block[0x02] = 0x07;                     /* window A: there, readable, writable */
        put16(block, 0x04, VBE_BANK / 1024);    /* granularity and size, KB */
        put16(block, 0x06, VBE_BANK / 1024);
        put16(block, 0x08, DOS_VGA_ADDRESS >> 4);
        put16(block, 0x10, vbe_modes[index].width);
        put16(block, 0x12, vbe_modes[index].width);
        put16(block, 0x14, vbe_modes[index].height);
        block[0x16] = 8;                        /* the character cell */
        block[0x17] = 16;
        block[0x18] = 1;                        /* planes */
        block[0x19] = 8;                        /* bits per pixel */
        block[0x1a] = 1;                        /* banks (of the memory model) */
        block[0x1b] = 4;                        /* packed pixels */
        block[0x1d] = (uint8_t)(VBE_MEMORY / ((uint32_t)vbe_modes[index].width * vbe_modes[index].height) - 1);
        block[0x1e] = 1;
        Game_DosSetAX(regs, 0x004f);
        return 1;
    case 0x02:
        /* set a mode: BX, bit 15 = keep the memory */
        index = vbe_find(Game_DosBX(regs));
        if (index < 0) break;
        if (vbe_frame == NULL) {
            vbe_frame = (uint8_t *)calloc(VBE_MEMORY, 1);
            if (vbe_frame == NULL) break;
        }
        install_flip();
        SDL_LockMutex(planes_lock);
        planar = 0;
        window_plane = -1;
        if ((Game_DosBX(regs) & 0x8000u) == 0) {
            memset(vbe_frame, 0, VBE_MEMORY);
            memset(Game_FrameBuffer, 0, DOS_VGA_SIZE);
        }
        vbe_mode = index;
        vbe_bank = 0;
        vbe_pitch = vbe_modes[index].width;
        vbe_start = 0;
        SDL_UnlockMutex(planes_lock);
        if (Game_DosBX(regs) & 0x4000u) {
            /* a linear frame buffer (VBE 2.0) is not there */
            vbe_mode = -1;
            break;
        }
        Game_DosRequestPresent();
        Game_DosSetAX(regs, 0x004f);
        return 1;
    case 0x03:
        Game_DosSetBX(regs, vbe_mode >= 0 ? vbe_modes[vbe_mode].mode : 0x0013);
        Game_DosSetAX(regs, 0x004f);
        return 1;
    case 0x05:
        /* the window: BH = 0 set, 1 get; BL = the window (A); DX = the bank */
        if ((uint8_t)regs->ebx != 0 || vbe_mode < 0) break;
        if ((uint8_t)(regs->ebx >> 8) == 0) vbe_set_bank(Game_DosDX(regs));
        else Game_DosSetDX(regs, (uint16_t)vbe_bank);
        Game_DosSetAX(regs, 0x004f);
        return 1;
    case 0x06: {
        /* the logical line: BL = 0 set CX pixels, 1 get, 2 set CX bytes, 3 the largest */
        const uint8_t function = (uint8_t)regs->ebx;
        if (vbe_mode < 0 || function > 3) break;
        if (function == 0 || function == 2) {
            const uint32_t pitch = Game_DosCX(regs);
            if (pitch < vbe_modes[vbe_mode].width || pitch * vbe_modes[vbe_mode].height > VBE_MEMORY) break;
            SDL_LockMutex(planes_lock);
            vbe_pitch = pitch;
            SDL_UnlockMutex(planes_lock);
        }
        if (function == 3) {
            Game_DosSetBX(regs, (uint16_t)(VBE_MEMORY / vbe_modes[vbe_mode].height > 0xffffu ? 0xffffu : VBE_MEMORY / vbe_modes[vbe_mode].height));
            Game_DosSetCX(regs, Game_DosBX(regs));
        } else {
            Game_DosSetBX(regs, (uint16_t)vbe_pitch);
            Game_DosSetCX(regs, (uint16_t)vbe_pitch);
            Game_DosSetDX(regs, (uint16_t)(VBE_MEMORY / vbe_pitch > 0xffffu ? 0xffffu : VBE_MEMORY / vbe_pitch));
        }
        Game_DosSetAX(regs, 0x004f);
        return 1;
    }
    case 0x07: {
        /* the display start: BL = 00h/80h set (CX pixel, DX line), 01h get */
        const uint8_t function = (uint8_t)regs->ebx;
        if (vbe_mode < 0) break;
        if (function == 0x00 || function == 0x80) {
            SDL_LockMutex(planes_lock);
            vbe_start = Game_DosDX(regs) * vbe_pitch + Game_DosCX(regs);
            SDL_UnlockMutex(planes_lock);
            Game_DosRequestPresent();
        } else if (function == 0x01) {
            regs->ebx &= 0xffff00ffu;
            Game_DosSetCX(regs, (uint16_t)(vbe_start % vbe_pitch));
            Game_DosSetDX(regs, (uint16_t)(vbe_start / vbe_pitch));
        } else break;
        Game_DosSetAX(regs, 0x004f);
        return 1;
    }
    case 0x08:
        /* the DAC's width: 6 bits, whatever is asked */
        if ((uint8_t)regs->ebx > 1) break;
        regs->ebx = (regs->ebx & 0xffff00ffu) | 0x0600u;
        Game_DosSetAX(regs, 0x004f);
        return 1;
    default:
        break;
    }
    if (!warned) {
        warned = 1;
        fprintf(stderr, GAME_NAME ": VESA BIOS function %04x (BX=%04x CX=%04x) not supported\n", Game_DosAX(regs), Game_DosBX(regs), Game_DosCX(regs));
    }
    Game_DosSetAX(regs, 0x014f);
    return 1;
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
