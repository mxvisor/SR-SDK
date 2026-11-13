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

/*
 *  DirectInput 3 and 5 (dinput.dll) for the recompiled game: the
 *  keyboard and the mouse, read from SDL, and the host's gamepads as
 *  joysticks, with force feedback. The COM vtables are llasm bridges
 *  (llasm/WinApi-dinput-asm.llasm); the objects live in guest memory.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "WinApi-dinput.h"
#include "WinApi-game.h"
#include "Game_hooks.h"
#include "Game_keys.h"
#include "Game_memory.h"
#include "touchgamepad.h"
#include "Game_osd.h"
#include "Game_thread.h"
#include "Game_pad.h"
#include "WinApi-guest.h"

/* GAME_WINAPI_TOUCH_GAMEPAD 1 (WinApi-user32.c): on Android a finger away from
 * the buttons is the mouse of a game that reads it here (default 0) */
#ifndef GAME_WINAPI_TOUCH_GAMEPAD
#define GAME_WINAPI_TOUCH_GAMEPAD 0
#endif

/* the clock of the guest (timeGetTime, GetTickCount): the time of a data record is read
   against it, not against SDL's */
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetTickCount(void);
#define Winapi_GetTicks() ((unsigned int)GetTickCount())
#else
extern "C" unsigned int CCALL Winapi_GetTicks(void);
#endif

#define DI_OK 0u
#define DIERR_UNSUPPORTED 0x80004001u /* E_NOTIMPL */
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_OUTOFMEMORY 0x8007000Eu
#define E_NOINTERFACE_ 0x80004002u

enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2, DEV_OTHER = 3, DEV_JOYSTICK = 4 };

struct joy_device;
typedef struct { uint32_t lpVtbl; uint32_t ref; uint32_t version; } di_object;
typedef struct { uint32_t lpVtbl; uint32_t ref; int kind; struct joy_device *joy; } di_device;

/* GUID_SysKeyboard 6F1D2B61-D5A0-11CF-BFC7-444553540000, GUID_SysMouse ...60 */
static int guid_kind(const uint8_t *g)
{
    static const uint8_t tail[12] = { 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    if (g == NULL || memcmp(g + 4, tail, 12) != 0 || g[1] != 0x2b || g[2] != 0x1d || g[3] != 0x6f) return DEV_OTHER;
    if (g[0] == 0x61) return DEV_KEYBOARD;
    if (g[0] == 0x60) return DEV_MOUSE;
    return DEV_OTHER;
}

/* Buffered keyboard data: the game reads keys with GetDeviceData, so key
   transitions are queued from an SDL event watch (called as events are pumped
   by the main thread) and handed out here, in order. */
#define KEY_QUEUE_SIZE 64

typedef struct { uint8_t dik; uint8_t down; uint32_t time; } key_event;

static key_event key_queue[KEY_QUEUE_SIZE];
static unsigned key_head, key_tail;
static uint32_t key_sequence;
static SDL_mutex *key_mutex;

static int SDLCALL key_event_watch(void *userdata, SDL_Event *event)
{
    uint8_t dik;
    (void)userdata;
    if ((event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) || event->key.repeat) return 0;
    if (Game_OsdHides(event)) return 0;
    dik = Game_PcScancode(event->key.keysym.scancode);
    if (dik == 0) return 0;
    SDL_LockMutex(key_mutex);
    if (((key_head + 1) % KEY_QUEUE_SIZE) != key_tail)
    {
        key_queue[key_head].dik = dik;
        key_queue[key_head].down = (event->type == SDL_KEYDOWN);
        key_queue[key_head].time = Winapi_GetTicks();
        key_head = (key_head + 1) % KEY_QUEUE_SIZE;
    }
    SDL_UnlockMutex(key_mutex);
    return 0;
}

/* The mouse, the same way: motion, wheel and buttons are collected from the
   event watch, for GetDeviceState (accumulated motion since the last call,
   current buttons) and for GetDeviceData (DIDEVICEOBJECTDATA records with the
   DIMOFS_* offsets of DIMOUSESTATE). */
#define MOUSE_QUEUE_SIZE 128

typedef struct { uint8_t ofs; uint32_t data; uint32_t time; } mouse_event;

static mouse_event mouse_queue[MOUSE_QUEUE_SIZE];
static unsigned mouse_head, mouse_tail;
static int32_t mouse_dx, mouse_dy, mouse_dz;
static uint8_t mouse_buttons_down[4];
static SDL_mutex *mouse_mutex;

static void mouse_queue_put(uint8_t ofs, uint32_t data, uint32_t time)
{
    if (((mouse_head + 1) % MOUSE_QUEUE_SIZE) == mouse_tail) return; /* overflow: DirectInput drops data too */
    mouse_queue[mouse_head].ofs = ofs;
    mouse_queue[mouse_head].data = data;
    mouse_queue[mouse_head].time = time;
    mouse_head = (mouse_head + 1) % MOUSE_QUEUE_SIZE;
}

#if GAME_WINAPI_TOUCH_GAMEPAD && defined(__ANDROID__)

#define TOUCHPAD_NO_FINGER (-1)
#define TOUCHPAD_TAP_MS 250     /* a finger on the glass this long at most is a click */
#define TOUCHPAD_TAP_MOVE 0.02f /* and it moved this far at most (of the window) */
#define TOUCHPAD_CLICK_MS 80    /* a game polling the state has to see the button */

extern int winapi_get_dd_mode(int *width, int *height);

static Sint64 touchpad_finger = TOUCHPAD_NO_FINGER;
static uint32_t touchpad_down_time, touchpad_click_end;
static float touchpad_moved, touchpad_rest_x, touchpad_rest_y;

/* The game draws its own cursor from the mouse's motion and SDL's mouse of a
 * finger is left out above: the free part of the screen is a touchpad, a drag
 * moves the cursor and a tap clicks. */
static void touchpad_event(const SDL_Event *event)
{
    float move_x, move_y;
    int width = 640, height = 480, dx, dy;

    switch (event->type)
    {
        case SDL_FINGERDOWN:
            if (touchpad_finger != TOUCHPAD_NO_FINGER) return;
            if (TouchGamepad_ButtonAt(event->tfinger.x, event->tfinger.y)) return;
            touchpad_finger = event->tfinger.fingerId;
            touchpad_down_time = Winapi_GetTicks();
            touchpad_moved = 0.0f;
            break;
        case SDL_FINGERMOTION:
            if (event->tfinger.fingerId != touchpad_finger) return;
            touchpad_moved += SDL_fabsf(event->tfinger.dx) + SDL_fabsf(event->tfinger.dy);
            winapi_get_dd_mode(&width, &height);
            move_x = event->tfinger.dx * width * 1.5f + touchpad_rest_x;
            move_y = event->tfinger.dy * height * 1.5f + touchpad_rest_y;
            dx = (int)move_x;
            dy = (int)move_y;
            touchpad_rest_x = move_x - dx;
            touchpad_rest_y = move_y - dy;
            SDL_LockMutex(mouse_mutex);
            mouse_dx += dx;
            mouse_dy += dy;
            if (dx) mouse_queue_put(0, (uint32_t)dx, Winapi_GetTicks()); /* DIMOFS_X */
            if (dy) mouse_queue_put(4, (uint32_t)dy, Winapi_GetTicks()); /* DIMOFS_Y */
            SDL_UnlockMutex(mouse_mutex);
            break;
        case SDL_FINGERUP:
            if (event->tfinger.fingerId != touchpad_finger) return;
            touchpad_finger = TOUCHPAD_NO_FINGER;
            if ((Winapi_GetTicks() - touchpad_down_time > TOUCHPAD_TAP_MS) || (touchpad_moved > TOUCHPAD_TAP_MOVE)) return;
            SDL_LockMutex(mouse_mutex);
            mouse_buttons_down[0] = 0x80;
            mouse_queue_put(12, 0x80, Winapi_GetTicks()); /* DIMOFS_BUTTON0 */
            touchpad_click_end = Winapi_GetTicks() + TOUCHPAD_CLICK_MS;
            SDL_UnlockMutex(mouse_mutex);
            break;
        default:
            break;
    }
}

/* mouse_mutex is held */
static void touchpad_end_click(void)
{
    if ((touchpad_click_end == 0) || ((int32_t)(Winapi_GetTicks() - touchpad_click_end) < 0)) return;
    touchpad_click_end = 0;
    mouse_buttons_down[0] = 0;
    mouse_queue_put(12, 0, Winapi_GetTicks()); /* DIMOFS_BUTTON0 */
}

#else

#define touchpad_event(event)
#define touchpad_end_click()
#endif

static void mouse_move(int dx, int dy)
{
    mouse_dx += dx;
    mouse_dy += dy;
    if (dx) mouse_queue_put(0, (uint32_t)dx, Winapi_GetTicks()); /* DIMOFS_X */
    if (dy) mouse_queue_put(4, (uint32_t)dy, Winapi_GetTicks()); /* DIMOFS_Y */
}

#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
/* <GAME>_DINPUT_EXCLUSIVE_MOUSE (Game_hooks.h): the game draws its cursor from the deltas and
   moves it itself to a menu item: a host pointer beside it would part from it, and a compositor
   does not let a program warp its pointer. As with the exclusive mouse of DirectInput the
   host's is hidden and held in the window, from the first click in the window (which the game
   does not see) until the window loses the focus. A window that the game closes and opens
   again (a new display mode: its 3D driver opens its own window) gives the focus to the new
   one: the hold goes on there without a click (mouse_hold_given). The watch only asks for it:
   the mode changes on the thread that polls the device, not in the middle of SDL's pump. */
static int mouse_held, mouse_hold_wanted, mouse_skip_up;
static int mouse_hold_given;            /* the click was made; a focus lost to another window ends it */
static uint32_t mouse_lost_window;      /* the window that lost the focus last */

static void mouse_apply_hold(void)
{
    /* the port's panel shows the host's pointer */
    int wanted = mouse_hold_wanted && !Game_OsdOpen();

    if ((mouse_held == wanted) && (SDL_GetRelativeMouseMode() == (mouse_held ? SDL_TRUE : SDL_FALSE))) return;
    mouse_held = wanted;
    Game_SetRelativeMouseMode(mouse_held);
}
#endif

static int SDLCALL mouse_event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;
    if (Game_OsdHides(event)) return 0;
    touchpad_event(event);
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    if ((event->type == SDL_WINDOWEVENT) && (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) &&
        ((SDL_GetKeyboardFocus() == NULL) || (SDL_GetWindowID(SDL_GetKeyboardFocus()) == event->window.windowID)))
    {
        /* (not when the new window of the game has the focus already) */
        mouse_hold_wanted = 0;
        mouse_lost_window = event->window.windowID;
    }
    if ((event->type == SDL_WINDOWEVENT) && (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED))
    {
        /* the focus back to the window that lost it: another program had it, a click holds again */
        if (event->window.windowID == mouse_lost_window) mouse_hold_given = 0;
        else if (mouse_hold_given) mouse_hold_wanted = 1;
    }
    if ((event->type == SDL_MOUSEMOTION) && !mouse_hold_wanted) return 0;
    if ((event->type == SDL_MOUSEBUTTONDOWN) && !mouse_hold_wanted)
    {
        if (event->button.which != SDL_TOUCH_MOUSEID) { mouse_hold_wanted = 1; mouse_hold_given = 1; mouse_skip_up = 1; }
        return 0;
    }
    if ((event->type == SDL_MOUSEBUTTONUP) && mouse_skip_up)
    {
        mouse_skip_up = 0;
        return 0;
    }
#endif
    switch (event->type)
    {
        case SDL_MOUSEMOTION:
            if (event->motion.which == SDL_TOUCH_MOUSEID) return 0;
            SDL_LockMutex(mouse_mutex);
            mouse_move(event->motion.xrel, event->motion.yrel);
            SDL_UnlockMutex(mouse_mutex);
            break;
        case SDL_MOUSEWHEEL:
            if (event->wheel.which == SDL_TOUCH_MOUSEID || event->wheel.y == 0) return 0;
            SDL_LockMutex(mouse_mutex);
            mouse_dz += event->wheel.y * 120; /* WHEEL_DELTA per notch */
            mouse_queue_put(8, (uint32_t)(event->wheel.y * 120), Winapi_GetTicks()); /* DIMOFS_Z */
            SDL_UnlockMutex(mouse_mutex);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            int button;
            if (event->button.which == SDL_TOUCH_MOUSEID) return 0;
            if (event->button.button == SDL_BUTTON_LEFT) button = 0;
            else if (event->button.button == SDL_BUTTON_RIGHT) button = 1;
            else if (event->button.button == SDL_BUTTON_MIDDLE) button = 2;
            else if (event->button.button == SDL_BUTTON_X1) button = 3;
            else return 0;
            SDL_LockMutex(mouse_mutex);
            mouse_buttons_down[button] = (event->type == SDL_MOUSEBUTTONDOWN) ? 0x80 : 0;
            mouse_queue_put((uint8_t)(12 + button), mouse_buttons_down[button], Winapi_GetTicks()); /* DIMOFS_BUTTONn */
            SDL_UnlockMutex(mouse_mutex);
            break;
        }
    }
    return 0;
}

