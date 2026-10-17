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

#ifndef _GAME_DOSKBD_H_INCLUDED_
#define _GAME_DOSKBD_H_INCLUDED_

#include <stdint.h>
#include "Game_dosfile.h"

/* The keyboard of a DOS4GW game: the controller's port 60h with its IRQ 1,
 * and the BIOS buffer behind INT 16h and INT 21h 08h/0Bh. */

#ifdef __cplusplus
extern "C" {
#endif

/* Empty queues; starts <GAME>_AUTO_KEYS (Game_keys.c). */
void Game_DosKbdInit(void);
/* Game thread: the key events of the event loop (Game_EventKQueue) into the
 * controller's queue (make and break codes, E0 before a grey key) and, for a
 * press, the BIOS buffer (scancode and ASCII). */
void Game_DosKbdDrain(void);
/* IN from port 60h (the next controller byte, 0 if none) or 64h (bit 0: a
 * byte is waiting). */
uint8_t Game_DosKbdPort(uint16_t port);
/* Runs the game's INT 9 handler once per waiting controller byte, until the
 * queue is empty or the handler cannot run.  A release is held back until
 * the press has been seen for a few frames. */
void Game_DosKbdDeliverIrq(void);
/* INT 16h AH = 00h/10h (wait for a key), 01h/11h (peek, ZF set if none), and
 * INT 21h AH = 08h (wait for a character, 0 then the scancode for a key
 * without one), 0Bh (AL = FFh if one is waiting): done, and 1 returned.
 * Anything else: 0, the registers untouched. */
int Game_DosKbdInt16(Game_DosRegs *regs);
int Game_DosKbdInt21(Game_DosRegs *regs);

/* WATCOM's kbhit and getch, for a game's external_procedures.sci to route
 * here (SR_checkch, SR_getch of its llasm glue): -1 if a key is waiting, else
 * 0; the next character, as INT 21h 08h gives it. */
int32_t Game_checkch(void);
int32_t Game_getch(void);

/* The game provides these (its interrupt vectors): run the guest's handler of
 * that vector as a hardware interrupt, 1 if it ran; whether a guest handler
 * is running now. */
int Game_CallGuestInterrupt(uint8_t vector);
int Game_GuestIsrRunning(void);

#ifdef __cplusplus
}
#endif

#endif
