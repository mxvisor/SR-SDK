/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
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

/* The pc input: the keyboard as it is.  main.c queues SDL's key events and
 * the game's runtime turns their scan codes into the PC keyboard's; nothing is
 * remapped here.  The gamepads are Game_pad.c's: opened here, read by the
 * joystick APIs of the runtime. */

#include "Game_defs.h"
#include "Game_vars.h"
#include "input.h"
#include "touchgamepad.h"
#include "Game_pad.h"

/* A key event as SDL would send it.  The scan code
 * is set too: that is what the game's runtime reads. */
void EmulateKey(int type, int key)
{
    SDL_Event pump_event;

    pump_event.type = type;
    pump_event.key.state = (type == SDL_KEYUP)?SDL_RELEASED:SDL_PRESSED;
    pump_event.key.repeat = 0;
    pump_event.key.keysym.sym = (SDL_Keycode) key;
    pump_event.key.keysym.scancode = SDL_GetScancodeFromKey((SDL_Keycode) key);
    pump_event.key.keysym.mod = KMOD_NONE;

    SDL_PushEvent(&pump_event);
}

void Init_Input(void)
{
}

void Init_Input2(void)
{
    Game_PadInit();
}

int Config_Input(char *str, char *param)
{
    return Game_PadConfig(str, param);
}

void Cleanup_Input(void)
{
    Game_PadQuit();
}

int Handle_Input_Event(SDL_Event *_event)
{
    if (TouchGamepad_Event(_event)) return 1;

    (void)_event;

    return 0;
}

int Handle_Input_Event2(SDL_Event *_event)
{
    (void)_event;

    return 0;
}

void Handle_Timer_Input_Event(void)
{
    Game_PadTick();
}
