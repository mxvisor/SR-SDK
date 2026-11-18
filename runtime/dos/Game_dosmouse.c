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
 * mouse's in the picture of the game (display/pc.c), so a function that
 * moves the cursor (4) and the ones that count the motion since the last call
 * only follow it.
 *
 * A game may poll it (function 3) or hand the driver an event handler
 * (function 0Ch, swapped by 14h), which the driver calls from its interrupt
 * when the mouse moves or a button goes down or up.  Here the interrupt is
 * Game_DosMouseDeliverIrq, at the same points as the keyboard's: the button
 * events of the event loop, one call each, and a move to where the mouse is.
 * A game that uses the driver (it reset it, function 0, or set a handler)
 * draws its own pointer (the driver's, functions 1 and 9, is not drawn here),
 * so the host's is hidden over the window, as a DOS screen had one pointer.
 * While the handler is set the mouse is held in the window too, as the
 * mouse of a DOS screen never left it: the first click in the window (which
 * the game does not see) puts SDL's mouse in relative mode, the position still
 * its in the window, until the window loses the focus; the port's panel shows
 * the host's pointer, free, while it is open.
 *
 * With the mod Gyro_Mouse (core/Game_gyro.h) on and a gyroscope there, the
 * position is the driver's own: turning the device moves it, and a touch away
 * from the on-screen buttons (gyro_mouse) and function 4 set it, so a tap puts
 * the pointer under the finger and the turns go on from there.  An on-screen
 * button may be the left button (TOUCHGAMEPAD_MOUSE_LEFT).  Another one may
 * make the touches the right button (TOUCHGAMEPAD_MOUSE_RIGHT): a touch on the
 * picture that comes down while it is held (the gamepad's pointer, as SDL's
 * mouse is the first finger's, the one on that button) is the right button,
 * where the finger is, until it is lifted (pad_mouse).
 *
 * The range covers the whole picture, unless the game's hook
 * GAME_DOS_MOUSE_MARGIN_Y(width, height) leaves rows out above and below it
 * (a game that draws a smaller screen in the middle of the mode's and sets
 * the range to that screen): a place on the picture is then the same place
 * of the game's, the motion of a touch or of the gyroscope as on the screen.
 *
 * A touch clicks only in the picture: one that comes down beside it, where
 * the on-screen buttons are, is not the mouse's (no press until it is lifted,
 * and with the gyroscope no move either).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_hooks.h"
#include "Game_dosmouse.h"
#include "Game_dosint.h"
#include "Game_thread.h"
#include "Game_gyro.h"
#include "Game_osd.h"
#include "touchgamepad.h"

/* the rows of the picture (width x height) above and below the part the
 * range covers */
#ifndef GAME_DOS_MOUSE_MARGIN_Y
#define GAME_DOS_MOUSE_MARGIN_Y(width, height) 0
#endif

static int mouse_min_x, mouse_max_x = -1;
static int mouse_min_y, mouse_max_y = -1;
static int last_x, last_y;
/* the event handler (function 0Ch): its address, the events it takes, and
 * what it was last told */
static uint32_t handler_address;
static uint16_t handler_mask;
static int handler_x, handler_y, handler_buttons;
/* the buttons as the event loop's events have them */
static int event_buttons;
/* the mouse held in the window: a click asked for it (the button whose release
 * the game does not see either), and SDL's relative mode is on */
static int hold_wanted, hold_skip_up, held;
/* Gyro_Mouse: the position, in fractions of the picture, while it is on, and
 * the on-screen gamepad's pointer when it was last looked at */
static int gyro_was_on;
static double gyro_x, gyro_y;
static unsigned int pointer_touches;
static float pointer_x, pointer_y;
/* the pointer's touch came down beside the picture */
static int pointer_beside;
/* the event loop's buttons pressed by a touch beside the picture, until it is
 * lifted (SDL's buttons, a bit each) */
static int touch_beside_events;
#if defined(__ANDROID__)
/* where the last touch in the picture left the mouse (-1: none yet) */
static int touch_last_x = -1, touch_last_y = -1;
/* the pointer's touch is the right button (pad_mouse), and the pointer's
 * touches when it was last looked at */
static int right_touch;
static unsigned int right_touches;
#endif
/* the mouse's buttons of the on-screen gamepad as pad_mouse gave them last */
static int pad_last;

static int trace_mouse(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_MOUSE")) != NULL;
    return enabled;
}

/* A place of the window (0..1) in the picture of the game, width x height
 * pixels: the picture is the largest 4:3 rectangle centred in the window (the
 * same as display/pc.c shows it).  Nonzero if it is in the picture; a place
 * beside it is clamped to its edge. */
static int window_to_picture(float window_fx, float window_fy, int width, int height, int *x, int *y)
{
    int inside;
    int window_width = 0, window_height = 0;
    int picture_width, picture_height;

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

    *x = ((int)floorf(window_fx * window_width + 0.5f) - (window_width - picture_width) / 2) * width / picture_width;
    *y = ((int)floorf(window_fy * window_height + 0.5f) - (window_height - picture_height) / 2) * height / picture_height;
    inside = *x >= 0 && *y >= 0 && *x < width && *y < height;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > width - 1) *x = width - 1;
    if (*y > height - 1) *y = height - 1;
    return inside;
}