static void mouse_init(void)
{
    if (mouse_mutex != NULL) return;
    mouse_mutex = SDL_CreateMutex();
#if GAME_WINAPI_TOUCH_GAMEPAD && defined(__ANDROID__)
    /* the touchpad is the mouse: SDL's own of a finger would click at once, on
     * the gamepad's buttons too */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    SDL_AddEventWatch(mouse_event_watch, NULL);
}

/* <GAME>_DEBUG_INPUT: which DirectInput calls the game makes for which device */
static int input_debug(void)
{
    static int value = -1;
    if (value < 0) value = (getenv(GAME_ENV("DEBUG_INPUT")) != NULL);
    return value;
}

/* with <GAME>_DEBUG_INPUT every SDL event that can mean a key press is listed:
   the DirectInput queue is not the only way the host can reach the game */
static int SDLCALL debug_event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;
    switch (event->type)
    {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            fprintf(stderr, "[sdl key %s scancode=%d repeat=%d t=%u]\n", (event->type == SDL_KEYDOWN) ? "down" : "up",
                    (int)event->key.keysym.scancode, event->key.repeat, Winapi_GetTicks());
            break;
        case SDL_TEXTINPUT:
            fprintf(stderr, "[sdl text input '%s']\n", event->text.text);
            break;
        case SDL_JOYAXISMOTION: case SDL_JOYHATMOTION: case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
        case SDL_CONTROLLERAXISMOTION: case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
        case SDL_JOYDEVICEADDED: case SDL_CONTROLLERDEVICEADDED:
            fprintf(stderr, "[sdl joystick/controller event type=0x%x]\n", event->type);
            break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            fprintf(stderr, "[sdl mouse button %d %s]\n", event->button.button, (event->type == SDL_MOUSEBUTTONDOWN) ? "down" : "up");
            break;
        default:
            break;
    }
    return 0;
}

static void key_queue_init(void)
{
    if (key_mutex != NULL) return;
    key_mutex = SDL_CreateMutex();
    SDL_AddEventWatch(key_event_watch, NULL);
    if (input_debug()) SDL_AddEventWatch(debug_event_watch, NULL);
    Game_AutoKeysStart();
}

/* ------------------------------------------------------------- joysticks */

/* The host's gamepads (Game_pad.h) as DirectInput shows an XInput pad: the
 * Nth pad there is is the joystick {6F1D2B70+N-D5A0-11CF-BFC7-444553540000}
 * (GUID_Joystick for the first).  Its objects: the X/Y axes the left stick,
 * Z the triggers (the left one up, the right one down), Rx/Ry the right
 * stick, Y down on both sticks; 10 buttons (A, B, X, Y, LB, RB, Back, Start,
 * the sticks' clicks); the D-pad the POV.  The axes go 0..65535 until the game
 * sets DIPROP_RANGE; with no DIPROP_DEADZONE of the game an axis has the dead
 * zones of Game_PadGetState, with one it is read raw and the game's applies.
 * GetDeviceState fills the game's data format (SetDataFormat: c_dfDIJoystick,
 * ...2 or the game's own), matched to the objects as DirectInput does.  A pad
 * that goes away reads centred.  Force feedback: the effects are made, played
 * and timed here, and the strongest of those playing drives the pad's motors.
 * A constant or ramp force for a time (a hit, a shot) runs the low-frequency
 * motor; one that runs until stopped is a wheel's steady load, felt on a pad
 * only as the jolt of its changes; a periodic force runs both motors, the
 * low-frequency one first for long periods.  The strength is taken through
 * a square root, as a motor is not felt below about a tenth of its range;
 * the conditions (spring, damper, inertia, friction) and custom forces are
 * made and stay silent, as they do on a pad without motors.  EnumDevices lists
 * the joysticks only, never the keyboard and the mouse. */

#define DI_NOEFFECT 1u
#define DIERR_NOTFOUND 0x80070002u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_NOTINITIALIZED 0x80070015u

#define DIDFT_RELAXIS 0x01u
#define DIDFT_ABSAXIS 0x02u
#define DIDFT_PSHBUTTON 0x04u
#define DIDFT_BUTTON 0x0Cu
#define DIDFT_POV 0x10u
#define DIDFT_ANYINSTANCE 0xFFFFu
#define DIDFT_FFACTUATOR 0x01000000u
#define DIDFT_INSTANCE(type) (((type) >> 8) & 0xFFFFu)

#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPH_BYID 2

#define DIPROP_BUFFERSIZE 1
#define DIPROP_GRANULARITY 3
#define DIPROP_RANGE 4
#define DIPROP_DEADZONE 5
#define DIPROP_SATURATION 6
#define DIPROP_FFGAIN 7
#define DIPROP_FFLOAD 8

#define JOY_DEVTYPE 0x00010404u /* DIDEVTYPE_JOYSTICK, ..JOYSTICK_GAMEPAD, DIDEVTYPE_HID */
#define JOY_AXES 5
#define JOY_BUTTONS 10
#define JOY_OBJECTS (JOY_AXES + 1 + JOY_BUTTONS)
#define JOY_POV_OBJECT JOY_AXES
#define JOY_EFFECTS 32
#define JOY_NONE (-1)

/* the object GUIDs GUID_XAxis..GUID_POV are A36D02xx-C9F3-11CF-BFC7-444553540000 */
static const uint8_t joy_guid_tail[15] = { 0x02, 0x6d, 0xa3, 0xf3, 0xc9, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
static const uint8_t joy_object_guid[JOY_OBJECTS] = {
    0xe0, 0xe1, 0xe2, 0xf4, 0xf5, 0xf2,
    0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
};
static const uint16_t joy_object_usage[JOY_AXES + 1] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x39 };
static const char *const joy_axis_name[JOY_AXES + 1] = { "X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Hat Switch" };

/* the object's dwType, and its offset in DIJOYSTATE (c_dfDIJoystick) */
static uint32_t joy_object_type(int object)
{
    if (object < JOY_AXES) return DIDFT_ABSAXIS | ((uint32_t)object << 8) | ((object < 2) ? DIDFT_FFACTUATOR : 0);
    if (object == JOY_POV_OBJECT) return DIDFT_POV;
    return DIDFT_PSHBUTTON | ((uint32_t)(object - JOY_AXES - 1) << 8);
}

static uint32_t joy_default_offset(int object)
{
    if (object < JOY_AXES) return (uint32_t)object * 4;
    if (object == JOY_POV_OBJECT) return 32;
    return (uint32_t)(48 + object - JOY_AXES - 1);
}

