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

/* The host's gamepads (Game_pad.h).  The state is read from SDL when it is
 * asked for: SDL keeps it up to date in SDL_PumpEvents, on whichever thread
 * pumps, and its joystick functions take its own lock.  The pads' events are
 * not needed for that, and the axes' ones are off everywhere: a stick sends
 * hundreds a second, and on win32 they would wait in SDL's queue for the
 * guest's message pump.
 *
 * Which pads there are is looked at by the timer thread's tick, every
 * SCAN_MS, not on SDL's events.  SDL2 on Linux and Windows is sdl2-compat over
 * SDL3, and it keeps the list of devices behind SDL2's device indices without
 * a lock: it makes it again, freeing the one before, in SDL_NumJoysticks, in
 * its event filter when SDL3 sends a pad's coming or going (SDL3 calls the
 * filter even for an event that is off, inside the pump of whichever thread
 * pumps), and when such an event is taken out of the queue.  A look at that
 * list from another thread freed it twice, and a pad that came or went while
 * the game ran crashed it under Windows.  So under sdl2-compat this file never
 * asks for the list by SDL2's indices: it asks SDL3 itself, which locks
 * (SDL_GetJoysticks, SDL_OpenGamepad by its ID; sdl3_load), and the pads'
 * events are off, so that none is taken out of the queue.  The other calls
 * of a pad that is open go straight through to SDL3.  SDL2 itself (the
 * Android build) locks its list, and stops updating the pads when all their
 * events are off: there the events stay on, and the loops let them go.
 *
 * The port's panel (Game_osd.c) is moved with the pad as with the keyboard:
 * while it is shown, the tick sends a key of the pad's button to it (a key
 * without a scancode, which no game reads). */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL.h>

#include "Game_pad.h"
#include "Game_osd.h"
#include "Game_hooks.h"

#define SCAN_MS 250

typedef struct {
    SDL_GameController *controller;
    uint32_t id;                /* SDL3's ID under sdl2-compat, else SDL2's instance ID */
    char name[64];
    Game_PadState last;         /* the raw state of the last read, for `packet` */
    Game_PadState logged;       /* <GAME>_DEBUG_PAD: the state last written */
} pad_slot;

static pad_slot slots[GAME_PAD_MAX];
static SDL_mutex *pad_lock = NULL;
static int pad_enabled = 1;
static int pad_started = 0;
static int pad_debug = 0;
static uint32_t pad_debug_time = 0;
static uint32_t pad_scan_time = 0;
static uint16_t pad_osd_buttons = 0;    /* the timer thread's: the buttons held when it last looked */

int Game_PadConfig(const char *key, const char *value)
{
    if (strcasecmp(key, "Gamepad") != 0) return 0;
    if (strcasecmp(value, "off") == 0) pad_enabled = 0;
    else if (strcasecmp(value, "on") == 0) pad_enabled = 1;
    return 1;
}

/* sdl2-compat, not SDL2: it numbers its versions from 2.x.50 */
static int sdl2_compat(void)
{
    SDL_version version;

    SDL_GetVersion(&version);
    return (version.major == 2) && (version.patch >= 50);
}

/* SDL3's own functions for the list of pads, under sdl2-compat (see the top
 * of the file): the library sdl2-compat has loaded, by the names it loads */
typedef uint32_t *(SDLCALL *sdl3_get_joysticks)(int *count);
typedef bool (SDLCALL *sdl3_is_gamepad)(uint32_t id);
typedef SDL_GameController *(SDLCALL *sdl3_open_gamepad)(uint32_t id);
typedef void (SDLCALL *sdl3_free)(void *mem);

static struct {
    void *library;
    sdl3_get_joysticks get_joysticks;
    sdl3_is_gamepad is_gamepad;
    sdl3_open_gamepad open_gamepad;
    sdl3_free free;
} sdl3;

