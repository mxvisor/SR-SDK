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

/*
 *  MSVFW32 video compression manager: the one codec the game's movies need,
 *  Microsoft Video 1 (8-bit, 4x4 blocks), decoded to a bottom-up 8-bit DIB.
 */

#include "WinApi-game.h"
#include "Game_hooks.h"
#include "Game_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICERR_OK            0u
#define ICERR_UNSUPPORTED   0xffffffffu
#define ICERR_BADFORMAT     0xfffffffeu
#define ICERR_BADPARAM      0xfffffff9u

#define ICM_DECOMPRESS_GET_FORMAT 0x400au
#define ICM_DECOMPRESS_QUERY      0x400bu
#define ICM_DECOMPRESS_BEGIN      0x400cu
#define ICM_DECOMPRESS_END        0x400eu

#define IC_HANDLE 0x49430001u

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static int ic_debug(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = (getenv(GAME_ENV("DEBUG_AVI")) != NULL);
    return enabled;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static int is_msvideo1(uint32_t fourcc)
{
    switch (fourcc | 0x20202020u)       /* lower case */
    {
        case FOURCC('c', 'r', 'a', 'm'):
        case FOURCC('m', 's', 'v', 'c'):
        case FOURCC('w', 'h', 'a', 'm'):
            return 1;
    }
    return 0;
}

/* Microsoft Video 1, 8 bits per pixel. The picture is stored bottom row first, so the
 * output is written the way a bottom-up DIB is laid out. Skipped blocks keep what the
 * buffer holds from the previous frame. */
static void msvideo1_decode8(const uint8_t *in, uint32_t in_size, uint8_t *out, int width, int height)
{
    const uint8_t *end = in + in_size;
    int blocks_wide = width / 4;
    int blocks_high = height / 4;
    int total_blocks = blocks_wide * blocks_high;
    int stride = (width + 3) & ~3;
    int skip_blocks = 0;
    int block_y, block_x;

    for (block_y = 0; block_y < blocks_high; block_y++)
    {
        for (block_x = 0; block_x < blocks_wide; block_x++)
        {
            uint8_t *block = out + (size_t)block_y * 4 * stride + block_x * 4;
            unsigned byte_a, byte_b, flags;
            uint8_t colors[8];
            int x, y;

            if (skip_blocks != 0)
            {
                skip_blocks--;
                total_blocks--;
                continue;
            }
            if (end - in < 2) return;
            byte_a = *in++;
            byte_b = *in++;

            if (byte_a == 0 && byte_b == 0 && total_blocks == 0) return;

            if ((byte_b & 0xfc) == 0x84)
            {
                /* skip code, the current block is not counted */
                skip_blocks = ((byte_b - 0x84) << 8) + byte_a - 1;
            }
            else if (byte_b < 0x80)
            {
                /* two colors */
                if (end - in < 2) return;
                flags = (byte_b << 8) | byte_a;
                colors[0] = *in++;
                colors[1] = *in++;
                for (y = 0; y < 4; y++)
                    for (x = 0; x < 4; x++, flags >>= 1)
                        block[y * stride + x] = colors[(flags & 1) ^ 1];
            }
            else if (byte_b >= 0x90)
            {
                /* eight colors, a pair per 2x2 quadrant */
                if (end - in < 8) return;
                flags = (byte_b << 8) | byte_a;
                for (x = 0; x < 8; x++) colors[x] = *in++;
                for (y = 0; y < 4; y++)
                    for (x = 0; x < 4; x++, flags >>= 1)
                        block[y * stride + x] = colors[((y & 2) << 1) + (x & 2) + ((flags & 1) ^ 1)];
            }
            else
            {
                /* one color */
                for (y = 0; y < 4; y++)
                    for (x = 0; x < 4; x++)
                        block[y * stride + x] = (uint8_t)byte_a;
            }
            total_blocks--;
        }
    }
}

/* a0 = fccType, a1 = fccHandler, a2 = input BITMAPINFOHEADER, a3 = output header, a4 = mode */
uint32_t CCALL ICLocate_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t compression = (a2 != 0) ? rd32((const uint8_t *)GUEST_MEM(a2) + 16) : 0;
    uint32_t result = (is_msvideo1(a1) || is_msvideo1(compression)) ? IC_HANDLE : 0;
    (void)a0; (void)a3; (void)a4;
    if (ic_debug()) fprintf(stderr, "[ICLocate handler=%08x compression=%08x -> %x]\n", a1, compression, result);
    return result;
}

/* a0 = HIC, a1 = message, a2/a3 = its parameters */
uint32_t CCALL ICSendMessage_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t result = ICERR_UNSUPPORTED;
    if (a0 != IC_HANDLE) return ICERR_BADPARAM;

    switch (a1)
    {
        case ICM_DECOMPRESS_GET_FORMAT:
        {
            /* the format of what ICDecompress produces: the input header, uncompressed, with its palette */
            const uint8_t *in;
            uint32_t header_size, colors, size;
            if (a2 == 0) { result = ICERR_BADPARAM; break; }
            in = (const uint8_t *)GUEST_MEM(a2);
            header_size = rd32(in);
            colors = rd32(in + 32);
            if (colors == 0 || colors > 256) colors = 256;
            size = header_size + colors * 4;
            if (a3 == 0) { result = size; break; }
            {
                uint8_t *out = (uint8_t *)GUEST_MEM(a3);
                int32_t width = (int32_t)rd32(in + 4);
                int32_t height = (int32_t)rd32(in + 8);
                uint32_t stride = ((uint32_t)width + 3) & ~3u;
                memcpy(out, in, size);
                memset(out + 16, 0, 4);                                         /* biCompression = BI_RGB */
                *(uint32_t *)(out + 20) = stride * (uint32_t)(height < 0 ? -height : height);   /* biSizeImage */
            }
            result = ICERR_OK;
            break;
        }
        case ICM_DECOMPRESS_QUERY:
        case ICM_DECOMPRESS_BEGIN:
        case ICM_DECOMPRESS_END:
            result = ICERR_OK;
            break;
    }

    if (ic_debug()) fprintf(stderr, "[ICSendMessage msg=%x %x %x -> %x]\n", a1, a2, a3, result);
    return result;
}

/* a0 = HIC, a1 = flags, a2 = input format, a3 = compressed data, a4 = output format, a5 = output pixels */
uint32_t CCALL ICDecompress_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    const uint8_t *format;
    uint32_t size;
    int32_t width, height;
    (void)a1; (void)a4;

    if (a0 != IC_HANDLE || a2 == 0 || a3 == 0 || a5 == 0) return ICERR_BADPARAM;
    format = (const uint8_t *)GUEST_MEM(a2);
    width = (int32_t)rd32(format + 4);
    height = (int32_t)rd32(format + 8);
    if (height < 0) height = -height;
    size = rd32(format + 20);
    if (size == 0) size = 0x100000;
    if (rd32(format + 14) >> 16 != 0 && (rd32(format + 12) >> 16) != 8) return ICERR_BADFORMAT;
    if (width <= 0 || height <= 0) return ICERR_BADFORMAT;

    msvideo1_decode8((const uint8_t *)GUEST_MEM(a3), size, (uint8_t *)GUEST_MEM(a5), width, height);
    return ICERR_OK;
}

uint32_t CCALL ICClose_c(uint32_t a0)
{
    (void)a0;
    return ICERR_OK;
}