static void joy_object_guid_bytes(int object, uint8_t *guid)
{
    guid[0] = joy_object_guid[object];
    memcpy(guid + 1, joy_guid_tail, 15);
}

typedef struct { uint32_t ofs; int object; uint32_t type; } joy_map;

struct joy_device {
    int index;                          /* the Nth pad there is */
    uint32_t version;                   /* of DirectInputCreate: the size of the records */
    int32_t min[JOY_AXES], max[JOY_AXES];
    uint32_t deadzone[JOY_AXES], saturation[JOY_AXES];
    uint32_t buffer_size;
    joy_map *map;                       /* SetDataFormat */
    int map_count;
    uint32_t data_size;
    uint32_t gain;                      /* DIPROP_FFGAIN */
    int actuators_off, paused;
    uint32_t effects[JOY_EFFECTS];      /* the guest's IDirectInputEffect objects */
    int rumbling;
    uint16_t rumble_low, rumble_high;
    uint32_t rumble_sent;
};

/* the slot of the Nth pad, -1 when there is none */
static int joy_slot(int index)
{
    unsigned int slots = Game_PadSlots();
    int slot;

    for (slot = 0; slot < GAME_PAD_MAX; slot++)
    {
        if ((slots & (1u << slot)) == 0) continue;
        if (index == 0) return slot;
        index--;
    }
    return -1;
}