/* Is a place of the window, in its pixels, in the picture? */
static int window_point_in_picture(int window_x, int window_y)
{
    int window_width = 0, window_height = 0, x, y;

    if (Game_Window != NULL) SDL_GetWindowSize(Game_Window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) return 1;
    return window_to_picture((float)window_x / window_width, (float)window_y / window_height, 320, 240, &x, &y);
}

/* The host's mouse in the picture, and its buttons.  On Android the mouse is
 * a touch's (SDL's of the first finger): beside the picture, and on the
 * on-screen keyboard, it stays where the last touch in the picture was, and a
 * button that goes down there stays up until it is released.  After the
 * keyboard closes the mouse stays until a touch moves it. */
static void screen_mouse(int width, int height, int *x, int *y, int *buttons)
{
    int window_x = 0, window_y = 0, window_width = 0, window_height = 0;
    const Uint32 state = SDL_GetMouseState(&window_x, &window_y);
    int inside;

    if (Game_Window != NULL) SDL_GetWindowSize(Game_Window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        window_width = width;
        window_height = height;
    }
    inside = window_to_picture((float)window_x / window_width, (float)window_y / window_height, width, height, x, y);

    *buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) *buttons |= 1;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) *buttons |= 2;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) *buttons |= 4;
#if defined(__ANDROID__)
    {
        /* the buttons pressed beside the picture, until they are released */
        static int touch_beside, before, keyboard_x = -1, keyboard_y = -1;

        /* where the keyboard's touches left SDL's mouse */
        if (TouchGamepad_KeyboardShown()) {
            keyboard_x = window_x;
            keyboard_y = window_y;
        } else if (window_x != keyboard_x || window_y != keyboard_y) {
            keyboard_x = -1;
        }
        if (keyboard_x >= 0) inside = 0;
        if (inside || touch_last_x < 0) {
            touch_last_x = *x;
            touch_last_y = *y;
        } else {
            *x = touch_last_x;
            *y = touch_last_y;
        }
        touch_beside &= *buttons;
        if (!inside) touch_beside |= *buttons & ~before;
        before = *buttons;
        *buttons &= ~touch_beside;
    }
#else
    (void)inside;
#endif
}

/* Gyro_Mouse: the driver's own position.  The touches are read from the
 * on-screen gamepad's pointer, not from SDL's mouse (that is a mouse of the
 * first finger, wherever it is, on a button too): a new touch away from the
 * buttons, and its motion, put the position under the finger; while the
 * finger is down the button is pressed.  The gyroscope moves the position. */
static void gyro_mouse(int width, int height, int *px, int *py, int *buttons)
{
    float fx, fy;
    int down;
    const unsigned int touches = TouchGamepad_Pointer(&fx, &fy, &down);
    double dx, dy;

    if (!gyro_was_on) {
        /* it starts where the host's mouse is */
        gyro_x = (*px + 0.5) / width;
        gyro_y = (*py + 0.5) / height;
        pointer_touches = touches;
        pointer_x = fx;
        pointer_y = fy;
        gyro_was_on = 1;
    }
    if (touches != pointer_touches || (down && (fx != pointer_x || fy != pointer_y))) {
        int x, y;
        const int inside = window_to_picture(fx, fy, width, height, &x, &y);

        /* a touch beside the picture is not the mouse's, wherever it goes */
        if (touches != pointer_touches) pointer_beside = !inside;
        if (!pointer_beside) {
            gyro_x = (x + 0.5) / width;
            gyro_y = (y + 0.5) / height;
        }
        pointer_touches = touches;
        pointer_x = fx;
        pointer_y = fy;
    }
    *buttons = (down && !pointer_beside) ? 1 : 0;

    Game_GyroTake(&dx, &dy);
    gyro_x += dx;
    gyro_y += dy;
    if (gyro_x < 0) gyro_x = 0;
    if (gyro_y < 0) gyro_y = 0;
    if (gyro_x > 1) gyro_x = 1;
    if (gyro_y > 1) gyro_y = 1;
    *px = (int)(gyro_x * width);
    *py = (int)(gyro_y * height);
    if (*px > width - 1) *px = width - 1;
    if (*py > height - 1) *py = height - 1;
}

