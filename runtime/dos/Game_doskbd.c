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

/* The keyboard of a DOS4GW game.  Two queues, as on a PC: the controller's
 * bytes, which the game's own INT 9 handler reads from port 60h, and the BIOS
 * buffer of pressed keys (scancode and ASCII) behind INT 16h, INT 21h 08h/0Bh
 * and the WATCOM kbhit/getch.  A game that hooks INT 9 does not chain to the
 * BIOS handler, so the BIOS buffer is filled here, beside the controller's. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_doskbd.h"
#include "Game_dosint.h"
#include "Game_keys.h"
#include "Game_hooks.h"

#define KBD_RAW_SIZE 128u   /* controller bytes, a power of 2 */
#define KBD_BIOS_SIZE 64u   /* BIOS buffer, a power of 2 */
/* The event loop can hand over a key's press and release in one batch; a game
 * that samples the key state once a frame would miss the press, so a key's
 * release waits this long after its own press, and a press of the key just
 * released waits as long after the release (a game that acts on a new press,
 * the key up in one frame and down in the next, would miss the second of two
 * quick taps).  Only the key's own press counts: the times between different
 * keys stay as they were (DOSBox holds none, it hands over the bytes as they
 * come), so a release never comes together with another key's.  A game whose
 * frame is longer may say how long in Game_hooks.h. */
#ifndef GAME_DOS_KBD_MIN_HOLD_MS
#define GAME_DOS_KBD_MIN_HOLD_MS 60u
#endif
/* (a variable: 0 would make the comparisons always false, a warning) */
static uint32_t min_hold_ms = GAME_DOS_KBD_MIN_HOLD_MS;


typedef struct {
    uint8_t scan;
    uint8_t ascii;
} bios_key;

static uint8_t raw_queue[KBD_RAW_SIZE];
static unsigned int raw_read, raw_write;
static bios_key bios_queue[KBD_BIOS_SIZE];
static unsigned int bios_read, bios_write;
/* INT 21h 08h: the scancode that follows the 0 of a key without ASCII */
static int ext_pending;
static uint8_t ext_scan;

/* <GAME>_TRACE_KEYS */
static int trace_keys(void)
{
    static int value = -1;
    if (value < 0) value = getenv(GAME_ENV("TRACE_KEYS")) != NULL;
    return value;
}

static void raw_put(uint8_t code)
{
    unsigned int next = (raw_write + 1u) & (KBD_RAW_SIZE - 1u);
    if (next == raw_read) return;
    raw_queue[raw_write] = code;
    raw_write = next;
}

static int raw_get(uint8_t *code)
{
    if (raw_read == raw_write) return 0;
    *code = raw_queue[raw_read];
    raw_read = (raw_read + 1u) & (KBD_RAW_SIZE - 1u);
    return 1;
}

static void bios_put(uint8_t scan, uint8_t ascii)
{
    unsigned int next = (bios_write + 1u) & (KBD_BIOS_SIZE - 1u);
    if (next == bios_read) return;
    bios_queue[bios_write].scan = scan;
    bios_queue[bios_write].ascii = ascii;
    bios_write = next;
}

static int bios_get(bios_key *key)
{
    if (bios_read == bios_write) return 0;
    *key = bios_queue[bios_read];
    bios_read = (bios_read + 1u) & (KBD_BIOS_SIZE - 1u);
    return 1;
}

static uint8_t shifted_ascii(uint8_t ascii)
{
    switch (ascii) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        case '`': return '~';
        default: return ascii;
    }
}