/* GUID_Joystick 6F1D2B70-..., the next pads after it: the pad's index, or -1 */
static int joy_guid_index(const uint8_t *g)
{
    static const uint8_t tail[12] = { 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    if (g == NULL || memcmp(g + 4, tail, 12) != 0 || g[1] != 0x2b || g[2] != 0x1d || g[3] != 0x6f) return -1;
    if (g[0] < 0x70 || g[0] >= 0x70 + GAME_PAD_MAX) return -1;
    return g[0] - 0x70;
}

/* <GAME>_DEBUG_PAD: the joystick calls, the states read and the motors */
static int joy_debug(void)
{
    static int value = -1;
    if (value < 0) value = (getenv(GAME_ENV("DEBUG_PAD")) != NULL) || input_debug();
    return value;
}

/* an axis -1..1 through the game's dead zone and saturation, into its range */
static int32_t joy_axis_value(const struct joy_device *j, int axis, double x)
{
    double m = (x < 0) ? -x : x, d = j->deadzone[axis] / 10000.0, s = j->saturation[axis] / 10000.0;
    int64_t centre, value;

    if (m > 1.0) m = 1.0;
    if (m <= d) m = 0.0;
    else if (m >= s) m = 1.0;
    else m = (m - d) / (s - d);
    centre = (int64_t)j->min[axis] + ((int64_t)j->max[axis] - j->min[axis]) / 2;
    if (x < 0) value = centre - (int64_t)(m * (double)(centre - j->min[axis]) + 0.5);
    else value = centre + (int64_t)(m * (double)(j->max[axis] - centre) + 0.5);
    return (int32_t)value;
}

typedef struct {
    int32_t axis[JOY_AXES];
    uint32_t pov;
    uint8_t button[JOY_BUTTONS];
} joy_state;

static int joy_read(const struct joy_device *j, joy_state *out)
{
    static const uint16_t order[JOY_BUTTONS] = {
        GAME_PAD_A, GAME_PAD_B, GAME_PAD_X, GAME_PAD_Y,
        GAME_PAD_LEFT_SHOULDER, GAME_PAD_RIGHT_SHOULDER, GAME_PAD_BACK, GAME_PAD_START,
        GAME_PAD_LEFT_THUMB, GAME_PAD_RIGHT_THUMB
    };
    Game_PadState cooked, raw;
    double value[JOY_AXES];
    int slot = joy_slot(j->index), axis, i, up, down, left, right, present;

    present = (slot >= 0) && Game_PadGetState(slot, &cooked) && Game_PadGetRawState(slot, &raw);
    if (!present)
    {
        memset(&cooked, 0, sizeof(cooked));
        memset(&raw, 0, sizeof(raw));
    }

    for (axis = 0; axis < JOY_AXES; axis++)
    {
        const Game_PadState *s = j->deadzone[axis] ? &raw : &cooked;
        switch (axis)
        {
            case 0: value[0] = s->left_x / 32767.0; break;
            case 1: value[1] = -s->left_y / 32767.0; break;
            case 2: value[2] = (s->left_trigger - s->right_trigger) / 255.0; break;
            case 3: value[3] = s->right_x / 32767.0; break;
            default: value[4] = -s->right_y / 32767.0; break;
        }
        out->axis[axis] = joy_axis_value(j, axis, value[axis]);
    }
    for (i = 0; i < JOY_BUTTONS; i++) out->button[i] = (cooked.buttons & order[i]) ? 0x80 : 0;

    up = (cooked.buttons & GAME_PAD_DPAD_UP) != 0;
    down = (cooked.buttons & GAME_PAD_DPAD_DOWN) != 0;
    left = (cooked.buttons & GAME_PAD_DPAD_LEFT) != 0;
    right = (cooked.buttons & GAME_PAD_DPAD_RIGHT) != 0;
    if (up && down) up = down = 0;
    if (left && right) left = right = 0;
    out->pov = 0xFFFFFFFFu;
    if (up) out->pov = right ? 4500 : (left ? 31500 : 0);
    else if (down) out->pov = right ? 13500 : (left ? 22500 : 18000);
    else if (right) out->pov = 9000;
    else if (left) out->pov = 27000;
    return present;
}

/* SetDataFormat: each object of the format takes the first free object of the
 * pad of its GUID (any with none), type and instance */
static uint32_t joy_set_format(struct joy_device *j, uint32_t format)
{
    const uint32_t *df = (const uint32_t *)GUEST_MEM(format);
    uint32_t count, objsize, i;
    int used[JOY_OBJECTS], object;
    joy_map *map;

    if (df == NULL || df[0] < 24 || df[1] < 16) return DIERR_INVALIDPARAM;
    objsize = df[1];
    count = df[4];
    map = (joy_map *)calloc(count ? count : 1, sizeof(*map));
    if (map == NULL) return DIERR_OUTOFMEMORY;
    memset(used, 0, sizeof(used));
    for (i = 0; i < count; i++)
    {
        const uint32_t *odf = (const uint32_t *)((const uint8_t *)GUEST_MEM(df[5]) + (size_t)i * objsize);
        const uint8_t *guid = odf[0] ? (const uint8_t *)GUEST_MEM(odf[0]) : NULL;
        uint32_t type = odf[2], kind = type & 0xFFu, instance = DIDFT_INSTANCE(type);

        map[i].ofs = odf[1];
        map[i].type = type;
        map[i].object = JOY_NONE;
        for (object = 0; object < JOY_OBJECTS; object++)
        {
            uint32_t otype = joy_object_type(object);
            uint8_t oguid[16];
            if (used[object]) continue;
            joy_object_guid_bytes(object, oguid);
            if (guid != NULL && memcmp(guid, oguid, 16) != 0) continue;
            if (kind != 0 && (kind & otype & 0xFFu) == 0) continue;
            if (instance != DIDFT_ANYINSTANCE && instance != DIDFT_INSTANCE(otype)) continue;
            if ((type & DIDFT_FFACTUATOR) && !(otype & DIDFT_FFACTUATOR)) continue;
            used[object] = 1;
            map[i].object = object;
            break;
        }
    }
    free(j->map);
    j->map = map;
    j->map_count = (int)count;
    j->data_size = df[3];
    if (joy_debug())
    {
        fprintf(stderr, "[dinput joystick %d data format: %u bytes, %u objects:", j->index, j->data_size, count);
        for (i = 0; i < count; i++) if (map[i].object != JOY_NONE) fprintf(stderr, " %u=%d", map[i].ofs, map[i].object);
        fprintf(stderr, "]\n");
    }
    return DI_OK;
}

static void joy_fill_state(struct joy_device *j, uint8_t *out, uint32_t size)
{
    joy_state st;
    int i;

    joy_read(j, &st);
    memset(out, 0, size);
    for (i = 0; i < j->map_count; i++)
    {
        const joy_map *m = &j->map[i];
        int object = m->object;
        uint32_t value;

        if (object > JOY_POV_OBJECT)
        {
            if (m->ofs < size) out[m->ofs] = st.button[object - JOY_AXES - 1];
            continue;
        }
        if (object == JOY_NONE && (m->type & DIDFT_BUTTON)) continue;
        if ((size_t)m->ofs + 4 > size) continue;
        if (object == JOY_NONE) value = (m->type & DIDFT_POV) ? 0xFFFFFFFFu : 0;
        else if (object == JOY_POV_OBJECT) value = st.pov;
        else value = (uint32_t)st.axis[object];
        memcpy(out + m->ofs, &value, 4);
    }
    if (joy_debug())
    {
        static joy_state last[GAME_PAD_MAX];
        if (j->index < GAME_PAD_MAX && memcmp(&last[j->index], &st, sizeof(st)) != 0)
        {
            last[j->index] = st;
            fprintf(stderr, "[dinput joystick %d: %d %d %d %d %d pov %d buttons", j->index, st.axis[0], st.axis[1], st.axis[2],
                    st.axis[3], st.axis[4], (int)st.pov);
            for (i = 0; i < JOY_BUTTONS; i++) fprintf(stderr, "%c", st.button[i] ? '1' + i % 9 : '.');
            fprintf(stderr, "]\n");
        }
    }
}

/* the object a property or GetObjectInfo names (DIPH_*), JOY_NONE if none */
static int joy_find_object(const struct joy_device *j, uint32_t obj, uint32_t how)
{
    int i;
    if (how == DIPH_BYOFFSET)
    {
        if (j->map != NULL)
        {
            for (i = 0; i < j->map_count; i++) if (j->map[i].ofs == obj && j->map[i].object != JOY_NONE) return j->map[i].object;
            return JOY_NONE;
        }
        for (i = 0; i < JOY_OBJECTS; i++) if (joy_default_offset(i) == obj) return i;
    }
    else if (how == DIPH_BYID)
    {
        for (i = 0; i < JOY_OBJECTS; i++)
        {
            uint32_t type = joy_object_type(i);
            if ((obj & type & 0xFFu) && DIDFT_INSTANCE(obj) == DIDFT_INSTANCE(type)) return i;
        }
    }
    return JOY_NONE;
}

static uint32_t joy_object_offset(const struct joy_device *j, int object)
{
    int i;
    for (i = 0; j->map != NULL && i < j->map_count; i++) if (j->map[i].object == object) return j->map[i].ofs;
    return joy_default_offset(object);
}

/* DIDEVICEOBJECTINSTANCE: 292 bytes (DirectInput 3), 316 (5) */
static void joy_fill_object(const struct joy_device *j, int object, uint8_t *out, uint32_t size)
{
    uint32_t type = joy_object_type(object);
    char name[32];

    memset(out, 0, size);
    *(uint32_t *)out = size;
    joy_object_guid_bytes(object, out + 4);
    *(uint32_t *)(out + 20) = joy_object_offset(j, object);
    *(uint32_t *)(out + 24) = type;
    *(uint32_t *)(out + 28) = ((type & DIDFT_FFACTUATOR) ? 1u : 0u) | ((object < JOY_AXES) ? 0x100u : 0u); /* DIDOI_FFACTUATOR, ..ASPECTPOSITION */
    if (object <= JOY_POV_OBJECT) snprintf(name, sizeof(name), "%s", joy_axis_name[object]);
    else snprintf(name, sizeof(name), "Button %d", object - JOY_AXES - 1);
    strcpy((char *)out + 32, name);
    if (size >= 316)
    {
        if (type & DIDFT_FFACTUATOR) { *(uint32_t *)(out + 292) = 10000; *(uint32_t *)(out + 296) = 100; }
        *(uint16_t *)(out + 304) = (object <= JOY_POV_OBJECT) ? 1 : 9;     /* wUsagePage */
        *(uint16_t *)(out + 306) = (object <= JOY_POV_OBJECT) ? joy_object_usage[object] : (uint16_t)(object - JOY_AXES);
    }
}

/* DIDEVICEINSTANCE: 560 bytes (DirectInput 3), 580 (5) */
static void joy_fill_instance(int index, uint8_t *out, uint32_t size)
{
    static const uint8_t joystick[16] = { 0x70,0x2b,0x1d,0x6f, 0xa0,0xd5, 0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
    /* the product: PIDVID 028E045E, the XInput pad's */
    static const uint8_t product[16] = { 0x5e,0x04,0x8e,0x02, 0,0, 0,0, 0x50,0x49,0x44,0x56,0x49,0x44,0x00,0x00 };
    int slot = joy_slot(index);
    const char *name = (slot >= 0) ? Game_PadName(slot) : "Gamepad";

    memset(out, 0, size);
    *(uint32_t *)out = size;
    memcpy(out + 4, joystick, 16);
    out[4] = (uint8_t)(0x70 + index);
    memcpy(out + 20, product, 16);
    *(uint32_t *)(out + 36) = JOY_DEVTYPE;
    snprintf((char *)out + 40, 260, "%s", name);
    snprintf((char *)out + 300, 260, "%s", name);
    if (size >= 580)
    {
        *(uint16_t *)(out + 576) = 1;   /* wUsagePage: generic desktop */
        *(uint16_t *)(out + 578) = 5;   /* wUsage: game pad */
    }
}

/* ------------------------------------------------------- force feedback */

enum { EFF_CONSTANT, EFF_RAMP, EFF_PERIODIC, EFF_SILENT };

/* in guest memory, as the game's IDirectInputEffect */
typedef struct {
    uint32_t lpVtbl, ref;
    uint32_t device;                    /* the di_device */
    uint8_t guid[16];
    int type;
    uint32_t duration, gain, start_delay;   /* µs, 0..10000, µs */
    int32_t magnitude, ramp_end;        /* constant: magnitude; ramp: start, end; periodic: magnitude, offset */
    uint32_t period;                    /* periodic, µs */
    int playing;
    uint32_t start, end;                /* the guest's ms; end 0: until stopped */
    int32_t kick;                       /* a steady force's last jolt, 0..10000, ... */
    uint32_t kick_time;                 /* ... from this ms */
} di_effect;

#define EFFECT_KICK_MS 250

/* a force that runs until stopped and pushes one way: a wheel's load */
static int effect_steady(const di_effect *e)
{
    return (e->type == EFF_CONSTANT || e->type == EFF_RAMP) && e->end == 0;
}

/* the effect GUIDs are 13541C2x-8E33-11D0-9AD0-00A0C9A06E35 */
static int effect_guid_number(const uint8_t *g)
{
    static const uint8_t tail[15] = { 0x1c, 0x54, 0x13, 0x33, 0x8e, 0xd0, 0x11, 0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35 };
    if (g == NULL || memcmp(g + 1, tail, 15) != 0 || g[0] < 0x20 || g[0] > 0x2b) return -1;
    return g[0] - 0x20;
}

static int effect_type(int number)
{
    if (number == 0) return EFF_CONSTANT;
    if (number == 1) return EFF_RAMP;
    if (number <= 6) return EFF_PERIODIC;
    return EFF_SILENT;
}

static const char *const effect_name[7] = { "Constant Force", "Ramp Force", "Square", "Sine", "Triangle", "Sawtooth Up", "Sawtooth Down" };

/* DIEFFECTINFO: dwSize, guid, dwEffType (DIEFT_CONSTANTFORCE 1, ..RAMPFORCE 2, ..PERIODIC 3), the
 * static and the dynamic parameters (DIEP_ALLPARAMS_DX5), tszName */
static void effect_fill_info(int number, uint8_t *out)
{
    static const uint8_t tail[15] = { 0x1c, 0x54, 0x13, 0x33, 0x8e, 0xd0, 0x11, 0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35 };
    uint32_t size = *(uint32_t *)out;

    if (size < 32 || size > 292) size = 292;
    memset(out, 0, size);
    *(uint32_t *)out = size;
    out[4] = (uint8_t)(0x20 + number);
    memcpy(out + 5, tail, 15);
    *(uint32_t *)(out + 20) = (number == 0) ? 1u : ((number == 1) ? 2u : 3u);
    *(uint32_t *)(out + 24) = 0x1ffu;
    *(uint32_t *)(out + 28) = 0x1ffu;
    if (size > 32) snprintf((char *)out + 32, size - 32, "%s", effect_name[number]);
}

/* the strength an effect asks for now, 0..10000 */
static int32_t effect_level(const di_effect *e, uint32_t now)
{
    int32_t level = e->magnitude;
    if (e->type == EFF_SILENT) return 0;
    if (e->type == EFF_RAMP && e->end != 0 && e->end != e->start)
    {
        double t = (double)(int32_t)(now - e->start) / (double)(e->end - e->start);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        level = (int32_t)(e->magnitude + (e->ramp_end - e->magnitude) * t);
    }
    else if (e->type == EFF_PERIODIC)
    {
        level = e->magnitude + ((e->ramp_end < 0) ? -e->ramp_end : e->ramp_end);
    }
    if (level < 0) level = -level;
    if (level > 10000) level = 10000;
    return (int32_t)((int64_t)level * e->gain / 10000);
}

/* the motors from the effects playing: called whenever they change and at each
 * poll of the device, as the effects end with time */
static void joy_rumble_update(struct joy_device *j)
{
    uint32_t now = Winapi_GetTicks();
    int32_t low = 0, high = 0;
    uint16_t want_low, want_high;
    int i, slot;

    for (i = 0; i < JOY_EFFECTS; i++)
    {
        di_effect *e;
        int32_t level;
        if (j->effects[i] == 0) continue;
        e = (di_effect *)GUEST_MEM(j->effects[i]);
        if (!e->playing) continue;
        if (e->end != 0 && (int32_t)(now - e->end) >= 0) { e->playing = 0; continue; }
        if ((int32_t)(now - e->start) < 0) continue;    /* the start delay */
        if (effect_steady(e))
        {
            uint32_t age = now - e->kick_time;
            level = (e->kick > 0 && age < EFFECT_KICK_MS) ? (int32_t)((int64_t)e->kick * (EFFECT_KICK_MS - age) / EFFECT_KICK_MS) : 0;
            if (level > low) low = level;
            continue;
        }
        level = effect_level(e, now);
        if (e->type == EFF_PERIODIC)
        {
            int32_t main = level, other = level / 2;
            if (e->period >= 50000) { if (main > low) low = main; if (other > high) high = other; }
            else { if (main > high) high = main; if (other > low) low = other; }
        }
        else if (level > low) low = level;
    }
    if (j->actuators_off || j->paused) low = high = 0;
    low = (int32_t)((int64_t)low * j->gain / 10000);
    high = (int32_t)((int64_t)high * j->gain / 10000);
    /* felt strength; steps of 1/64, so that a fading jolt is not sent at every poll */
    want_low = low > 0 ? (uint16_t)(((uint32_t)(sqrt(low / 10000.0) * 64.0 + 0.5) * 65535u) / 64u) : 0;
    want_high = high > 0 ? (uint16_t)(((uint32_t)(sqrt(high / 10000.0) * 64.0 + 0.5) * 65535u) / 64u) : 0;

    slot = joy_slot(j->index);
    if (slot < 0) { j->rumbling = 0; return; }
    if (want_low == j->rumble_low && want_high == j->rumble_high && (!j->rumbling || (now - j->rumble_sent) < 500)) return;
    /* a second at most, sent again while it goes on: a game that stops polling stops the motors */
    Game_PadRumble(slot, want_low, want_high, (want_low || want_high) ? 1000 : 0);
    if (joy_debug() && (want_low != j->rumble_low || want_high != j->rumble_high))
        fprintf(stderr, "[dinput joystick %d rumble %u %u]\n", j->index, want_low, want_high);
    j->rumble_low = want_low;
    j->rumble_high = want_high;
    j->rumbling = (want_low || want_high);
    j->rumble_sent = now;
}

static void joy_stop_effects(struct joy_device *j)
{
    int i;
    for (i = 0; i < JOY_EFFECTS; i++) if (j->effects[i]) ((di_effect *)GUEST_MEM(j->effects[i]))->playing = 0;
    joy_rumble_update(j);
}

/* ------------------------------------------------------------ DirectInput */

uint32_t CCALL DirectInputCreateA_c(uint32_t hinst, uint32_t version, uint32_t lplpDI, uint32_t punk)
{
    di_object *o;
    (void)hinst;
    if (lplpDI == 0 || punk != 0) return DIERR_INVALIDPARAM;
    o = (di_object *)x86_malloc(sizeof(*o));
    if (o == NULL) return DIERR_OUTOFMEMORY;
    o->lpVtbl = GUEST_ADDR(&IDirectInputVtbl_asm2c);
    o->ref = 1;
    o->version = version;
    *(uint32_t *)GUEST_MEM(lplpDI) = GUEST_ADDR(o);
    return DI_OK;
}

uint32_t CCALL IDirectInput_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1;
    if (a2) *(uint32_t *)GUEST_MEM(a2) = 0;
    return E_NOINTERFACE_;
}

uint32_t CCALL IDirectInput_AddRef_c(uint32_t a0)
{
    return ++((di_object *)GUEST_MEM(a0))->ref;
}

uint32_t CCALL IDirectInput_Release_c(uint32_t a0)
{
    di_object *o = (di_object *)GUEST_MEM(a0);
    return o->ref ? --o->ref : 0; /* the object is tiny, never freed */
}

uint32_t CCALL IDirectInput_CreateDevice_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    di_device *d;
    struct joy_device *joy = NULL;
    int kind, index;
    if (a1 == 0 || a2 == 0 || a3 != 0) return DIERR_INVALIDPARAM;
    kind = guid_kind((const uint8_t *)GUEST_MEM(a1));
    index = joy_guid_index((const uint8_t *)GUEST_MEM(a1));
    if (index >= 0)
    {
        int axis;
        if (joy_slot(index) < 0) return DIERR_DEVICENOTREG;
        kind = DEV_JOYSTICK;
        joy = (struct joy_device *)calloc(1, sizeof(*joy));
        if (joy == NULL) return DIERR_OUTOFMEMORY;
        joy->index = index;
        joy->version = ((di_object *)GUEST_MEM(a0))->version;
        for (axis = 0; axis < JOY_AXES; axis++)
        {
            joy->max[axis] = 65535;
            joy->saturation[axis] = 10000;
        }
        joy->gain = 10000;
    }
    if (kind == DEV_OTHER) return DIERR_UNSUPPORTED;
    d = (di_device *)x86_malloc(sizeof(*d));
    if (d == NULL) { free(joy); return DIERR_OUTOFMEMORY; }
    d->lpVtbl = GUEST_ADDR(&IDirectInputDeviceVtbl_asm2c);
    d->ref = 1;
    d->kind = kind;
    d->joy = joy;
    if (kind == DEV_KEYBOARD) key_queue_init();
    if (kind == DEV_MOUSE) mouse_init();
    if (input_debug()) fprintf(stderr, "[dinput CreateDevice kind=%d -> %08x]\n", kind, GUEST_ADDR(d));
    *(uint32_t *)GUEST_MEM(a2) = GUEST_ADDR(d);
    return DI_OK;
}