/* The mouse's buttons of the on-screen gamepad: 1 the left one it holds, 2 a
 * touch that is the right button.  A touch of the gamepad's pointer that
 * comes down on the picture while its right button's modifier is held
 * (TOUCHGAMEPAD_MOUSE_RIGHT) is the right button until it is lifted, and puts
 * the mouse under the finger (the gyroscope's position is already there);
 * SDL's mouse of the same finger is not the left button then. */
static int pad_mouse(int width, int height, int *px, int *py, int *buttons)
{
    const int pad = TouchGamepad_MouseButtons();
#if defined(__ANDROID__)
    float fx, fy;
    int down, x, y;
    const unsigned int touches = TouchGamepad_Pointer(&fx, &fy, &down);

    if (touches != right_touches) {
        right_touches = touches;
        right_touch = (pad & 2) && down && window_to_picture(fx, fy, width, height, &x, &y);
    }
    if (!down) right_touch = 0;
    if (right_touch) {
        if (!Game_GyroOn()) {
            window_to_picture(fx, fy, width, height, &x, &y);
            *px = touch_last_x = x;
            *py = touch_last_y = y;
        }
        *buttons &= ~1;
        return (pad & 1) | 2;
    }
#else
    (void)width;
    (void)height;
    (void)px;
    (void)py;
    (void)buttons;
#endif
    return pad & 1;
}

/* The rows of the picture the range covers: from *top, *rows of them. */
static void range_rows(int height, int *top, int *rows)
{
    const int width = (int)Render_Width > 0 ? (int)Render_Width : 320;

    (void)width;
    *top = GAME_DOS_MOUSE_MARGIN_Y(width, height);
    *rows = height - 2 * *top;
    if (*top < 0 || *rows <= 0) {
        *top = 0;
        *rows = height;
    }
}

/* The position in the virtual screen, clamped to the range the game set. */
static void mouse_position(int *x, int *y, int *buttons)
{
    const int width = (int)Render_Width > 0 ? (int)Render_Width : 320;
    const int height = (int)Render_Height > 0 ? (int)Render_Height : 200;
    const int virtual_width = width <= 320 ? 2 * width : width;
    int px, py, top, rows;

    if (mouse_max_x < 0) mouse_max_x = virtual_width - 1;
    if (mouse_max_y < 0) mouse_max_y = height - 1;

    screen_mouse(width, height, &px, &py, buttons);
    if (Game_GyroOn()) gyro_mouse(width, height, &px, &py, buttons);
    else gyro_was_on = 0;
    /* the on-screen buttons that are the mouse's (TOUCHGAMEPAD_MOUSE_LEFT,
     * TOUCHGAMEPAD_MOUSE_RIGHT) */
    pad_last = pad_mouse(width, height, &px, &py, buttons);
    *buttons |= pad_last;

    *x = mouse_min_x + (int)((int64_t)px * (mouse_max_x - mouse_min_x + 1) / width);
    range_rows(height, &top, &rows);
    *y = mouse_min_y + (int)((int64_t)(py - top) * (mouse_max_y - mouse_min_y + 1) / rows);
    if (*x < mouse_min_x) *x = mouse_min_x;
    if (*x > mouse_max_x) *x = mouse_max_x;
    if (*y < mouse_min_y) *y = mouse_min_y;
    if (*y > mouse_max_y) *y = mouse_max_y;
}

void Game_DosMouseVideoMode(int width, int height)
{
    mouse_min_x = mouse_min_y = 0;
    mouse_max_x = (width <= 320 ? 2 * width : width) - 1;
    mouse_max_y = height - 1;
    if (trace_mouse()) fprintf(stderr, "MOUSE video mode: range x 0..%d, y 0..%d\n", mouse_max_x, mouse_max_y);
}

/* The host's pointer hidden once the game has reset the driver or has an
 * event handler: it draws its own. */
static int driver_reset;

static void host_pointer(void)
{
    static int hidden;
    const int hide = driver_reset || (handler_address != 0 && handler_mask != 0);

    if (hide == hidden) return;
    hidden = hide;
    Game_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE);
}

/* The hold as the focus, the handler and the panel have it now. */
static void hold_apply(void)
{
    int wanted;

    if (handler_address == 0 || handler_mask == 0 || Game_Window == NULL || SDL_GetKeyboardFocus() != Game_Window) {
        hold_wanted = 0;
    }
    wanted = hold_wanted && !Game_OsdOpen();
    if (wanted == held) return;
    held = wanted;
    Game_SetRelativeMouseMode(held);
}

