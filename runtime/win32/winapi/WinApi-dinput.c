/*
 *  DirectInput 3 (dinput.dll) for the recompiled game: the
 *  keyboard and the mouse, read from SDL. The COM vtables are llasm bridges
 *  (llasm/WinApi-dinput-asm.llasm); the objects live in guest memory.
 *  No joystick: EnumDevices reports nothing.
 */

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

enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2, DEV_OTHER = 3 };

typedef struct { uint32_t lpVtbl; uint32_t ref; } di_object;
typedef struct { uint32_t lpVtbl; uint32_t ref; int kind; } di_device;

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
    SDL_SetRelativeMouseMode(mouse_held ? SDL_TRUE : SDL_FALSE);
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

/* ------------------------------------------------------------ DirectInput */

uint32_t CCALL DirectInputCreateA_c(uint32_t hinst, uint32_t version, uint32_t lplpDI, uint32_t punk)
{
    di_object *o;
    (void)hinst; (void)version;
    if (lplpDI == 0 || punk != 0) return DIERR_INVALIDPARAM;
    o = (di_object *)x86_malloc(sizeof(*o));
    if (o == NULL) return DIERR_OUTOFMEMORY;
    o->lpVtbl = GUEST_ADDR(&IDirectInputVtbl_asm2c);
    o->ref = 1;
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
    int kind;
    (void)a0;
    if (a1 == 0 || a2 == 0 || a3 != 0) return DIERR_INVALIDPARAM;
    kind = guid_kind((const uint8_t *)GUEST_MEM(a1));
    if (kind == DEV_OTHER) return DIERR_UNSUPPORTED;
    d = (di_device *)x86_malloc(sizeof(*d));
    if (d == NULL) return DIERR_OUTOFMEMORY;
    d->lpVtbl = GUEST_ADDR(&IDirectInputDeviceVtbl_asm2c);
    d->ref = 1;
    d->kind = kind;
    if (kind == DEV_KEYBOARD) key_queue_init();
    if (kind == DEV_MOUSE) mouse_init();
    if (input_debug()) fprintf(stderr, "[dinput CreateDevice kind=%d -> %08x]\n", kind, GUEST_ADDR(d));
    *(uint32_t *)GUEST_MEM(a2) = GUEST_ADDR(d);
    return DI_OK;
}

uint32_t CCALL IDirectInput_EnumDevices_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    return DI_OK; /* no joysticks */
}

uint32_t CCALL IDirectInput_GetDeviceStatus_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DI_OK; }
uint32_t CCALL IDirectInput_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }
uint32_t CCALL IDirectInput_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }

/* ----------------------------------------------------------------- device */

uint32_t CCALL IDirectInputDevice_QueryInterface_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1;
    if (a2) *(uint32_t *)GUEST_MEM(a2) = 0;
    return E_NOINTERFACE_;
}

uint32_t CCALL IDirectInputDevice_AddRef_c(uint32_t a0)
{
    return ++((di_device *)GUEST_MEM(a0))->ref;
}

uint32_t CCALL IDirectInputDevice_Release_c(uint32_t a0)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    return d->ref ? --d->ref : 0;
}

uint32_t CCALL IDirectInputDevice_GetCapabilities_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DIERR_UNSUPPORTED; }
uint32_t CCALL IDirectInputDevice_EnumObjects_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { (void)a0; (void)a1; (void)a2; (void)a3; return DI_OK; }
uint32_t CCALL IDirectInputDevice_GetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DIERR_UNSUPPORTED; }
uint32_t CCALL IDirectInputDevice_SetProperty_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (input_debug()) fprintf(stderr, "[dinput %08x SetProperty %u %u]\n", a0, a1, a2 ? ((uint32_t *)GUEST_MEM(a2))[4] : 0);
    return DI_OK;
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
            if (keys[sc] || Game_AutoKeyDown((SDL_Scancode)sc)) { uint8_t dik = Game_PcScancode((SDL_Scancode)sc); if (dik) out[dik] = 0x80; }
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

uint32_t CCALL IDirectInputDevice_SetDataFormat_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DI_OK; }
uint32_t CCALL IDirectInputDevice_SetEventNotification_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DI_OK; }
uint32_t CCALL IDirectInputDevice_SetCooperativeLevel_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a1;
    if (input_debug()) fprintf(stderr, "[dinput %08x SetCooperativeLevel %x]\n", a0, a2);
    return DI_OK;
}
uint32_t CCALL IDirectInputDevice_GetObjectInfo_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { (void)a0; (void)a1; (void)a2; (void)a3; return DIERR_UNSUPPORTED; }

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

/* Whether the DirectInput keyboard device has been created (key_mutex != NULL):
 * the program's main loop skips the SDL keyboard events then, as DirectInput
 * owns the keyboard. */
uint32_t CCALL winapi_is_dinput_active(void)
{
    return (key_mutex != NULL) ? 1 : 0;
}