/* The ASCII code the BIOS gives a key (US layout), 0 for none. */
static uint8_t key_ascii(SDL_Keycode key, uint16_t mod)
{
    const int shift = (mod & KMOD_SHIFT) != 0;

    if (key >= SDLK_a && key <= SDLK_z) {
        uint8_t ascii = (uint8_t)('a' + (key - SDLK_a));
        if (shift != ((mod & KMOD_CAPS) != 0)) ascii = (uint8_t)(ascii - ('a' - 'A'));
        return ascii;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        uint8_t ascii = (uint8_t)('0' + (key - SDLK_0));
        return shift ? shifted_ascii(ascii) : ascii;
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return (mod & KMOD_NUM) ? (uint8_t)('1' + (key - SDLK_KP_1)) : 0;

    switch (key) {
        case SDLK_RETURN: return 0x0d;
        case SDLK_ESCAPE: return 0x1b;
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB: return 0x09;
        case SDLK_SPACE: return ' ';
        case SDLK_MINUS: case SDLK_EQUALS: case SDLK_LEFTBRACKET: case SDLK_RIGHTBRACKET:
        case SDLK_BACKSLASH: case SDLK_SEMICOLON: case SDLK_QUOTE: case SDLK_COMMA:
        case SDLK_PERIOD: case SDLK_SLASH: case SDLK_BACKQUOTE:
            return shift ? shifted_ascii((uint8_t)key) : (uint8_t)key;
        case SDLK_KP_0: return (mod & KMOD_NUM) ? '0' : 0;
        case SDLK_KP_PERIOD: return (mod & KMOD_NUM) ? '.' : 0;
        case SDLK_KP_ENTER: return 0x0d;
        default: return 0;
    }
}

void Game_DosKbdInit(void)
{
    raw_read = raw_write = 0;
    bios_read = bios_write = 0;
    ext_pending = 0;
    Game_AutoKeysStart();
}

/* The press of the key (byte, after E0 if grey) is in the controller's queue,
 * not yet handed to the game. */
static int raw_pending_make(uint8_t byte, int grey)
{
    unsigned int i;
    int after_e0 = 0;

    for (i = raw_read; i != raw_write; i = (i + 1u) & (KBD_RAW_SIZE - 1u)) {
        if (raw_queue[i] == 0xe0u) {
            after_e0 = 1;
            continue;
        }
        if (raw_queue[i] == byte && after_e0 == grey) return 1;
        after_e0 = 0;
    }
    return 0;
}

void Game_DosKbdDrain(void)
{
    /* the taps left out of the controller's queue, by code (E0 keys | 80h):
     * their releases go too */
    static uint8_t dropped[256];

    while (Game_KQueueRead != Game_KQueueWrite) {
        const SDL_Event *event = &Game_EventKQueue[Game_KQueueRead];
        uint8_t code;

        Game_KQueueRead = (Game_KQueueRead + 1u) & (GAME_KQUEUE_LENGTH - 1u);
        if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) continue;

        code = Game_PcScancode(event->key.keysym.scancode);
        if (code != 0) {
            const int grey = (code & 0x80u) != 0;
            const uint8_t byte = (uint8_t)((code & 0x7fu) | (event->type == SDL_KEYUP ? 0x80u : 0u));
            int drop;

            /* The presses and the releases are held apart (GAME_DOS_KBD_MIN_HOLD_MS):
             * taps quicker than that would pile up and reach the game long after
             * the last one.  A press of a key whose press still waits in the
             * queue is left out, and its release; the BIOS buffer keeps it. */
            if (event->type == SDL_KEYDOWN) {
                drop = raw_pending_make((uint8_t)(code & 0x7fu), grey);
                dropped[code] = (uint8_t)drop;
            } else {
                drop = dropped[code];
                dropped[code] = 0;
            }
            if (!drop) {
                /* the grey keys send E0 first: how a game tells them from the keypad */
                if (grey) raw_put(0xe0);
                raw_put(byte);
            }
            code &= 0x7fu;
            if (trace_keys()) fprintf(stderr, "KEY queue %s%02x%s\n", grey ? "e0 " : "", byte, drop ? " dropped" : "");
        }
        if (event->type == SDL_KEYDOWN) {
            const uint8_t ascii = key_ascii(event->key.keysym.sym, event->key.keysym.mod);
            if (code != 0 || ascii != 0) bios_put(code, ascii);
        }
    }
}

uint8_t Game_DosKbdPort(uint16_t port)
{
    uint8_t code;

    if (port == 0x64) return raw_read != raw_write ? 1 : 0;
    if (raw_get(&code)) {
        if (trace_keys()) fprintf(stderr, "KEY port60 %02x\n", code);
        return code;
    }
    if (trace_keys()) fprintf(stderr, "KEY port60 empty\n");
    return 0;
}

