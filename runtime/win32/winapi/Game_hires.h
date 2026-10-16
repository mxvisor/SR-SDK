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

#ifndef GAME_HIRES_H
#define GAME_HIRES_H

#include <stdint.h>

#define GAME_HIRES_MAX_FACTOR 6

#ifdef __cplusplus
extern "C" {
#endif

/* The factor of the enhanced 3D rendering, 0 when it is off: the config
 * file's Display_Enhanced_3D_Rendering and Display_3D_Factor, which
 * GAME_ENHANCED_3D overrides.  Valid once the config file is read. */
int Game_HiresFactor(void);
/* H's height, 240k (H is 4:3); valid after Game_HiresInit. */
int Game_HiresHeight(void);
/* Allocates the buffers; display/pc.c's Init_Display2 calls it when the
 * factor is not 0.  0 on success. */
int Game_HiresInit(void);
/* The flip with the enhanced 3D: src is the 320x200 VGA frame, dst
 * 320k x 240k pixels, palette the 256 colours as dst takes them. */
void Game_HiresFlip(const uint8_t *src, uint32_t *dst, const uint32_t *palette);

#ifdef __cplusplus
}
#endif

#endif