/* the bits of the event handler's condition mask */
enum {
    MOUSE_MOVED = 0x01,
    MOUSE_LEFT_DOWN = 0x02,
    MOUSE_LEFT_UP = 0x04,
    MOUSE_RIGHT_DOWN = 0x08,
    MOUSE_RIGHT_UP = 0x10,
    MOUSE_MIDDLE_DOWN = 0x20,
    MOUSE_MIDDLE_UP = 0x40
};

/* Calls the event handler with the events of condition, the buttons and the
 * position; ESI and EDI are the motion since the last call, in mickeys (8 to
 * 8 points of the virtual screen, as the driver counts by default). */
static void call_handler(unsigned int condition, int x, int y, int buttons)
{
    Game_DosRegs regs;

    memset(&regs, 0, sizeof(regs));
    if (trace_mouse()) {
        fprintf(stderr, "MOUSE handler %08x events=%02x x=%d y=%d buttons=%d\n", (unsigned int)handler_address,
                condition, x, y, buttons);
    }
    regs.eax = condition;
    regs.ebx = (uint32_t)buttons;
    regs.ecx = (uint32_t)x;
    regs.edx = (uint32_t)y;
    regs.esi = (uint32_t)(int32_t)(int16_t)(x - handler_x);
    regs.edi = (uint32_t)(int32_t)(int16_t)(y - handler_y);
    handler_x = x;
    handler_y = y;
    handler_buttons = buttons;
    Game_DosCallGuestFar(handler_address, &regs);
}

/* The buttons of a button event of the event loop, after it. */
static int event_button_state(const SDL_Event *event, int buttons)
{
    int bit;

    switch (event->button.button) {
    case SDL_BUTTON_LEFT: bit = 1; break;
    case SDL_BUTTON_RIGHT: bit = 2; break;
    case SDL_BUTTON_MIDDLE: bit = 4; break;
    default: return buttons;
    }
    return event->type == SDL_MOUSEBUTTONDOWN ? (buttons | bit) : (buttons & ~bit);
}

/* The events of a change of the buttons from before to after. */
static unsigned int button_events(int before, int after)
{
    const int down = after & ~before, up = before & ~after;
    unsigned int condition = 0;

    if (down & 1) condition |= MOUSE_LEFT_DOWN;
    if (up & 1) condition |= MOUSE_LEFT_UP;
    if (down & 2) condition |= MOUSE_RIGHT_DOWN;
    if (up & 2) condition |= MOUSE_RIGHT_UP;
    if (down & 4) condition |= MOUSE_MIDDLE_DOWN;
    if (up & 4) condition |= MOUSE_MIDDLE_UP;
    return condition;
}