static int sdl3_load(void)
{
    static const char *const names[] = {
#if defined(_WIN32)
        "SDL3.dll",
#else
        "libSDL3.so.0", "libSDL3.so",
#endif
    };
    unsigned int i;

    for (i = 0; (sdl3.library == NULL) && (i < sizeof(names) / sizeof(names[0])); i++)
    {
        sdl3.library = SDL_LoadObject(names[i]);
    }
    if (sdl3.library == NULL) return 0;
    sdl3.get_joysticks = (sdl3_get_joysticks)SDL_LoadFunction(sdl3.library, "SDL_GetJoysticks");
    sdl3.is_gamepad = (sdl3_is_gamepad)SDL_LoadFunction(sdl3.library, "SDL_IsGamepad");
    sdl3.open_gamepad = (sdl3_open_gamepad)SDL_LoadFunction(sdl3.library, "SDL_OpenGamepad");
    sdl3.free = (sdl3_free)SDL_LoadFunction(sdl3.library, "SDL_free");
    if ((sdl3.get_joysticks != NULL) && (sdl3.is_gamepad != NULL) && (sdl3.open_gamepad != NULL) && (sdl3.free != NULL))
    {
        return 1;
    }
    SDL_UnloadObject(sdl3.library);
    memset(&sdl3, 0, sizeof(sdl3));
    return 0;
}

/* the pads' events: see the top of the file */
static void pad_events(void)
{
    static const Uint32 all[] = {
        SDL_JOYHATMOTION, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP, SDL_JOYDEVICEADDED, SDL_JOYDEVICEREMOVED,
#if SDL_VERSION_ATLEAST(2,24,0)
        SDL_JOYBATTERYUPDATED,
#endif
        SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP,
        SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED, SDL_CONTROLLERDEVICEREMAPPED,
#if SDL_VERSION_ATLEAST(2,0,14)
        SDL_CONTROLLERTOUCHPADDOWN, SDL_CONTROLLERTOUCHPADMOTION, SDL_CONTROLLERTOUCHPADUP,
        SDL_CONTROLLERSENSORUPDATE,
#endif
#if SDL_VERSION_ATLEAST(2,30,0)
        SDL_CONTROLLERSTEAMHANDLEUPDATED,
#endif
    };
    unsigned int i;

    SDL_EventState(SDL_JOYAXISMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBALLMOTION, SDL_IGNORE);
    SDL_EventState(SDL_CONTROLLERAXISMOTION, SDL_IGNORE);
    if (sdl3.library == NULL) return;
    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) SDL_EventState(all[i], SDL_IGNORE);
}

static void close_slot(int pad)
{
    if (slots[pad].controller != NULL) SDL_GameControllerClose(slots[pad].controller);
    memset(&slots[pad], 0, sizeof(slots[pad]));
}

/* under pad_lock: the slot of the pad with this ID, or -1; *free_pad: the first free slot, or -1 */
static int find_slot(uint32_t id, int *free_pad)
{
    int pad;

    *free_pad = -1;
    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        if ((slots[pad].controller != NULL) && (slots[pad].id == id)) return pad;
        if ((slots[pad].controller == NULL) && (*free_pad < 0)) *free_pad = pad;
    }
    return -1;
}

/* under pad_lock: a pad opened into a free slot */
static void take_slot(int pad, SDL_GameController *controller, uint32_t id)
{
    const char *name;

    slots[pad].controller = controller;
    slots[pad].id = id;
    name = SDL_GameControllerName(controller);
    snprintf(slots[pad].name, sizeof(slots[pad].name), "%s", (name != NULL) ? name : "gamepad");
    fprintf(stderr, "Gamepad %d: %s\n", pad, slots[pad].name);
}

