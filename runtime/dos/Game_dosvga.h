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

/* The VGA ports of a DOS game: the DAC (3C7h, 3C8h, 3C9h) and Input Status
 * #1 (3DAh). */

#ifdef __cplusplus
extern "C" {
#endif

/* IN from a VGA port: 1 and *value, or 0 if the port is not one of them. */
int Game_DosVgaIn(uint16_t port, uint32_t *value);
/* OUT to a VGA port: 1, or 0 if the port is not one of them.  A complete DAC
 * entry goes to the display (Set_Palette_Value). */
int Game_DosVgaOut(uint16_t port, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
