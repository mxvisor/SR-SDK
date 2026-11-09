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
 * not needed for that: the axes' ones are switched off (a stick sends hundreds
 * a second, and on win32 they would wait in SDL's queue for the guest's message
 * pump); the buttons' stay for the port's panel.  A pad that comes or goes
 * only marks the slots for a new look (an event watch, which runs inside
 * SDL_PumpEvents and must not open a device there); the next call of any
 * function here, or the timer thread's tick, looks. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL.h>

#include "Game_pad.h"
#include "Game_hooks.h"

typedef struct {
    SDL_GameController *controller;
    SDL_JoystickID id;
    char name[64];
    Game_PadState last;         /* the raw state of the last read, for `packet` */
    Game_PadState logged;       /* <GAME>_DEBUG_PAD: the state last written */
} pad_slot;

static pad_slot slots[GAME_PAD_MAX];
static SDL_mutex *pad_lock = NULL;
static SDL_atomic_t pad_rescan;
static int pad_enabled = 1;
static int pad_started = 0;
static int pad_debug = 0;
static uint32_t pad_debug_time = 0;

int Game_PadConfig(const char *key, const char *value)
{
    if (strcasecmp(key, "Gamepad") != 0) return 0;
    if (strcasecmp(value, "off") == 0) pad_enabled = 0;
    else if (strcasecmp(value, "on") == 0) pad_enabled = 1;
    return 1;
}

static int SDLCALL pad_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;
    switch (event->type)
    {
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            SDL_AtomicSet(&pad_rescan, 1);
            break;
    }
    return 1;
}

static void close_slot(int pad)
{
    if (slots[pad].controller != NULL) SDL_GameControllerClose(slots[pad].controller);
    memset(&slots[pad], 0, sizeof(slots[pad]));
}

/* under pad_lock: close the pads that went, open the ones that came */
static void rescan(void)
{
    int pad, index, count;

    for (pad = 0; pad < GAME_PAD_MAX; pad++)
    {
        if ((slots[pad].controller != NULL) && !SDL_GameControllerGetAttached(slots[pad].controller))
        {
            fprintf(stderr, "Gamepad %d: %s: gone\n", pad, slots[pad].name);
            close_slot(pad);
        }
    }

    count = SDL_NumJoysticks();
    for (index = 0; index < count; index++)
    {
        SDL_JoystickID id;
        SDL_GameController *controller;
        const char *name;
        int free_pad = -1;

        if (!SDL_IsGameController(index)) continue;
        id = SDL_JoystickGetDeviceInstanceID(index);
        for (pad = 0; pad < GAME_PAD_MAX; pad++)
        {
            if ((slots[pad].controller != NULL) && (slots[pad].id == id)) break;
            if ((slots[pad].controller == NULL) && (free_pad < 0)) free_pad = pad;
        }
        if ((pad < GAME_PAD_MAX) || (free_pad < 0)) continue;

        controller = SDL_GameControllerOpen(index);
        if (controller == NULL) continue;
        slots[free_pad].controller = controller;
        slots[free_pad].id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
        name = SDL_GameControllerName(controller);
        snprintf(slots[free_pad].name, sizeof(slots[free_pad].name), "%s", (name != NULL) ? name : "gamepad");
        fprintf(stderr, "Gamepad %d: %s\n", free_pad, slots[free_pad].name);
    }
}

/* takes pad_lock, and looks at the pads first when one came or went */
static int lock_pads(void)
{
    if (!pad_started) return 0;
    SDL_LockMutex(pad_lock);
    if (SDL_AtomicSet(&pad_rescan, 0)) rescan();
    return 1;
}

void Game_PadInit(void)
{
    const char *debug;

    if (pad_started || !pad_enabled) return;

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
    /* the state is read, not sent: see the top of the file */
    SDL_EventState(SDL_JOYAXISMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBALLMOTION, SDL_IGNORE);
    SDL_EventState(SDL_CONTROLLERAXISMOTION, SDL_IGNORE);
    SDL_AddEventWatch(pad_watch, NULL);

    debug = getenv(GAME_ENV("DEBUG_PAD"));
    pad_debug = (debug != NULL) && (debug[0] != 0) && (strcmp(debug, "0") != 0);

    pad_started = 1;
    SDL_AtomicSet(&pad_rescan, 1);
    if (lock_pads()) SDL_UnlockMutex(pad_lock);
}

void Game_PadQuit(void)
{
    int pad;

    if (!pad_started) return;
    SDL_DelEventWatch(pad_watch, NULL);
    SDL_LockMutex(pad_lock);
    for (pad = 0; pad < GAME_PAD_MAX; pad++) close_slot(pad);
    pad_started = 0;
    SDL_UnlockMutex(pad_lock);
    SDL_DestroyMutex(pad_lock);
    pad_lock = NULL;
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
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

void Game_PadTick(void)
{
    uint32_t now;
    int pad;

    if (!pad_started) return;
    if (!pad_debug)
    {
        /* a pad that came is opened here, not first in the game's call */
        if (SDL_AtomicGet(&pad_rescan) && lock_pads()) SDL_UnlockMutex(pad_lock);
        return;
    }

    now = SDL_GetTicks();
    if ((now - pad_debug_time) < 50) return;
    pad_debug_time = now;

    if (!lock_pads()) return;
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