void Game_DosMouseDeliverIrq(void)
{
    int x, y, buttons;
    const int gyro = Game_GyroOn();

    /* a handler is called as an interrupt: not while the guest's IF is clear
     * or another handler runs (the events wait in the queue) */
    hold_apply();
    if (handler_address != 0 && !Game_DosGuestInterruptsEnabled()) return;

    mouse_position(&x, &y, &buttons);

    /* The host mouse's buttons, one event at a time: a click shorter than a
     * frame is both of its events here.  The events are taken whether or not
     * there is a handler, so one installed later starts from the mouse as it
     * is then.  With the gyroscope the button is the touch's (gyro_mouse),
     * not the one SDL makes of the first finger. */
    while (Game_MQueueRead != Game_MQueueWrite) {
        const SDL_Event event = Game_EventMQueue[Game_MQueueRead];
        unsigned int condition;
        int after;

        Game_MQueueRead = (Game_MQueueRead + 1u) & (GAME_MQUEUE_LENGTH - 1u);
        if (gyro || (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP)) continue;
        /* the click that holds the mouse (not a touch's), down and up */
        if (event.type == SDL_MOUSEBUTTONDOWN && !hold_wanted && handler_address != 0 && handler_mask != 0 &&
            event.button.which != SDL_TOUCH_MOUSEID) {
            hold_wanted = 1;
            hold_apply();
            /* (a window without the focus holds nothing: the click is the game's) */
            if (hold_wanted) {
                hold_skip_up = event.button.button;
                continue;
            }
        }
        if (event.type == SDL_MOUSEBUTTONUP && hold_skip_up != 0 && event.button.button == hold_skip_up) {
            hold_skip_up = 0;
            continue;
        }
        /* a touch that comes down beside the picture: neither its press nor
         * its release */
        if (event.button.which == SDL_TOUCH_MOUSEID && event.button.button >= 1 && event.button.button <= 8) {
            const int bit = 1 << (event.button.button - 1);

            if (event.type == SDL_MOUSEBUTTONDOWN && !window_point_in_picture(event.button.x, event.button.y)) {
                touch_beside_events |= bit;
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONUP && (touch_beside_events & bit)) {
                touch_beside_events &= ~bit;
                continue;
            }
        }
        after = event_button_state(&event, event_buttons);
        if (after == event_buttons) continue;
        event_buttons = after;
        if (handler_address == 0) continue;
        after |= pad_last;
        condition = button_events(handler_buttons, after);
        if ((condition & handler_mask) == 0) {
            handler_buttons = after;
            continue;
        }
        call_handler(condition | ((x != handler_x || y != handler_y) ? MOUSE_MOVED : 0), x, y, after);
        /* the handler may have cleared IF, or the window been closed */
        if (!Game_DosGuestInterruptsEnabled()) return;
    }
    /* a release the event loop did not pass on (the window lost the focus
     * first): the host's state no longer has the button */
    {
        const Uint32 state = SDL_GetMouseState(NULL, NULL);
        int host = 0;

        if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) host |= 1;
        if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) host |= 2;
        if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) host |= 4;
        event_buttons &= host;
    }
    if (handler_address == 0) return;

    /* the motion, and the buttons that are not the host mouse's events: the
     * on-screen gamepad's, the gyroscope's touch */
    if (!gyro) buttons = event_buttons | pad_last;
    {
        const unsigned int condition = button_events(handler_buttons, buttons) |
                                       ((x != handler_x || y != handler_y) ? MOUSE_MOVED : 0);

        if (condition & handler_mask) call_handler(condition, x, y, buttons);
        else handler_buttons = buttons;
    }
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
        handler_address = 0;
        handler_mask = 0;
        driver_reset = 1;
        host_pointer();
        break;
    case 0x000c:
        /* the event handler, at ES:EDX (flat: EDX), for the events in CX */
        handler_address = regs->edx;
        handler_mask = Game_DosCX(regs);
        handler_x = x;
        handler_y = y;
        handler_buttons = buttons;
        host_pointer();
        break;
    case 0x0014:
        /* the same, giving back the one before (the selector in ES is not
         * kept: the game is flat) */
        {
            const uint32_t old_address = handler_address;
            const uint16_t old_mask = handler_mask;

            handler_address = regs->edx;
            handler_mask = Game_DosCX(regs);
            handler_x = x;
            handler_y = y;
            handler_buttons = buttons;
            regs->edx = old_address;
            Game_DosSetCX(regs, old_mask);
        }
        host_pointer();
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
    case 0x0004:
        /* the position: the driver's own only with the gyroscope */
        if (gyro_was_on) {
            const int height = (int)Render_Height > 0 ? (int)Render_Height : 200;
            int top, rows;

            range_rows(height, &top, &rows);
            gyro_x = (double)((int16_t)Game_DosCX(regs) - mouse_min_x) / (mouse_max_x - mouse_min_x + 1);
            gyro_y = (top + (double)((int16_t)Game_DosDX(regs) - mouse_min_y) * rows / (mouse_max_y - mouse_min_y + 1)) /
                     height;
        }
        break;
    case 0x0007:
        mouse_min_x = Game_DosCX(regs);
        mouse_max_x = Game_DosDX(regs);
        if (trace_mouse()) fprintf(stderr, "MOUSE range x %d..%d\n", mouse_min_x, mouse_max_x);
        break;
    case 0x0008:
        mouse_min_y = Game_DosCX(regs);
        mouse_max_y = Game_DosDX(regs);
        if (trace_mouse()) fprintf(stderr, "MOUSE range y %d..%d\n", mouse_min_y, mouse_max_y);
        break;
    case 0x000b:
        /* the motion since the last call, in mickeys */
        Game_DosSetCX(regs, (uint16_t)(int16_t)(x - last_x));
        Game_DosSetDX(regs, (uint16_t)(int16_t)(y - last_y));
        last_x = x;
        last_y = y;
        break;
    default:
        /* show and hide the cursor (1, 2), the rate (1Ch) and the rest:
         * nothing to do, the game draws its own cursor */
        break;
    }

    if (trace_mouse()) {
        fprintf(stderr, "MOUSE ax=%04x -> x=%d y=%d buttons=%d\n", function, x, y, buttons);
    }
    Game_DosSetCarry(regs, 0);
    return 1;
}
