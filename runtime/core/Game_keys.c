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

/* The PC scancodes of SDL's keys (the DOS keyboard of runtime/dos, DirectInput
 * of the winapi layer) and <GAME>_AUTO_KEYS. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include "Game_keys.h"
#include "Game_hooks.h"

/* how long <GAME>_AUTO_KEYS holds a key without ~hold_ms: menus that sample
 * the key state once a frame need a few frames of it */
#ifndef GAME_AUTO_KEY_HOLD_MS
#define GAME_AUTO_KEY_HOLD_MS 250
#endif

uint8_t Game_PcScancode(SDL_Scancode sc)
{
    static const uint8_t letters[26] = { 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                         0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c };
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return letters[sc - SDL_SCANCODE_A];
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (uint8_t)(0x02 + sc - SDL_SCANCODE_1);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (uint8_t)(0x3b + sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_3) return (uint8_t)(0x4f + sc - SDL_SCANCODE_KP_1);
    switch (sc)
    {
        case SDL_SCANCODE_0: return 0x0b;
        case SDL_SCANCODE_ESCAPE: return 0x01;
        case SDL_SCANCODE_MINUS: return 0x0c;
        case SDL_SCANCODE_EQUALS: return 0x0d;
        case SDL_SCANCODE_BACKSPACE: return 0x0e;
        case SDL_SCANCODE_TAB: return 0x0f;
        case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
        case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
        case SDL_SCANCODE_RETURN: return 0x1c;
        case SDL_SCANCODE_LCTRL: return 0x1d;
        case SDL_SCANCODE_SEMICOLON: return 0x27;
        case SDL_SCANCODE_APOSTROPHE: return 0x28;
        case SDL_SCANCODE_GRAVE: return 0x29;
        case SDL_SCANCODE_LSHIFT: return 0x2a;
        case SDL_SCANCODE_BACKSLASH: return 0x2b;
        case SDL_SCANCODE_COMMA: return 0x33;
        case SDL_SCANCODE_PERIOD: return 0x34;
        case SDL_SCANCODE_SLASH: return 0x35;
        case SDL_SCANCODE_RSHIFT: return 0x36;
        case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
        case SDL_SCANCODE_LALT: return 0x38;
        case SDL_SCANCODE_SPACE: return 0x39;
        case SDL_SCANCODE_CAPSLOCK: return 0x3a;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
        case SDL_SCANCODE_SCROLLLOCK: return 0x46;
        case SDL_SCANCODE_KP_7: return 0x47;
        case SDL_SCANCODE_KP_8: return 0x48;
        case SDL_SCANCODE_KP_9: return 0x49;
        case SDL_SCANCODE_KP_MINUS: return 0x4a;
        case SDL_SCANCODE_KP_4: return 0x4b;
        case SDL_SCANCODE_KP_5: return 0x4c;
        case SDL_SCANCODE_KP_6: return 0x4d;
        case SDL_SCANCODE_KP_PLUS: return 0x4e;
        case SDL_SCANCODE_KP_0: return 0x52;
        case SDL_SCANCODE_KP_PERIOD: return 0x53;
        case SDL_SCANCODE_F11: return 0x57;
        case SDL_SCANCODE_F12: return 0x58;
        case SDL_SCANCODE_KP_ENTER: return 0x9c;
        case SDL_SCANCODE_RCTRL: return 0x9d;
        case SDL_SCANCODE_KP_DIVIDE: return 0xb5;
        case SDL_SCANCODE_PRINTSCREEN: return 0xb7;
        case SDL_SCANCODE_RALT: return 0xb8;
        case SDL_SCANCODE_PAUSE: return 0xc5;
        case SDL_SCANCODE_HOME: return 0xc7;
        case SDL_SCANCODE_UP: return 0xc8;
        case SDL_SCANCODE_PAGEUP: return 0xc9;
        case SDL_SCANCODE_LEFT: return 0xcb;
        case SDL_SCANCODE_RIGHT: return 0xcd;
        case SDL_SCANCODE_END: return 0xcf;
        case SDL_SCANCODE_DOWN: return 0xd0;
        case SDL_SCANCODE_PAGEDOWN: return 0xd1;
        case SDL_SCANCODE_INSERT: return 0xd2;
        case SDL_SCANCODE_DELETE: return 0xd3;
        default: return 0;
    }
}

/* the keys <GAME>_AUTO_KEYS holds down */
static volatile uint8_t injected_keys[SDL_NUM_SCANCODES];

int Game_AutoKeyDown(SDL_Scancode scancode)
{
    return scancode >= 0 && scancode < SDL_NUM_SCANCODES && injected_keys[scancode];
}

/* A key event as SDL would send it, but with windowID 0 (see Game_keys.h). */
static void push_key(SDL_Scancode scancode, int down)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
    injected_keys[scancode] = (uint8_t)down;
    SDL_PushEvent(&event);
}

static void press_key(SDL_Scancode scancode, uint32_t hold_ms)
{
    push_key(scancode, 1);
    SDL_Delay(hold_ms);
    push_key(scancode, 0);
}

/* One line of the control FIFO.  Read with read(), not stdio: a thread blocked
 * in fgets holds the FILE's lock, and exit() then waits for it forever while
 * flushing the streams. */