/* under pad_lock: close the pads that went, open the ones that came */
static void rescan(void)
{
    int pad, free_pad, i, count;

    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        if ((slots[pad].controller != NULL) && !SDL_GameControllerGetAttached(slots[pad].controller))
        {
            fprintf(stderr, "Gamepad %d: %s: gone\n", pad, slots[pad].name);
            close_slot(pad);
        }
    }

    if (sdl3.library != NULL)
    {
        /* sdl2-compat: SDL3's list, by SDL3's IDs */
        uint32_t *ids = sdl3.get_joysticks(&count);

        if (ids == NULL) return;
        for (i = 0; i < count; i++)
        {
            SDL_GameController *controller;

            if (!sdl3.is_gamepad(ids[i])) continue;
            if ((find_slot(ids[i], &free_pad) >= 0) || (free_pad < 0)) continue;
            controller = sdl3.open_gamepad(ids[i]);
            if (controller != NULL) take_slot(free_pad, controller, ids[i]);
        }
        sdl3.free(ids);
        return;
    }

    /* SDL2: its device indices (it locks its list) */
    count = SDL_NumJoysticks();
    for (i = 0; i < count; i++)
    {
        SDL_GameController *controller;
        uint32_t id;

        if (!SDL_IsGameController(i)) continue;
        id = (uint32_t)SDL_JoystickGetDeviceInstanceID(i);
        if ((find_slot(id, &free_pad) >= 0) || (free_pad < 0)) continue;
        controller = SDL_GameControllerOpen(i);
        if (controller == NULL) continue;
        take_slot(free_pad, controller, (uint32_t)SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)));
    }
}

static int lock_pads(void)
{
    if (!pad_started) return 0;
    SDL_LockMutex(pad_lock);
    return 1;
}

void Game_PadInit(void)
{
    const char *debug;

    if (pad_started || !pad_enabled) return;

    if (sdl2_compat() && !sdl3_load())
    {
        /* no SDL3 to ask: the pads would be looked at through sdl2-compat's list */
        fprintf(stderr, "Gamepad: SDL3 not found under sdl2-compat, no gamepads\n");
        return;
    }
    /* before the pads are opened, so that no event of theirs is queued (and
     * again after: switching an event off also drops the queued ones) */
    pad_events();
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "Gamepad: SDL: %s\n", SDL_GetError());
        return;
    }
    pad_lock = SDL_CreateMutex();
    if (pad_lock == NULL)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        return;
    }

#if SDL_VERSION_ATLEAST(2,0,2)
    /* the mappings of the community's database, when there is one beside the game */
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
#endif
    pad_events();

    debug = getenv(GAME_ENV("DEBUG_PAD"));
    pad_debug = (debug != NULL) && (debug[0] != 0) && (strcmp(debug, "0") != 0);

    pad_started = 1;
    SDL_LockMutex(pad_lock);
    rescan();
    SDL_UnlockMutex(pad_lock);
    pad_scan_time = SDL_GetTicks();
}

void Game_PadQuit(void)
{
    int pad;

    if (!pad_started) return;
    SDL_LockMutex(pad_lock);
    for (pad = 0; pad < GAME_PAD_MAX; pad++) close_slot(pad);
    pad_started = 0;
    SDL_UnlockMutex(pad_lock);
    SDL_DestroyMutex(pad_lock);
    pad_lock = NULL;
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (sdl3.library != NULL) SDL_UnloadObject(sdl3.library);
    memset(&sdl3, 0, sizeof(sdl3));
}

int Game_PadCount(void)
{
    unsigned int bits = Game_PadSlots();
    int count = 0;

    for (; bits != 0; bits >>= 1) count += bits & 1;
    return count;
}

unsigned int Game_PadSlots(void)
{
    unsigned int bits = 0;
    int pad;

    if (!lock_pads()) return 0;
    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        if (slots[pad].controller != NULL) bits |= 1u << pad;
    }
    SDL_UnlockMutex(pad_lock);
    return bits;
}