/* (this, dwDevType, lpCallback, pvRef, dwFlags): the joysticks only, for 0
 * (all) and DIDEVTYPE_JOYSTICK; each pad has force feedback (DIEDFL_FORCEFEEDBACK) */
uint32_t CCALL IDirectInput_EnumDevices_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t size = (((di_object *)GUEST_MEM(a0))->version >= 0x500) ? 580 : 560;
    uint8_t *instance;
    int index, count = Game_PadCount();

    if (joy_debug()) fprintf(stderr, "[dinput EnumDevices type=%x flags=%x: %d pads]\n", a1, a4, count);
    if (a2 == 0) return DIERR_INVALIDPARAM;
    if ((a1 & 0xFFu) != 0 && (a1 & 0xFFu) != 4) return DI_OK;
    instance = (uint8_t *)x86_malloc(size);
    if (instance == NULL) return DIERR_OUTOFMEMORY;
    for (index = 0; index < count && index < GAME_PAD_MAX; index++)
    {
        uint32_t args[2];
        joy_fill_instance(index, instance, size);
        args[0] = GUEST_ADDR(instance);
        args[1] = a3;
        if (Winapi_CallGuest(a2, 2, args) == 0) break;     /* DIENUM_STOP */
    }
    x86_free(instance);
    return DI_OK;
}

uint32_t CCALL IDirectInput_GetDeviceStatus_c(uint32_t a0, uint32_t a1)
{
    int index = a1 ? joy_guid_index((const uint8_t *)GUEST_MEM(a1)) : -1;
    (void)a0;
    if (index >= 0 && joy_slot(index) < 0) return DI_NOEFFECT;     /* DI_NOTATTACHED */
    return DI_OK;
}
uint32_t CCALL IDirectInput_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }
uint32_t CCALL IDirectInput_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }

/* ----------------------------------------------------------------- device */

/* IUnknown, IDirectInputDeviceA (5944E680-C92E-11CF-BFC7-444553540000) and
 * IDirectInputDevice2A (..E682): the device itself, which has the vtable of the
 * second */
uint32_t CCALL IDirectInputDevice_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    static const uint8_t unknown[16] = { 0,0,0,0, 0,0, 0,0, 0xc0,0,0,0,0,0,0,0x46 };
    static const uint8_t device[16] = { 0x80,0xe6,0x44,0x59, 0x2e,0xc9, 0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
    const uint8_t *iid = a1 ? (const uint8_t *)GUEST_MEM(a1) : NULL;
    int known = 0;

    if (a2 == 0) return DIERR_INVALIDPARAM;
    if (iid != NULL)
        known = (memcmp(iid, unknown, 16) == 0) ||
                ((iid[0] == 0x80 || iid[0] == 0x82) && memcmp(iid + 1, device + 1, 15) == 0);
    if (input_debug()) fprintf(stderr, "[dinput %08x QueryInterface %02x -> %s]\n", a0, iid ? iid[0] : 0, known ? "yes" : "no");
    if (!known)
    {
        *(uint32_t *)GUEST_MEM(a2) = 0;
        return E_NOINTERFACE_;
    }
    ((di_device *)GUEST_MEM(a0))->ref++;
    *(uint32_t *)GUEST_MEM(a2) = a0;
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_AddRef_c(uint32_t a0)
{
    return ++((di_device *)GUEST_MEM(a0))->ref;
}

uint32_t CCALL IDirectInputDevice_Release_c(uint32_t a0)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    if (d->ref == 0) return 0;
    if (--d->ref == 0 && d->joy != NULL) joy_stop_effects(d->joy);    /* the object stays, as the others */
    return d->ref;
}

/* DIDEVCAPS: dwSize (24, or 44 with the force feedback's), dwFlags, dwDevType,
 * dwAxes, dwButtons, dwPOVs, ... */
uint32_t CCALL IDirectInputDevice_GetCapabilities_c(uint32_t a0, uint32_t a1)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint32_t *caps = (uint32_t *)GUEST_MEM(a1), size;

    if (d->kind != DEV_JOYSTICK) return DIERR_UNSUPPORTED;
    if (caps == NULL || caps[0] < 24) return DIERR_INVALIDPARAM;
    size = (caps[0] >= 44) ? 44 : 24;
    memset(caps + 1, 0, size - 4);
    caps[1] = 0x100u | ((joy_slot(d->joy->index) >= 0) ? 1u : 0u);  /* DIDC_FORCEFEEDBACK, DIDC_ATTACHED */
    caps[2] = JOY_DEVTYPE;
    caps[3] = JOY_AXES;
    caps[4] = JOY_BUTTONS;
    caps[5] = 1;
    if (size >= 44)
    {
        caps[6] = 10000;    /* dwFFSamplePeriod, µs */
        caps[7] = 10000;    /* dwFFMinTimeResolution */
        caps[10] = 0x500;   /* dwFFDriverVersion */
    }
    if (joy_debug()) fprintf(stderr, "[dinput joystick %d GetCapabilities size=%u]\n", d->joy->index, caps[0]);
    return DI_OK;
}

/* (this, lpCallback, pvRef, dwFlags): DIDFT_ALL or the types asked for */
uint32_t CCALL IDirectInputDevice_EnumObjects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint32_t size;
    uint8_t *instance;
    int object;

    if (d->kind != DEV_JOYSTICK) return DI_OK;
    if (a1 == 0) return DIERR_INVALIDPARAM;
    if (joy_debug()) fprintf(stderr, "[dinput joystick %d EnumObjects flags=%x]\n", d->joy->index, a3);
    size = (d->joy->version >= 0x500) ? 316 : 292;
    instance = (uint8_t *)x86_malloc(size);
    if (instance == NULL) return DIERR_OUTOFMEMORY;
    for (object = 0; object < JOY_OBJECTS; object++)
    {
        uint32_t type = joy_object_type(object), args[2];
        if ((a3 & 0xFFu) && !(a3 & type & 0xFFu)) continue;
        if ((a3 & DIDFT_FFACTUATOR) && !(type & DIDFT_FFACTUATOR)) continue;
        joy_fill_object(d->joy, object, instance, size);
        args[0] = GUEST_ADDR(instance);
        args[1] = a2;
        if (Winapi_CallGuest(a1, 2, args) == 0) break;
    }
    x86_free(instance);
    return DI_OK;
}