#if defined(_WIN32)
typedef FILE *fifo_t;
#define fifo_open(path) fopen((path), "r")
#define fifo_close(fifo) fclose(fifo)
#define fifo_valid(fifo) ((fifo) != NULL)
static int fifo_read_line(fifo_t fifo, char *line, int size)
{
    return fgets(line, size, fifo) != NULL;
}
#else
typedef int fifo_t;
#define fifo_open(path) open((path), O_RDONLY)
#define fifo_close(fifo) close(fifo)
#define fifo_valid(fifo) ((fifo) >= 0)
static int fifo_read_line(fifo_t fifo, char *line, int size)
{
    int length = 0;
    char c = 0;

    while (read(fifo, &c, 1) == 1)
    {
        if (c == '\n') break;
        if (length < size - 1) line[length++] = c;
    }
    line[length] = 0;
    return (length != 0) || (c == '\n');
}
#endif

/* "@path": key names, one per line, from that file or FIFO as they arrive.
 * "+Key" presses and keeps it down, "-Key" releases it, "!Key" is a tap
 * shorter than a frame, "~dx,dy" moves the mouse, "~L" / "~R" clicks a
 * button. */
static void read_fifo(const char *path)
{
    char line[64];

    for (;;)
    {
        fifo_t fifo = fifo_open(path);

        if (!fifo_valid(fifo)) return;
        while (fifo_read_line(fifo, line, sizeof(line)))
        {
            SDL_Scancode scancode;

            line[strcspn(line, "\r\n")] = 0;
            if (line[0] == '~')
            {
                SDL_Event event;

                memset(&event, 0, sizeof(event));
                if (line[1] == 'L' || line[1] == 'R')
                {
                    event.type = SDL_MOUSEBUTTONDOWN;
                    event.button.button = (line[1] == 'L') ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
                    event.button.state = SDL_PRESSED;
                    SDL_PushEvent(&event);
                    SDL_Delay(100);
                    event.type = SDL_MOUSEBUTTONUP;
                    event.button.state = SDL_RELEASED;
                }
                else
                {
                    event.type = SDL_MOUSEMOTION;
                    if (sscanf(line + 1, "%d,%d", &event.motion.xrel, &event.motion.yrel) != 2) continue;
                }
                SDL_PushEvent(&event);
                continue;
            }
            if (line[0] == '+' || line[0] == '-')
            {
                scancode = SDL_GetScancodeFromName(line + 1);
                if (scancode != SDL_SCANCODE_UNKNOWN) push_key(scancode, line[0] == '+');
                continue;
            }
            if (line[0] == '!')
            {
                scancode = SDL_GetScancodeFromName(line + 1);
                if (scancode != SDL_SCANCODE_UNKNOWN) press_key(scancode, 2);
                continue;
            }
            scancode = SDL_GetScancodeFromName(line);
            if (scancode != SDL_SCANCODE_UNKNOWN) press_key(scancode, GAME_AUTO_KEY_HOLD_MS);
        }
        fifo_close(fifo);
    }
}

/* <GAME>_AUTO_KEYS="seconds:Key[~hold_ms],..." presses each key (an SDL key
 * name) that many seconds after start and holds it hold_ms, or
 * GAME_AUTO_KEY_HOLD_MS; "@path" reads them from a FIFO (read_fifo).  For
 * unattended testing, where a compositor may not forward synthetic input to
 * the window. */
static int SDLCALL auto_keys_thread(void *userdata)
{
    const char *spec = (const char *)userdata;
    uint32_t start = SDL_GetTicks();

    if (*spec == '@')
    {
        read_fifo(spec + 1);
        return 0;
    }

    while (*spec != 0)
    {
        char *end;
        double seconds = strtod(spec, &end);
        unsigned long hold_ms = GAME_AUTO_KEY_HOLD_MS;
        char name[32];
        size_t length = 0;
        SDL_Scancode scancode;

        if (end == spec || *end != ':')
        {
            fprintf(stderr, "%s: bad key near '%s'\n", GAME_ENV("AUTO_KEYS"), spec);
            break;
        }
        for (spec = end + 1; *spec != 0 && *spec != ',' && *spec != '~' && length < sizeof(name) - 1; spec++) name[length++] = *spec;
        name[length] = 0;
        if (*spec == '~') hold_ms = strtoul(spec + 1, (char **)&spec, 10);
        while (*spec != 0 && *spec != ',') spec++;
        if (*spec == ',') spec++;

        scancode = SDL_GetScancodeFromName(name);
        if (scancode == SDL_SCANCODE_UNKNOWN)
        {
            fprintf(stderr, "%s: unknown key '%s'\n", GAME_ENV("AUTO_KEYS"), name);
            continue;
        }
        while (SDL_GetTicks() - start < (uint32_t)(seconds * 1000)) SDL_Delay(5);
        press_key(scancode, (uint32_t)hold_ms);
    }
    return 0;
}

void Game_AutoKeysStart(void)
{
    static int started;
    const char *auto_keys = getenv(GAME_ENV("AUTO_KEYS"));

    if (started || auto_keys == NULL || *auto_keys == 0) return;
    started = 1;
    SDL_CreateThread(auto_keys_thread, "autokeys", (void *)auto_keys);
}
