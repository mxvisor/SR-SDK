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

#ifndef _GAME_DOSVGA_H_INCLUDED_
#define _GAME_DOSVGA_H_INCLUDED_

#include <stdint.h>
#include "Game_dosregs.h"

/* The VGA of a DOS game: the DAC (3C7h, 3C8h, 3C9h), Input Status #1 (3DAh),
 * the sequencer's Map Mask (3C4h/3C5h), the graphics controller (3CEh/3CFh),
 * the CRTC's standard registers (3D4h/3D5h), mode 12h, 640x480 in 16 colours on
 * four planes, mode 13h unchained ("mode X", 256 colours on the four planes),
 * and the VESA BIOS (VBE 1.2) with its 256-colour modes, banked through the
 * window at A0000h (Game_dosvga.c). */

#ifdef __cplusplus
extern "C" {
#endif

/* IN from a VGA port: 1 and *value, or 0 if the port is not one of them. */
int Game_DosVgaIn(uint16_t port, uint32_t *value);
/* OUT to a VGA port: 1, or 0 if the port is not one of them.  A complete DAC
 * entry goes to the display (Set_Palette_Value). */
int Game_DosVgaOut(uint16_t port, uint8_t value);
/* INT 10h AH=00h: the planes of mode 12h, or none for the other modes; clear:
 * bit 7 of AL was not set (the planes and the VGA window are cleared) */
void Game_DosVgaSetMode(uint8_t mode, int clear);
/* INT 10h AX=1000h/1007h: a palette register of the attribute controller */
void Game_DosVgaSetAttribute(uint8_t index, uint8_t value);
uint8_t Game_DosVgaAttribute(uint8_t index);
/* INT 10h AH=4Fh, the VESA BIOS: 1 if served (AX = 004Fh, or 014Fh for a
 * function or mode it lacks).  buffer: the DOS address of ES:DI (a real-mode
 * call through DPMI 0300h) or the flat EDI, the block 4F00h and 4F01h fill. */
int Game_DosVbe(Game_DosRegs *regs, uint32_t buffer);
/* The pixels of a video mode (a VGA mode of AH=00h, or a VESA mode of
 * vbe_modes): 1 and *width, *height, or 0 if it is not known; text modes count
 * eight pixels a column and a row. */
int Game_DosVgaModeSize(uint16_t mode, int *width, int *height);
/* rep movsb of count bytes from source to destination (DOS addresses): in mode
 * 13h unchained with a latch copy set up (write mode 1, or write mode 0 with a
 * Bit Mask of 0) and both in the VGA window, each plane of the Map Mask from
 * itself, as the VGA copies through its latches.  The game's
 * instruction_replacements.sci calls it in place of the copy.  Returns count. */
uint32_t Game_DosVgaCopy(uint32_t destination, uint32_t source, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