/* the property (DIPROP_*: a number, not a GUID) of the axes a DIPROPHEADER
 * names: dwSize, dwHeaderSize, dwObj, dwHow, then the data */
static int joy_property_axes(const struct joy_device *j, const uint32_t *header, int *first, int *last)
{
    int object;
    if (header[3] == DIPH_DEVICE)
    {
        *first = 0;
        *last = JOY_AXES - 1;
        return 1;
    }
    object = joy_find_object(j, header[2], header[3]);
    if (object == JOY_NONE || object >= JOY_AXES) return 0;
    *first = *last = object;
    return 1;
}

uint32_t CCALL IDirectInputDevice_GetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint32_t *header = (uint32_t *)GUEST_MEM(a2);
    struct joy_device *j = d->joy;
    int first, last;

    if (d->kind != DEV_JOYSTICK) return DIERR_UNSUPPORTED;
    if (header == NULL) return DIERR_INVALIDPARAM;
    switch (a1)
    {
        case DIPROP_BUFFERSIZE: header[4] = j->buffer_size; return DI_OK;
        case DIPROP_FFGAIN: header[4] = j->gain; return DI_OK;
        case DIPROP_FFLOAD: header[4] = 0; return DI_OK;
        case DIPROP_GRANULARITY: header[4] = 1; return DI_OK;
        case DIPROP_RANGE:
        case DIPROP_DEADZONE:
        case DIPROP_SATURATION:
            if (header[3] == DIPH_DEVICE || !joy_property_axes(j, header, &first, &last)) return DIERR_NOTFOUND;
            if (a1 == DIPROP_RANGE) { header[4] = (uint32_t)j->min[first]; header[5] = (uint32_t)j->max[first]; }
            else header[4] = (a1 == DIPROP_DEADZONE) ? j->deadzone[first] : j->saturation[first];
            return DI_OK;
        default:
            return DIERR_UNSUPPORTED;
    }
}

uint32_t CCALL IDirectInputDevice_SetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    const uint32_t *header = (const uint32_t *)GUEST_MEM(a2);
    struct joy_device *j = d->joy;
    int first, last, axis;

    if (input_debug() || (d->kind == DEV_JOYSTICK && joy_debug()))
        fprintf(stderr, "[dinput %08x SetProperty %u obj=%x how=%u %d %d]\n", a0, a1, header ? header[2] : 0,
                header ? header[3] : 0, header ? (int32_t)header[4] : 0, header ? (int32_t)header[5] : 0);
    if (d->kind != DEV_JOYSTICK) return DI_OK;
    if (header == NULL) return DIERR_INVALIDPARAM;
    switch (a1)
    {
        case DIPROP_BUFFERSIZE: j->buffer_size = header[4]; return DI_OK;
        case DIPROP_FFGAIN:
            j->gain = (header[4] > 10000) ? 10000 : header[4];
            joy_rumble_update(j);
            return DI_OK;
        case DIPROP_RANGE:
        case DIPROP_DEADZONE:
        case DIPROP_SATURATION:
            if (!joy_property_axes(j, header, &first, &last)) return DIERR_NOTFOUND;
            for (axis = first; axis <= last; axis++)
            {
                if (a1 == DIPROP_RANGE)
                {
                    if ((int32_t)header[4] >= (int32_t)header[5]) return DIERR_INVALIDPARAM;
                    j->min[axis] = (int32_t)header[4];
                    j->max[axis] = (int32_t)header[5];
                }
                else if (header[4] > 10000) return DIERR_INVALIDPARAM;
                else if (a1 == DIPROP_DEADZONE) j->deadzone[axis] = header[4];
                else j->saturation[axis] = header[4];
            }
            return DI_OK;
        default:
            return DI_OK;   /* DIPROP_AXISMODE, ..AUTOCENTER, ... */
    }
}

uint32_t CCALL IDirectInputDevice_Acquire_c(uint32_t a0)
{
    if (input_debug()) fprintf(stderr, "[dinput %08x Acquire]\n", a0);
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    if ((((di_device *)GUEST_MEM(a0))->kind == DEV_MOUSE) && mouse_hold_given) mouse_hold_wanted = 1;
#endif
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_Unacquire_c(uint32_t a0)
{
    if (input_debug()) fprintf(stderr, "[dinput %08x Unacquire]\n", a0);
    if (((di_device *)GUEST_MEM(a0))->joy != NULL) joy_stop_effects(((di_device *)GUEST_MEM(a0))->joy);
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    if (((di_device *)GUEST_MEM(a0))->kind == DEV_MOUSE) { mouse_hold_wanted = 0; mouse_apply_hold(); }
#endif
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_GetDeviceState_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint8_t *out = (uint8_t *)GUEST_MEM(a2);
    if (out == NULL) return DIERR_INVALIDPARAM;
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    if (d->kind == DEV_MOUSE) mouse_apply_hold();
#endif
    if (d->kind == DEV_JOYSTICK)
    {
        if (d->joy->map == NULL) return DIERR_INVALIDPARAM;     /* no SetDataFormat */
        joy_fill_state(d->joy, out, a1);
        joy_rumble_update(d->joy);
        return DI_OK;
    }
    memset(out, 0, a1);

    if (d->kind == DEV_KEYBOARD)
    {
        int n = 0, sc;
        SDL_Keycode held;
        const uint8_t *keys = SDL_GetKeyboardState(&n);
        if (a1 < 256) return DIERR_INVALIDPARAM;
        /* the keys are the port's panel's while it is open */
        if (Game_OsdOpen()) return DI_OK;
        for (sc = 0; sc < n; sc++)
            if ((keys[sc] && !Game_OsdKeyHidden((SDL_Scancode)sc)) || Game_AutoKeyDown((SDL_Scancode)sc)) { uint8_t dik = Game_PcScancode((SDL_Scancode)sc); if (dik) out[dik] = 0x80; }
        for (sc = 0; TouchGamepad_HeldKey(sc, &held); sc++)
        {
            uint8_t dik = Game_PcScancode(SDL_GetScancodeFromKey(held));
            if (dik) out[dik] = 0x80;
        }
    }
    else if (d->kind == DEV_MOUSE)
    {
        int32_t *axes = (int32_t *)out; /* DIMOUSESTATE: lX, lY, lZ, rgbButtons[4] */
        if (a1 < 16) return DIERR_INVALIDPARAM;
        SDL_LockMutex(mouse_mutex);
        touchpad_end_click();
        axes[0] = mouse_dx;
        axes[1] = mouse_dy;
        axes[2] = mouse_dz;
        memcpy(out + 12, mouse_buttons_down, 4);
        mouse_dx = mouse_dy = mouse_dz = 0;
        SDL_UnlockMutex(mouse_mutex);
        if (input_debug() && (axes[0] || axes[1] || axes[2] || out[12] || out[13]))
            fprintf(stderr, "[dinput mouse state %d %d %d %02x %02x]\n", axes[0], axes[1], axes[2], out[12], out[13]);
    }
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_GetDeviceData_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint32_t *count, capacity, n = 0;
    unsigned tail;
    if (a3 == 0) return DIERR_INVALIDPARAM;
#ifdef GAME_DINPUT_EXCLUSIVE_MOUSE
    if (d->kind == DEV_MOUSE) mouse_apply_hold();
#endif
    count = (uint32_t *)GUEST_MEM(a3);
    capacity = (a2 != 0) ? *count : 0;
    if (d->kind == DEV_KEYBOARD && key_mutex != NULL)
    {
        SDL_LockMutex(key_mutex);
        tail = key_tail;
        while (n < capacity && tail != key_head)
        {
            uint32_t *out = (uint32_t *)((uint8_t *)GUEST_MEM(a2) + (size_t)n * a1);
            out[0] = key_queue[tail].dik;
            out[1] = key_queue[tail].down ? 0x80u : 0u;
            out[2] = key_queue[tail].time;
            out[3] = ++key_sequence;
            if (input_debug()) fprintf(stderr, "[dinput key data dik=%02x %s]\n", out[0], out[1] ? "down" : "up");
            tail = (tail + 1) % KEY_QUEUE_SIZE;
            n++;
        }
        if ((a4 & 1u) == 0) key_tail = tail; /* DIGDD_PEEK leaves the data queued */
        SDL_UnlockMutex(key_mutex);
    }
    else if (d->kind == DEV_MOUSE && mouse_mutex != NULL)
    {
        SDL_LockMutex(mouse_mutex);
        touchpad_end_click();
        tail = mouse_tail;
        while (n < capacity && tail != mouse_head)
        {
            uint32_t *out = (uint32_t *)((uint8_t *)GUEST_MEM(a2) + (size_t)n * a1);
            out[0] = mouse_queue[tail].ofs;
            out[1] = mouse_queue[tail].data;
            out[2] = mouse_queue[tail].time;
            out[3] = ++key_sequence;
            if (input_debug()) fprintf(stderr, "[dinput mouse data ofs=%u data=%d]\n", out[0], (int32_t)out[1]);
            tail = (tail + 1) % MOUSE_QUEUE_SIZE;
            n++;
        }
        if ((a4 & 1u) == 0) mouse_tail = tail;
        SDL_UnlockMutex(mouse_mutex);
    }
    *count = n;
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_SetDataFormat_c(uint32_t a0, uint32_t a1)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    if (d->kind != DEV_JOYSTICK) return DI_OK;
    return joy_set_format(d->joy, a1);
}
uint32_t CCALL IDirectInputDevice_SetEventNotification_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DI_OK; }
uint32_t CCALL IDirectInputDevice_SetCooperativeLevel_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a1;
    if (input_debug()) fprintf(stderr, "[dinput %08x SetCooperativeLevel %x]\n", a0, a2);
    return DI_OK;
}
uint32_t CCALL IDirectInputDevice_GetObjectInfo_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint8_t *out = (uint8_t *)GUEST_MEM(a1);
    int object;

    if (d->kind != DEV_JOYSTICK) return DIERR_UNSUPPORTED;
    if (out == NULL || *(uint32_t *)out < 292) return DIERR_INVALIDPARAM;
    object = joy_find_object(d->joy, a2, a3);
    if (object == JOY_NONE) return DIERR_NOTFOUND;
    joy_fill_object(d->joy, object, out, (*(uint32_t *)out >= 316) ? 316 : 292);
    return DI_OK;
}

