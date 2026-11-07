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


/* The GDI of a game that draws into DIB sections (GAME_WINAPI_GDI 1 in
 * Game_hooks.h): WinApi-gdi.c.  BitBlt, CreateCompatibleDC, DeleteDC,
 * DeleteObject, SelectObject and the text functions keep the prototypes of
 * WinApi-gdi32.h. */

#if !defined(_WINAPI_GDI_H_INCLUDED_)
#define _WINAPI_GDI_H_INCLUDED_

#include <stdint.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL GetDC_c(uint32_t hwnd);
uint32_t CCALL ReleaseDC_c(uint32_t hwnd, uint32_t hdc);
uint32_t CCALL BeginPaint_c(uint32_t hwnd, uint32_t ps);
uint32_t CCALL EndPaint_c(uint32_t hwnd, uint32_t ps);
uint32_t CCALL GetObjectA_c(uint32_t h, uint32_t size, uint32_t buffer);
uint32_t CCALL CreateDIBSection_c(uint32_t hdc, uint32_t pbmi, uint32_t usage, uint32_t ppvBits, uint32_t hSection, uint32_t offset);
uint32_t CCALL CreateCompatibleBitmap_c(uint32_t hdc, uint32_t width, uint32_t height);
uint32_t CCALL SetDIBColorTable_c(uint32_t hdc, uint32_t start, uint32_t count, uint32_t colors);
uint32_t CCALL SetDIBits_c(uint32_t hdc, uint32_t hbm, uint32_t start, uint32_t lines, uint32_t pbits, uint32_t pbmi, uint32_t usage);
uint32_t CCALL SetStretchBltMode_c(uint32_t hdc, uint32_t mode);
uint32_t CCALL GdiFlush_c(void);
uint32_t CCALL StretchBlt_c(uint32_t hdcDest, uint32_t xd, uint32_t yd, uint32_t wd, uint32_t hd,
                            uint32_t hdcSrc, uint32_t xs, uint32_t ys, uint32_t ws, uint32_t hs, uint32_t rop);
uint32_t CCALL LoadBitmapA_c(uint32_t hInstance, uint32_t lpBitmapName);
uint32_t CCALL CreatePalette_c(uint32_t logpal);
uint32_t CCALL SelectPalette_c(uint32_t hdc, uint32_t hpal, uint32_t background);
uint32_t CCALL RealizePalette_c(uint32_t hdc);
uint32_t CCALL AnimatePalette_c(uint32_t hpal, uint32_t start, uint32_t count, uint32_t entries);
uint32_t CCALL GetSystemPaletteEntries_c(uint32_t hdc, uint32_t start, uint32_t count, uint32_t entries);
uint32_t CCALL GetSystemPaletteUse_c(uint32_t hdc);
uint32_t CCALL SetSystemPaletteUse_c(uint32_t hdc, uint32_t use);
uint32_t CCALL GetDeviceCaps_c(uint32_t hdc, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