/* under pad_lock: SDL's state of a pad that is there */
static void read_raw(int pad, Game_PadState *state)
{
    static const struct { SDL_GameControllerButton button; uint16_t bit; } buttons[] = {
        { SDL_CONTROLLER_BUTTON_DPAD_UP, GAME_PAD_DPAD_UP },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, GAME_PAD_DPAD_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, GAME_PAD_DPAD_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GAME_PAD_DPAD_RIGHT },
        { SDL_CONTROLLER_BUTTON_START, GAME_PAD_START },
        { SDL_CONTROLLER_BUTTON_BACK, GAME_PAD_BACK },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, GAME_PAD_LEFT_THUMB },
        { SDL_CONTROLLER_BUTTON_RIGHTSTICK, GAME_PAD_RIGHT_THUMB },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GAME_PAD_LEFT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GAME_PAD_RIGHT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_GUIDE, GAME_PAD_GUIDE },
        { SDL_CONTROLLER_BUTTON_A, GAME_PAD_A },
        { SDL_CONTROLLER_BUTTON_B, GAME_PAD_B },
        { SDL_CONTROLLER_BUTTON_X, GAME_PAD_X },
        { SDL_CONTROLLER_BUTTON_Y, GAME_PAD_Y },
    };
    SDL_GameController *controller = slots[pad].controller;
    unsigned int i;
    int value;

    memset(state, 0, sizeof(*state));
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        if (SDL_GameControllerGetButton(controller, buttons[i].button)) state->buttons |= buttons[i].bit;
    }
    state->left_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    state->right_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    /* SDL's Y goes down, XInput's up */
    value = -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    state->left_y = (int16_t)((value > 32767) ? 32767 : value);
    value = -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
    state->right_y = (int16_t)((value > 32767) ? 32767 : value);
    /* 0..32767 to 0..255 */
    value = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    state->left_trigger = (uint8_t)((value > 0) ? (value >> 7) : 0);
    value = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    state->right_trigger = (uint8_t)((value > 0) ? (value >> 7) : 0);

    if ((state->buttons != slots[pad].last.buttons) || (state->left_trigger != slots[pad].last.left_trigger)
        || (state->right_trigger != slots[pad].last.right_trigger) || (state->left_x != slots[pad].last.left_x)
        || (state->left_y != slots[pad].last.left_y) || (state->right_x != slots[pad].last.right_x)
        || (state->right_y != slots[pad].last.right_y))
    {
        state->packet = slots[pad].last.packet + 1;
        slots[pad].last = *state;
    }
    else
    {
        state->packet = slots[pad].last.packet;
    }
}

/* a stick's dead zone, round: inside it 0, past it from 0 to the edge, the direction kept */
static void stick_deadzone(int16_t *x, int16_t *y, int deadzone)
{
    double fx = *x, fy = *y, length, scaled;

    length = sqrt(fx * fx + fy * fy);
    if (length <= deadzone)
    {
        *x = *y = 0;
        return;
    }
    scaled = (length - deadzone) / (32767.0 - deadzone);
    if (scaled > 1.0) scaled = 1.0;
    fx = fx / length * scaled * 32767.0;
    fy = fy / length * scaled * 32767.0;
    *x = (int16_t)lrint(fx);
    *y = (int16_t)lrint(fy);
}

static uint8_t trigger_deadzone(uint8_t value)
{
    if (value <= GAME_PAD_TRIGGER_DEADZONE) return 0;
    return (uint8_t)((value - GAME_PAD_TRIGGER_DEADZONE) * 255 / (255 - GAME_PAD_TRIGGER_DEADZONE));
}