void Game_DosKbdDeliverIrq(void)
{
    /* by code (E0 keys | 80h): the last press, the last release */
    static uint32_t make_tick[256], break_tick[256];

    /* The handler starts with an ACTION checkpoint, which comes back here
     * before it reads port 60h: delivering again then would hand it an empty
     * port. */
    if (Game_GuestIsrRunning()) return;
    Game_DosKbdDrain();
    while (raw_read != raw_write) {
        const unsigned int before = raw_read;
        uint8_t code = raw_queue[raw_read];
        unsigned int key;
        uint32_t now;

        if (code == 0xe0u) {
            /* an E0 goes with the code after it, which may not be here yet */
            const unsigned int next = (raw_read + 1u) & (KBD_RAW_SIZE - 1u);
            if (next == raw_write) break;
            code = raw_queue[next];
            key = (code & 0x7fu) | 0x80u;
        } else {
            key = code & 0x7fu;
        }
        now = SDL_GetTicks();
        if (code & 0x80u) {
            if (now - make_tick[key] < min_hold_ms) break;
            break_tick[key] = now;
        } else {
            if (now - break_tick[key] < min_hold_ms) break;
            make_tick[key] = now;
        }
        if (trace_keys()) fprintf(stderr, "KEY irq %02x\n", raw_queue[raw_read]);
        /* One handler call takes one byte.  A handler that cannot run (IF
         * clear), or one that leaves port 60h alone (the null handler before
         * the game installs its own), would take this byte forever: drop it. */
        if (!Game_CallGuestInterrupt(0x09) || raw_read == before)
            raw_read = (raw_read + 1u) & (KBD_RAW_SIZE - 1u);
        if (Game_GuestIsrRunning()) break;
    }
}

static int kbhit(void)
{
    Game_DosKbdDrain();
    return ext_pending || bios_read != bios_write;
}

static uint8_t getch_blocking(void)
{
    bios_key key;

    if (ext_pending) {
        ext_pending = 0;
        return ext_scan;
    }
    while (!Thread_Exit) {
        Game_DosKbdDrain();
        if (bios_get(&key)) {
            if (trace_keys()) fprintf(stderr, "KEY getch %02x %02x\n", key.scan, key.ascii);
            if (key.ascii != 0) return key.ascii;
            ext_scan = key.scan;
            ext_pending = 1;
            return 0;
        }
        SDL_Delay(10);
    }
    return 0;
}

int Game_DosKbdInt16(Game_DosRegs *regs)
{
    bios_key key;

    switch ((regs->eax >> 8) & 0xffu) {
        case 0x00:
        case 0x10:
            key.scan = key.ascii = 0;
            while (!Thread_Exit) {
                Game_DosKbdDrain();
                if (bios_get(&key)) break;
                SDL_Delay(1);
            }
            if (trace_keys()) fprintf(stderr, "KEY int16 %02x %02x\n", key.scan, key.ascii);
            Game_DosSetAX(regs, (uint16_t)((key.scan << 8) | key.ascii));
            return 1;
        case 0x01:
        case 0x11:
            Game_DosKbdDrain();
            if (bios_read != bios_write) {
                key = bios_queue[bios_read];
                Game_DosSetAX(regs, (uint16_t)((key.scan << 8) | key.ascii));
                regs->eflags &= ~GAME_DOS_ZF;
            } else {
                regs->eflags |= GAME_DOS_ZF;
            }
            return 1;
        default:
            return 0;
    }
}

int Game_DosKbdInt21(Game_DosRegs *regs)
{
    switch ((regs->eax >> 8) & 0xffu) {
        case 0x08:
            Game_DosSetAL(regs, getch_blocking());
            break;
        case 0x0b:
            Game_DosSetAL(regs, kbhit() ? 0xffu : 0u);
            break;
        default:
            return 0;
    }
    Game_DosSetCarry(regs, 0);
    return 1;
}

int32_t Game_checkch(void)
{
    return kbhit() ? -1 : 0;
}

int32_t Game_getch(void)
{
    return getch_blocking();
}