/* DIDEVICEINSTANCE (DirectInput 3): dwSize, guidInstance, guidProduct, dwDevType,
 * tszInstanceName[260], tszProductName[260] */
uint32_t CCALL IDirectInputDevice_GetDeviceInfo_c(uint32_t a0, uint32_t a1)
{
    static const uint8_t sys_mouse[16]    = { 0x60,0x2b,0x1d,0x6f, 0xa0,0xd5, 0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
    static const uint8_t sys_keyboard[16] = { 0x61,0x2b,0x1d,0x6f, 0xa0,0xd5, 0xcf,0x11, 0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00 };
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint8_t *out = (uint8_t *)GUEST_MEM(a1);
    const uint8_t *guid;
    const char *name;
    uint32_t size, devtype;

    if (out == NULL) return DIERR_INVALIDPARAM;
    size = *(uint32_t *)out;
    if (size < 40) return DIERR_INVALIDPARAM;
    if (d->kind == DEV_JOYSTICK)
    {
        if (size < 560) return DIERR_INVALIDPARAM;
        joy_fill_instance(d->joy->index, out, (size >= 580) ? 580 : 560);
        return DI_OK;
    }
    if (size > 560) size = 560;
    if (d->kind == DEV_KEYBOARD) { guid = sys_keyboard; name = "Keyboard"; devtype = 3; }
    else if (d->kind == DEV_MOUSE) { guid = sys_mouse; name = "Mouse"; devtype = 2; }
    else { guid = sys_mouse; name = "Device"; devtype = 0; }

    memset(out, 0, size);
    *(uint32_t *)out = size;
    memcpy(out + 4, guid, 16);      /* guidInstance */
    memcpy(out + 20, guid, 16);     /* guidProduct */
    *(uint32_t *)(out + 36) = devtype;
    if (size > 40) strncpy((char *)out + 40, name, (size - 40 < 260 ? size - 40 : 260) - 1);
    if (size > 300) strncpy((char *)out + 300, name, (size - 300 < 260 ? size - 300 : 260) - 1);
    if (input_debug()) fprintf(stderr, "[dinput %08x GetDeviceInfo size=%u devtype=%u]\n", a0, size, devtype);
    return DI_OK;
}
uint32_t CCALL IDirectInputDevice_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }
uint32_t CCALL IDirectInputDevice_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { (void)a0; (void)a1; (void)a2; (void)a3; return DI_OK; }

/* ------------------------------------------- IDirectInputDevice2 (DirectX 5) */

static struct joy_device *device_joy(uint32_t device)
{
    di_device *d = (di_device *)GUEST_MEM(device);
    return (d->kind == DEV_JOYSTICK) ? d->joy : NULL;
}

/* DIEFFECT: dwSize, dwFlags, dwDuration, dwSamplePeriod, dwGain, dwTriggerButton,
 * dwTriggerRepeatInterval, cAxes, rgdwAxes, rglDirection, lpEnvelope,
 * cbTypeSpecificParams, lpvTypeSpecificParams, dwStartDelay (DirectX 6) */
#define DIEP_DURATION 0x001u
#define DIEP_GAIN 0x004u
#define DIEP_TYPESPECIFICPARAMS 0x100u
#define DIEP_STARTDELAY 0x200u
#define DIEP_START 0x20000000u
#define DIES_SOLO 1u

static uint32_t effect_start(di_effect *e, uint32_t iterations, uint32_t flags);

/* a steady force that changes by a tenth of the range or more jolts the pad */
static void effect_kick(di_effect *e, int32_t before)
{
    uint32_t now = Winapi_GetTicks();
    int32_t change = effect_level(e, now) - before, left = 0;

    if (change < 0) change = -change;
    if (change < 1000) return;
    if (e->kick > 0 && now - e->kick_time < EFFECT_KICK_MS)
        left = (int32_t)((int64_t)e->kick * (EFFECT_KICK_MS - (now - e->kick_time)) / EFFECT_KICK_MS);
    e->kick = (change > left) ? change : left;
    e->kick_time = now;
    if (joy_debug()) fprintf(stderr, "[dinput effect %02x jolt %d]\n", e->guid[0], e->kick);
}

static uint32_t effect_set(di_effect *e, uint32_t params, uint32_t flags)
{
    const uint32_t *p = (const uint32_t *)GUEST_MEM(params);
    int32_t before = effect_level(e, Winapi_GetTicks());

    if (p == NULL || p[0] < 52) return DIERR_INVALIDPARAM;
    if (flags & DIEP_DURATION) e->duration = p[2];
    if (flags & DIEP_GAIN) e->gain = (p[4] > 10000) ? 10000 : p[4];
    if ((flags & DIEP_STARTDELAY) && p[0] >= 56) e->start_delay = p[13];
    if ((flags & DIEP_TYPESPECIFICPARAMS) && p[12] != 0)
    {
        const int32_t *t = (const int32_t *)GUEST_MEM(p[12]);
        uint32_t size = p[11];
        if (e->type == EFF_CONSTANT && size >= 4) e->magnitude = t[0];
        else if (e->type == EFF_RAMP && size >= 8) { e->magnitude = t[0]; e->ramp_end = t[1]; }
        else if (e->type == EFF_PERIODIC && size >= 16) { e->magnitude = t[0]; e->ramp_end = t[1]; e->period = (uint32_t)t[3]; }
    }
    if (e->playing && effect_steady(e)) effect_kick(e, before);
    if (flags & DIEP_START) return effect_start(e, 1, 0);
    if (device_joy(e->device) != NULL) joy_rumble_update(device_joy(e->device));
    return DI_OK;
}

static uint32_t effect_start(di_effect *e, uint32_t iterations, uint32_t flags)
{
    struct joy_device *j = device_joy(e->device);
    uint32_t now = Winapi_GetTicks(), delay = e->start_delay / 1000;
    int i, steady_before = e->playing && effect_steady(e);

    if (j == NULL) return DIERR_NOTINITIALIZED;
    if (flags & DIES_SOLO)
        for (i = 0; i < JOY_EFFECTS; i++) if (j->effects[i]) ((di_effect *)GUEST_MEM(j->effects[i]))->playing = 0;
    e->playing = 1;
    e->start = now + delay;
    if (e->duration == 0xFFFFFFFFu || iterations == 0xFFFFFFFFu) e->end = 0;   /* INFINITE */
    else
    {
        uint64_t ms = (uint64_t)e->duration * (iterations ? iterations : 1) / 1000;
        e->end = e->start + (uint32_t)((ms > 0x7FFFFFFF) ? 0x7FFFFFFF : (ms ? ms : 1));
        if (e->end == 0) e->end = 1;
    }
    if (effect_steady(e) && !steady_before) { e->kick = 0; effect_kick(e, 0); }   /* (re)started: from nothing */
    if (joy_debug())
        fprintf(stderr, "[dinput joystick %d effect %02x start: magnitude %d..%d period %u gain %u, %s %u ms]\n", j->index, e->guid[0],
                e->magnitude, e->ramp_end, e->period, e->gain, e->end ? "for" : "until stopped", e->end ? e->end - e->start : 0);
    joy_rumble_update(j);
    return DI_OK;
}

/* (this, rguid, lpeff, ppdeff, punkOuter) */
uint32_t CCALL IDirectInputDevice2_CreateEffect_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    struct joy_device *j = device_joy(a0);
    const uint8_t *guid = a1 ? (const uint8_t *)GUEST_MEM(a1) : NULL;
    int number = effect_guid_number(guid), slot;
    di_effect *e;

    if (a3 == 0 || a4 != 0) return DIERR_INVALIDPARAM;
    *(uint32_t *)GUEST_MEM(a3) = 0;
    if (j == NULL) return DIERR_UNSUPPORTED;
    if (number < 0) return DIERR_DEVICENOTREG;
    for (slot = 0; slot < JOY_EFFECTS && j->effects[slot] != 0; slot++) {}
    if (slot == JOY_EFFECTS) return DIERR_OUTOFMEMORY;
    e = (di_effect *)x86_malloc(sizeof(*e));
    if (e == NULL) return DIERR_OUTOFMEMORY;
    memset(e, 0, sizeof(*e));
    e->lpVtbl = GUEST_ADDR(&IDirectInputEffectVtbl_asm2c);
    e->ref = 1;
    e->device = a0;
    memcpy(e->guid, guid, 16);
    e->type = effect_type(number);
    e->gain = 10000;
    e->duration = 0xFFFFFFFFu;
    j->effects[slot] = GUEST_ADDR(e);
    if (joy_debug()) fprintf(stderr, "[dinput joystick %d CreateEffect %02x -> %08x]\n", j->index, guid[0], GUEST_ADDR(e));
    if (a2 != 0)
    {
        uint32_t result = effect_set(e, a2, 0x3FFu);
        if (result != DI_OK && result != DI_NOEFFECT) { j->effects[slot] = 0; x86_free(e); return result; }
    }
    ((di_device *)GUEST_MEM(a0))->ref++;   /* the effect holds its device */
    *(uint32_t *)GUEST_MEM(a3) = GUEST_ADDR(e);
    return DI_OK;
}

