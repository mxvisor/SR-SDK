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

#if defined(PTROFS_64BIT)
extern uint64_t pointer_offset;
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))
#define GUEST_ADDR(p) ((uint32_t)((p) ? ((uintptr_t)(p) - pointer_offset) : 0))
#else
#define GUEST_MEM(a) ((void *)(uintptr_t)(a))
#define GUEST_ADDR(p) ((uint32_t)(uintptr_t)(p))
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
    dik = Game_PcScancode(event->key.keysym.scancode);
    if (dik == 0) return 0;
    SDL_LockMutex(key_mutex);
    if (((key_head + 1) % KEY_QUEUE_SIZE) != key_tail)
    {
        key_queue[key_head].dik = dik;
        key_queue[key_head].down = (event->type == SDL_KEYDOWN);
        key_queue[key_head].time = event->key.timestamp;
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

static int SDLCALL mouse_event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;
    switch (event->type)
    {
        case SDL_MOUSEMOTION:
            if (event->motion.which == SDL_TOUCH_MOUSEID) return 0;
            SDL_LockMutex(mouse_mutex);
            mouse_dx += event->motion.xrel;
            mouse_dy += event->motion.yrel;
            if (event->motion.xrel) mouse_queue_put(0, (uint32_t)event->motion.xrel, event->motion.timestamp); /* DIMOFS_X */
            if (event->motion.yrel) mouse_queue_put(4, (uint32_t)event->motion.yrel, event->motion.timestamp); /* DIMOFS_Y */
            SDL_UnlockMutex(mouse_mutex);
            break;
        case SDL_MOUSEWHEEL:
            if (event->wheel.which == SDL_TOUCH_MOUSEID || event->wheel.y == 0) return 0;
            SDL_LockMutex(mouse_mutex);
            mouse_dz += event->wheel.y * 120; /* WHEEL_DELTA per notch */
            mouse_queue_put(8, (uint32_t)(event->wheel.y * 120), event->wheel.timestamp); /* DIMOFS_Z */
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
            mouse_queue_put((uint8_t)(12 + button), mouse_buttons_down[button], event->button.timestamp); /* DIMOFS_BUTTONn */
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
    SDL_AddEventWatch(mouse_event_watch, NULL);
}

/* <GAME>_DEBUG_INPUT: which DirectInput calls the game makes for which device */
static int input_debug(void)
{
    static int value = -1;
    if (value < 0) value = (getenv(GAME_ENV("DEBUG_INPUT")) != NULL);
    return value;
}

static void key_queue_init(void)
{
    if (key_mutex != NULL) return;
    key_mutex = SDL_CreateMutex();
    SDL_AddEventWatch(key_event_watch, NULL);
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
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_Unacquire_c(uint32_t a0)
{
    if (input_debug()) fprintf(stderr, "[dinput %08x Unacquire]\n", a0);
    return DI_OK;
}

uint32_t CCALL IDirectInputDevice_GetDeviceState_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    di_device *d = (di_device *)GUEST_MEM(a0);
    uint8_t *out = (uint8_t *)GUEST_MEM(a2);
    if (out == NULL) return DIERR_INVALIDPARAM;
    memset(out, 0, a1);

    if (d->kind == DEV_KEYBOARD)
    {
        int n = 0, sc;
        const uint8_t *keys = SDL_GetKeyboardState(&n);
        if (a1 < 256) return DIERR_INVALIDPARAM;
        for (sc = 0; sc < n; sc++)
            if (keys[sc] || Game_AutoKeyDown((SDL_Scancode)sc)) { uint8_t dik = Game_PcScancode((SDL_Scancode)sc); if (dik) out[dik] = 0x80; }
    }
    else if (d->kind == DEV_MOUSE)
    {
        int32_t *axes = (int32_t *)out; /* DIMOUSESTATE: lX, lY, lZ, rgbButtons[4] */
        if (a1 < 16) return DIERR_INVALIDPARAM;
        SDL_LockMutex(mouse_mutex);
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
uint32_t CCALL IDirectInputDevice_GetDeviceInfo_c(uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return DIERR_UNSUPPORTED; }
uint32_t CCALL IDirectInputDevice_RunControlPanel_c(uint32_t a0, uint32_t a1, uint32_t a2) { (void)a0; (void)a1; (void)a2; return DI_OK; }
uint32_t CCALL IDirectInputDevice_Initialize_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { (void)a0; (void)a1; (void)a2; (void)a3; return DI_OK; }

/* Whether the DirectInput keyboard device has been created (key_mutex != NULL):
 * the program's main loop skips the SDL keyboard events then, as DirectInput
 * owns the keyboard. */
uint32_t CCALL winapi_is_dinput_active(void)
{
    return (key_mutex != NULL) ? 1 : 0;
}