int Game_PadGetRawState(int pad, Game_PadState *state)
{
    if ((pad < 0) || (pad >= GAME_PAD_MAX) || !lock_pads())
    {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    if (slots[pad].controller == NULL)
    {
        SDL_UnlockMutex(pad_lock);
        memset(state, 0, sizeof(*state));
        return 0;
    }
    read_raw(pad, state);
    SDL_UnlockMutex(pad_lock);
    return 1;
}

int Game_PadGetState(int pad, Game_PadState *state)
{
    if (!Game_PadGetRawState(pad, state)) return 0;
    stick_deadzone(&state->left_x, &state->left_y, GAME_PAD_LEFT_DEADZONE);
    stick_deadzone(&state->right_x, &state->right_y, GAME_PAD_RIGHT_DEADZONE);
    state->left_trigger = trigger_deadzone(state->left_trigger);
    state->right_trigger = trigger_deadzone(state->right_trigger);
    return 1;
}

const char *Game_PadName(int pad)
{
    const char *name = "";

    if ((pad < 0) || (pad >= GAME_PAD_MAX) || !lock_pads()) return name;
    /* the slot's name stays where it is while the program runs */
    if (slots[pad].controller != NULL) name = slots[pad].name;
    SDL_UnlockMutex(pad_lock);
    return name;
}

int Game_PadRumble(int pad, uint16_t low, uint16_t high, uint32_t ms)
{
    int done = 0;

    if ((pad < 0) || (pad >= GAME_PAD_MAX) || !lock_pads()) return 0;
#if SDL_VERSION_ATLEAST(2,0,9)
    if (slots[pad].controller != NULL)
    {
        done = (SDL_GameControllerRumble(slots[pad].controller, low, high, ms) == 0);
    }
#else
    (void)low; (void)high; (void)ms;
#endif
    SDL_UnlockMutex(pad_lock);
    return done;
}

/* the panel's keys of the pad's buttons, on a press */
static void osd_keys(uint16_t buttons)
{
    static const struct { uint16_t bit; SDL_Keycode key; } keys[] = {
        { GAME_PAD_DPAD_UP, SDLK_UP },
        { GAME_PAD_DPAD_DOWN, SDLK_DOWN },
        { GAME_PAD_DPAD_LEFT, SDLK_LEFT },
        { GAME_PAD_DPAD_RIGHT, SDLK_RIGHT },
        { GAME_PAD_A, SDLK_RETURN },
        { GAME_PAD_B, SDLK_ESCAPE },
        { GAME_PAD_BACK, SDLK_ESCAPE },
    };
    uint16_t pressed = buttons & ~pad_osd_buttons;
    unsigned int i;

    pad_osd_buttons = buttons;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        SDL_Event event;

        if (!(pressed & keys[i].bit)) continue;
        memset(&event, 0, sizeof(event));
        event.type = SDL_KEYDOWN;
        event.key.timestamp = SDL_GetTicks();
        event.key.state = SDL_PRESSED;
        event.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;
        event.key.keysym.sym = keys[i].key;
        SDL_PushEvent(&event);
    }
}

void Game_PadTick(void)
{
    uint32_t now;
    int pad;

    if (!pad_started) return;
    now = SDL_GetTicks();

    if ((now - pad_scan_time) >= SCAN_MS)
    {
        pad_scan_time = now;
        SDL_LockMutex(pad_lock);
        rescan();
        SDL_UnlockMutex(pad_lock);
    }

    if (Game_OsdVisible())
    {
        uint16_t buttons = 0;

        for (pad = 0; pad < GAME_PAD_MAX; pad++)
        {
            Game_PadState state;

            if (Game_PadGetState(pad, &state)) buttons |= state.buttons;
        }
        osd_keys(buttons);
    }
    else
    {
        /* what is held when the panel opens moves nothing in it */
        pad_osd_buttons = 0xffff;
    }

    if (!pad_debug) return;
    if ((now - pad_debug_time) < 50) return;
    pad_debug_time = now;

    SDL_LockMutex(pad_lock);
    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        Game_PadState state;

        if (slots[pad].controller == NULL) continue;
        read_raw(pad, &state);
        if (state.packet == slots[pad].logged.packet) continue;
        slots[pad].logged = state;
        fprintf(stderr, "Gamepad %d: buttons %04x  left %6d %6d  right %6d %6d  triggers %3u %3u\n",
                pad, state.buttons, state.left_x, state.left_y, state.right_x, state.right_y,
                state.left_trigger, state.right_trigger);
    }
    SDL_UnlockMutex(pad_lock);
}
