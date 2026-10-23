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

/* The Microsoft mouse driver of INT 33h for a DOS game.
 *
 * The functions are numbered in AX, the arguments are in BX, CX and DX.  The
 * driver counts the position in a virtual screen, 640 wide in the modes up to
 * 320 pixels (the game halves X) and as wide as the picture otherwise; the
 * game sets the range with functions 7 and 8.  The position is the host
 * mouse's in the picture of the game (Game_dosdisplay.c), so a function that
 * moves the cursor (4) and the ones that count the motion since the last call
 * only follow it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_hooks.h"
#include "Game_dosmouse.h"

static int mouse_min_x, mouse_max_x = -1;
static int mouse_min_y, mouse_max_y = -1;
static int last_x, last_y;

static int trace_mouse(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_MOUSE")) != NULL;
    return enabled;
}

/* The host mouse in the picture of the game, width x height pixels: the
 * picture is the largest 4:3 rectangle centred in the window (the same as
 * Game_dosdisplay.c shows it). */
static void screen_mouse(int width, int height, int *x, int *y, int *buttons)
{
    int window_x = 0, window_y = 0, window_width = 0, window_height = 0;
    int picture_width, picture_height;
    const Uint32 state = SDL_GetMouseState(&window_x, &window_y);

    if (Game_Window != NULL) SDL_GetWindowSize(Game_Window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        window_width = width;
        window_height = height;
    }
    picture_width = window_width;
    picture_height = window_width * 3 / 4;
    if (picture_height > window_height) {
        picture_height = window_height;
        picture_width = window_height * 4 / 3;
    }
    if (picture_width <= 0 || picture_height <= 0) picture_width = picture_height = 1;

    *x = (window_x - (window_width - picture_width) / 2) * width / picture_width;
    *y = (window_y - (window_height - picture_height) / 2) * height / picture_height;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > width - 1) *x = width - 1;
    if (*y > height - 1) *y = height - 1;

    *buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) *buttons |= 1;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) *buttons |= 2;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) *buttons |= 4;
}

/* The position in the virtual screen, clamped to the range the game set. */
static void mouse_position(int *x, int *y, int *buttons)
{
    const int width = (int)Render_Width > 0 ? (int)Render_Width : 320;
    const int height = (int)Render_Height > 0 ? (int)Render_Height : 200;
    const int virtual_width = width <= 320 ? 2 * width : width;
    int px, py;

    if (mouse_max_x < 0) mouse_max_x = virtual_width - 1;
    if (mouse_max_y < 0) mouse_max_y = height - 1;

    screen_mouse(width, height, &px, &py, buttons);
    *x = mouse_min_x + (int)((int64_t)px * (mouse_max_x - mouse_min_x + 1) / width);
    *y = mouse_min_y + (int)((int64_t)py * (mouse_max_y - mouse_min_y + 1) / height);
    if (*x < mouse_min_x) *x = mouse_min_x;
    if (*x > mouse_max_x) *x = mouse_max_x;
    if (*y < mouse_min_y) *y = mouse_min_y;
    if (*y > mouse_max_y) *y = mouse_max_y;
}

int Game_DosMouseCall(Game_DosRegs *regs)
{
    const uint16_t function = Game_DosAX(regs);
    int x, y, buttons;

    if (Game_DosAH(regs) != 0) return 0;

    mouse_position(&x, &y, &buttons);

    switch (function) {
    case 0x0000:
        /* reset: the driver is there, with this many buttons */
        Game_DosSetAX(regs, 0xffff);
        Game_DosSetBX(regs, 3);
        mouse_min_x = mouse_min_y = 0;
        mouse_max_x = mouse_max_y = -1;
        break;
    case 0x0003:
        /* the position and the buttons */
        Game_DosSetBX(regs, (uint16_t)buttons);
        Game_DosSetCX(regs, (uint16_t)x);
        Game_DosSetDX(regs, (uint16_t)y);
        break;
    case 0x0005:
    case 0x0006:
        /* the presses and the releases of a button since the last call: the
         * state of the buttons, none counted */
        Game_DosSetAX(regs, (uint16_t)buttons);
        Game_DosSetBX(regs, 0);
        Game_DosSetCX(regs, (uint16_t)x);
        Game_DosSetDX(regs, (uint16_t)y);
        break;
    case 0x0007:
        mouse_min_x = Game_DosCX(regs);
        mouse_max_x = Game_DosDX(regs);
        break;
    case 0x0008:
        mouse_min_y = Game_DosCX(regs);
        mouse_max_y = Game_DosDX(regs);
        break;
    case 0x000b:
        /* the motion since the last call, in mickeys */
        Game_DosSetCX(regs, (uint16_t)(int16_t)(x - last_x));
        Game_DosSetDX(regs, (uint16_t)(int16_t)(y - last_y));
        last_x = x;
        last_y = y;
        break;
    default:
        /* show and hide the cursor (1, 2), the position (4), the handler
         * (0Ch), the rate (1Ch) and the rest: nothing to do, the game draws
         * its own cursor and the position is the host's */
        break;
    }

    if (trace_mouse()) {
        fprintf(stderr, "MOUSE ax=%04x -> x=%d y=%d buttons=%d\n", function, x, y, buttons);
    }
    Game_DosSetCarry(regs, 0);
    return 1;
}