/* (this, lpCallback, pvRef, dwEffType) */
uint32_t CCALL IDirectInputDevice2_EnumEffects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint8_t *info;
    int number;

    if (device_joy(a0) == NULL) return DI_OK;
    if (a1 == 0) return DIERR_INVALIDPARAM;
    info = (uint8_t *)x86_malloc(292);
    if (info == NULL) return DIERR_OUTOFMEMORY;
    for (number = 0; number < 7; number++)
    {
        uint32_t args[2], kind = (number == 0) ? 1u : ((number == 1) ? 2u : 3u);
        if ((a3 & 0xFFu) != 0 && (a3 & 0xFFu) != kind) continue;
        *(uint32_t *)info = 292;
        effect_fill_info(number, info);
        args[0] = GUEST_ADDR(info);
        args[1] = a2;
        if (Winapi_CallGuest(a1, 2, args) == 0) break;
    }
    x86_free(info);
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice2_GetEffectInfo_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    int number = a2 ? effect_guid_number((const uint8_t *)GUEST_MEM(a2)) : -1;
    if (device_joy(a0) == NULL) return DIERR_UNSUPPORTED;
    if (a1 == 0) return DIERR_INVALIDPARAM;
    if (number < 0 || number >= 7) return DIERR_DEVICENOTREG;
    effect_fill_info(number, (uint8_t *)GUEST_MEM(a1));
    return DI_OK;
}

/* DIGFFS_EMPTY 1, ..STOPPED 2, ..PAUSED 4, ..ACTUATORSON 0x10, ..OFF 0x20, ..POWERON 0x40 */
uint32_t CCALL IDirectInputDevice2_GetForceFeedbackState_c(uint32_t a0, uint32_t a1)
{
    struct joy_device *j = device_joy(a0);
    uint32_t state = 0x40u;
    int i, any = 0, playing = 0;

    if (j == NULL) return DIERR_UNSUPPORTED;
    if (a1 == 0) return DIERR_INVALIDPARAM;
    joy_rumble_update(j);
    for (i = 0; i < JOY_EFFECTS; i++)
    {
        if (j->effects[i] == 0) continue;
        any = 1;
        if (((di_effect *)GUEST_MEM(j->effects[i]))->playing) playing = 1;
    }
    if (!any) state |= 1u;
    if (!playing) state |= 2u;
    if (j->paused) state |= 4u;
    state |= j->actuators_off ? 0x20u : 0x10u;
    *(uint32_t *)GUEST_MEM(a1) = state;
    return DI_OK;
}

/* DISFFC_RESET 1, ..STOPALL 2, ..PAUSE 4, ..CONTINUE 8, ..SETACTUATORSON 0x10, ..OFF 0x20 */
uint32_t CCALL IDirectInputDevice2_SendForceFeedbackCommand_c(uint32_t a0, uint32_t a1)
{
    struct joy_device *j = device_joy(a0);
    if (j == NULL) return DIERR_UNSUPPORTED;
    if (joy_debug()) fprintf(stderr, "[dinput joystick %d SendForceFeedbackCommand %x]\n", j->index, a1);
    if (a1 & 4u) j->paused = 1;
    if (a1 & 8u) j->paused = 0;
    if (a1 & 0x10u) j->actuators_off = 0;
    if (a1 & 0x20u) j->actuators_off = 1;
    if (a1 & 3u)
    {
        j->paused = 0;
        joy_stop_effects(j);
    }
    else joy_rumble_update(j);
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice2_EnumCreatedEffectObjects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    struct joy_device *j = device_joy(a0);
    uint32_t list[JOY_EFFECTS];
    int i, n = 0;
    (void)a3;
    if (j == NULL) return DI_OK;
    if (a1 == 0) return DIERR_INVALIDPARAM;
    for (i = 0; i < JOY_EFFECTS; i++) if (j->effects[i]) list[n++] = j->effects[i];
    for (i = 0; i < n; i++)
    {
        uint32_t args[2];
        args[0] = list[i];
        args[1] = a2;
        if (Winapi_CallGuest(a1, 2, args) == 0) break;
    }
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice2_Escape_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DIERR_UNSUPPORTED; }

uint32_t CCALL IDirectInputDevice2_Poll_c(uint32_t a0)
{
    struct joy_device *j = device_joy(a0);
    if (j == NULL) return DI_NOEFFECT;  /* not a polled device */
    joy_rumble_update(j);
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice2_SendDeviceData_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a4;
    if (a3) *(uint32_t *)GUEST_MEM(a3) = 0;
    return DIERR_UNSUPPORTED;
}

/* -------------------------------------------------------- IDirectInputEffect */

uint32_t CCALL IDirectInputEffect_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    static const uint8_t unknown[16] = { 0,0,0,0, 0,0, 0,0, 0xc0,0,0,0,0,0,0,0x46 };
    static const uint8_t effect[16] = { 0xc0,0xf7,0xe1,0xe7, 0xd2,0x88, 0xd0,0x11, 0x9a,0xd0,0x00,0xa0,0xc9,0xa0,0x6e,0x35 };
    const uint8_t *iid = a1 ? (const uint8_t *)GUEST_MEM(a1) : NULL;
    if (a2 == 0) return DIERR_INVALIDPARAM;
    if (iid == NULL || (memcmp(iid, unknown, 16) != 0 && memcmp(iid, effect, 16) != 0))
    {
        *(uint32_t *)GUEST_MEM(a2) = 0;
        return E_NOINTERFACE_;
    }
    ((di_effect *)GUEST_MEM(a0))->ref++;
    *(uint32_t *)GUEST_MEM(a2) = a0;
    return DI_OK;
}

uint32_t CCALL IDirectInputEffect_AddRef_c(uint32_t a0)
{
    return ++((di_effect *)GUEST_MEM(a0))->ref;
}

uint32_t CCALL IDirectInputEffect_Release_c(uint32_t a0)
{
    di_effect *e = (di_effect *)GUEST_MEM(a0);
    struct joy_device *j;
    int i;

    if (e->ref == 0) return 0;
    if (--e->ref != 0) return e->ref;
    j = device_joy(e->device);
    e->playing = 0;
    if (j != NULL)
    {
        for (i = 0; i < JOY_EFFECTS; i++) if (j->effects[i] == a0) j->effects[i] = 0;
        joy_rumble_update(j);
    }
    IDirectInputDevice_Release_c(e->device);
    x86_free(e);
    return 0;
}

uint32_t CCALL IDirectInputEffect_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { (void)a0; (void)a1; (void)a2; (void)a3; return DI_OK; }

uint32_t CCALL IDirectInputEffect_GetEffectGuid_c(uint32_t a0, uint32_t a1)
{
    if (a1 == 0) return DIERR_INVALIDPARAM;
    memcpy(GUEST_MEM(a1), ((di_effect *)GUEST_MEM(a0))->guid, 16);
    return DI_OK;
}

uint32_t CCALL IDirectInputEffect_GetParameters_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    di_effect *e = (di_effect *)GUEST_MEM(a0);
    uint32_t *p = (uint32_t *)GUEST_MEM(a1);

    if (p == NULL || p[0] < 52) return DIERR_INVALIDPARAM;
    if (a2 & DIEP_DURATION) p[2] = e->duration;
    if (a2 & DIEP_GAIN) p[4] = e->gain;
    if ((a2 & DIEP_STARTDELAY) && p[0] >= 56) p[13] = e->start_delay;
    if ((a2 & DIEP_TYPESPECIFICPARAMS) && p[12] != 0)
    {
        int32_t *t = (int32_t *)GUEST_MEM(p[12]);
        if (e->type == EFF_CONSTANT && p[11] >= 4) t[0] = e->magnitude;
        else if ((e->type == EFF_RAMP && p[11] >= 8) || (e->type == EFF_PERIODIC && p[11] >= 16)) { t[0] = e->magnitude; t[1] = e->ramp_end; }
    }
    return DI_OK;
}

uint32_t CCALL IDirectInputEffect_SetParameters_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return effect_set((di_effect *)GUEST_MEM(a0), a1, a2);
}

uint32_t CCALL IDirectInputEffect_Start_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return effect_start((di_effect *)GUEST_MEM(a0), a1, a2);
}

uint32_t CCALL IDirectInputEffect_Stop_c(uint32_t a0)
{
    di_effect *e = (di_effect *)GUEST_MEM(a0);
    e->playing = 0;
    if (device_joy(e->device) != NULL) joy_rumble_update(device_joy(e->device));
    return DI_OK;
}

/* DIEGES_PLAYING 1 */
uint32_t CCALL IDirectInputEffect_GetEffectStatus_c(uint32_t a0, uint32_t a1)
{
    di_effect *e = (di_effect *)GUEST_MEM(a0);
    if (a1 == 0) return DIERR_INVALIDPARAM;
    if (device_joy(e->device) != NULL) joy_rumble_update(device_joy(e->device));
    *(uint32_t *)GUEST_MEM(a1) = e->playing ? 1u : 0u;
    return DI_OK;
}

uint32_t CCALL IDirectInputEffect_Download_c(uint32_t a0) { (void)a0; return DI_OK; }
uint32_t CCALL IDirectInputEffect_Unload_c(uint32_t a0) { return IDirectInputEffect_Stop_c(a0); }
uint32_t CCALL IDirectInputEffect_Escape_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DIERR_UNSUPPORTED; }

/* Whether the DirectInput keyboard device has been created (key_mutex != NULL):
 * the program's main loop skips the SDL keyboard events then, as DirectInput
 * owns the keyboard. */
uint32_t CCALL winapi_is_dinput_active(void)
{
    return (key_mutex != NULL) ? 1 : 0;
}
