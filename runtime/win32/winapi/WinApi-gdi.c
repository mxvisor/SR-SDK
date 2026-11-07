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

/* The GDI of a game that draws its picture into a DIB section and copies it
 * to its window with BitBlt or StretchBlt (GAME_WINAPI_GDI 1 in Game_hooks.h).
 *
 * Device contexts, bitmaps, palettes and fonts are handles of this file.  The
 * pixels of a bitmap are in the guest's memory, where the game draws them; a
 * DIB keeps its rows bottom-up or top-down as its BITMAPINFO says.  The display
 * is a 256-color one with a palette, as Windows 95 in 256 colors, the mode such
 * a game was made for.  The window's device context is a picture of the window's
 * client size in RGBA: a blit into it converts the source through its colors
 * and shows the picture through OpenGL, with the port's filter, in a window of
 * its own (as the DirectDraw layer does). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"

#if GAME_WINAPI_GDI

#include <SDL.h>
#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "Game_ui.h"
#include "Game_osd.h"
#include "Game_gl.h"
#include "WinApi.h"
#include "WinApi-gdi.h"
#include "WinApi-gdi32.h"
#include "WinApi-user32.h"

/* the size of the window's client area: the display mode the game asks for */
#ifndef GAME_WINDOW_WIDTH
#define GAME_WINDOW_WIDTH 640
#endif
#ifndef GAME_WINDOW_HEIGHT
#define GAME_WINDOW_HEIGHT 480
#endif

#define GDI_LOG(...) do { if (gdi_log) fprintf(stderr, "gdi: " __VA_ARGS__); } while (0)

#define GDI_MAX_OBJECTS 256
#define GDI_HANDLE_BASE 0x6a000000u

enum { GDI_FREE, GDI_DC, GDI_BITMAP, GDI_PALETTE, GDI_FONT };

typedef struct {
    int type;

    /* a device context */
    int window;             /* the window's DC (GetDC, BeginPaint) */
    uint32_t bitmap, palette, font;
    int32_t stretch_mode, bk_mode;
    uint32_t text_color;

    /* a bitmap */
    int width, height, bpp, stride;
    int bottom_up;          /* the first row in memory is the bottom one */
    int pal_colors;         /* its colors are indices into the DC's palette (DIB_PAL_COLORS, a device bitmap) */
    uint32_t bits;          /* the guest's address of the pixels */
    uint8_t colors[256][4]; /* RGBQUAD: blue, green, red, reserved */
    uint16_t indices[256];  /* DIB_PAL_COLORS */

    /* a palette */
    int count;
    uint8_t entries[256][4];    /* PALETTEENTRY: red, green, blue, flags */

    /* a font */
    uint8_t logfont[60];
} gdi_object;

static gdi_object objects[GDI_MAX_OBJECTS];
static int gdi_log = -1;

/* the palette of the display: what RealizePalette put there */
static uint8_t system_palette[256][4];
static int system_palette_use = 1;              /* SYSPAL_STATIC */

/* the window: its picture in RGBA and how it gets to the screen */
static struct {
    SDL_Window *window;
    SDL_GLContext glctx;
    GLuint gltex;
    int width, height;
    uint32_t *pixels;
    uint32_t dc;
} screen;

static void log_init(void)
{
    if (gdi_log < 0) gdi_log = (getenv(GAME_ENV("GDI_LOG")) != NULL);
}

static uint32_t object_new(int type)
{
    int i;

    for (i = 1; i < GDI_MAX_OBJECTS; i++)
    {
        if (objects[i].type == GDI_FREE)
        {
            memset(&objects[i], 0, sizeof(objects[i]));
            objects[i].type = type;
            return GDI_HANDLE_BASE + (uint32_t)i * 16;
        }
    }
    fprintf(stderr, "gdi: out of objects\n");
    return 0;
}

static gdi_object *object_get(uint32_t handle, int type)
{
    uint32_t i;

    if (handle < GDI_HANDLE_BASE || ((handle - GDI_HANDLE_BASE) & 15)) return NULL;
    i = (handle - GDI_HANDLE_BASE) / 16;
    if (i >= GDI_MAX_OBJECTS || objects[i].type == GDI_FREE) return NULL;
    if (type != GDI_FREE && objects[i].type != type) return NULL;
    return &objects[i];
}

/* --- the window --- */

static void screen_present(void)
{
    if (screen.gltex == 0) return;
    Game_OsdFrame(screen.width, screen.height, "OpenGL");
    Game_GlPresentAspect(screen.window, screen.gltex, screen.width, screen.height, 4, 3, 1, 0, Game_GlFilter());
}

static void screen_open(void)
{
    if (screen.pixels != NULL) return;

    screen.width = GAME_WINDOW_WIDTH;
    screen.height = GAME_WINDOW_HEIGHT;
    screen.pixels = (uint32_t *)calloc((size_t)screen.width * screen.height, 4);

    /* the port's window stays hidden: a Win32 game opens its own */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "gdi: SDL video initialization failed: %s\n", SDL_GetError());
        return;
    }
    screen.window = Game_WindowCreate(screen.width, screen.height, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (screen.window == NULL)
    {
        fprintf(stderr, "gdi: the window: %s\n", SDL_GetError());
        return;
    }
    /* the window menu holds the game while it is open and shows the picture again */
    Game_UiSetRepaint(screen_present);

    /* without OpenGL it says so and ends the program */
    screen.glctx = Game_GlCreateContext(screen.window, 1);
    glGenTextures(1, &screen.gltex);
    glBindTexture(GL_TEXTURE_2D, screen.gltex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    Game_GlTexImageBgra(0, screen.width, screen.height, NULL);
    Game_ShowWindow(screen.window);
}

static void screen_update(void)
{
    if (screen.gltex == 0) return;
    glBindTexture(GL_TEXTURE_2D, screen.gltex);
    Game_GlTexSubImageBgra(screen.width, screen.height, screen.pixels);
    screen_present();
}

static uint32_t window_dc(void)
{
    if (screen.dc == 0)
    {
        screen.dc = object_new(GDI_DC);
        if (screen.dc != 0) object_get(screen.dc, GDI_DC)->window = 1;
    }
    screen_open();
    return screen.dc;
}

/* --- bitmaps --- */

static uint8_t *bitmap_row(gdi_object *bm, int y)
{
    uint8_t *bits = (uint8_t *)GUEST_MEM(bm->bits);

    if (bits == NULL || y < 0 || y >= bm->height) return NULL;
    return bits + (size_t)(bm->bottom_up ? (bm->height - 1 - y) : y) * bm->stride;
}

static uint32_t bitmap_new(int width, int height, int bpp, int bottom_up)
{
    uint32_t handle;
    gdi_object *bm;
    void *bits;

    if (width <= 0 || height <= 0 || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) return 0;
    handle = object_new(GDI_BITMAP);
    if (handle == 0) return 0;
    bm = object_get(handle, GDI_BITMAP);
    bm->width = width;
    bm->height = height;
    bm->bpp = bpp;
    bm->stride = ((width * bpp + 31) / 32) * 4;
    bm->bottom_up = bottom_up;
    bits = x86_malloc((unsigned int)bm->stride * (unsigned int)height);
    if (bits == NULL)
    {
        bm->type = GDI_FREE;
        return 0;
    }
    memset(bits, 0, (size_t)bm->stride * height);
    bm->bits = GUEST_ADDR(bits);
    return handle;
}

/* the colors of a BITMAPINFO into a bitmap: RGBQUADs, or the WORD indices of DIB_PAL_COLORS */
static void bitmap_colors(gdi_object *bm, const uint8_t *bmi, uint32_t usage)
{
    uint32_t header, used, i;

    if (bm->bpp != 8) return;
    memcpy(&header, bmi, 4);
    memcpy(&used, bmi + 32, 4);
    if (used == 0 || used > 256) used = 256;
    bm->pal_colors = (usage == 1);
    for (i = 0; i < used; i++)
    {
        if (bm->pal_colors) memcpy(&bm->indices[i], bmi + header + i * 2, 2);
        else memcpy(bm->colors[i], bmi + header + i * 4, 4);
    }
}

/* the color of an 8-bit pixel of a bitmap, as BGRA */
static void bitmap_palette(gdi_object *bm, gdi_object *dc, uint32_t out[256])
{
    gdi_object *pal = (dc != NULL) ? object_get(dc->palette, GDI_PALETTE) : NULL;
    int i;

    for (i = 0; i < 256; i++)
    {
        const uint8_t *e;

        if (bm->pal_colors)
        {
            int index = bm->indices[i];
            e = (pal != NULL && index < pal->count) ? pal->entries[index] : system_palette[index & 255];
            out[i] = 0xff000000u | ((uint32_t)e[0] << 16) | ((uint32_t)e[1] << 8) | e[2];
        }
        else
        {
            e = bm->colors[i];
            out[i] = 0xff000000u | ((uint32_t)e[2] << 16) | ((uint32_t)e[1] << 8) | e[0];
        }
    }
}

static uint32_t bitmap_pixel_bgra(gdi_object *bm, const uint8_t *row, int x, const uint32_t pal[256])
{
    switch (bm->bpp)
    {
        case 8: return pal[row[x]];
        case 16:
        {
            uint16_t p = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8));    /* 5-5-5 */
            return 0xff000000u | ((uint32_t)((p >> 10) & 31) * 255 / 31 << 16) |
                   ((uint32_t)((p >> 5) & 31) * 255 / 31 << 8) | ((uint32_t)(p & 31) * 255 / 31);
        }
        case 24: return 0xff000000u | ((uint32_t)row[x * 3 + 2] << 16) | ((uint32_t)row[x * 3 + 1] << 8) | row[x * 3];
        default: return 0xff000000u | ((uint32_t)row[x * 4 + 2] << 16) | ((uint32_t)row[x * 4 + 1] << 8) | row[x * 4];
    }
}

/* --- blits --- */

static int blit(uint32_t hdcDest, int32_t xd, int32_t yd, int32_t wd, int32_t hd,
                uint32_t hdcSrc, int32_t xs, int32_t ys, int32_t ws, int32_t hs, uint32_t rop)
{
    gdi_object *dst = object_get(hdcDest, GDI_DC), *src = object_get(hdcSrc, GDI_DC);
    gdi_object *sbm, *dbm;
    int x, y;

    if (dst == NULL || wd == 0 || hd == 0) return 0;
    if (rop != 0x00cc0020u)             /* SRCCOPY */
    {
        static int told;
        if (!told) { told = 1; fprintf(stderr, "gdi: raster operation 0x%x drawn as SRCCOPY\n", rop); }
    }
    sbm = (src != NULL) ? object_get(src->bitmap, GDI_BITMAP) : NULL;
    if (sbm == NULL) return 0;
    if (ws == 0 || hs == 0) return 1;

    if (dst->window)
    {
        uint32_t pal[256];

        if (screen.pixels == NULL) return 0;
        bitmap_palette(sbm, src, pal);
        for (y = 0; y < (hd < 0 ? -hd : hd); y++)
        {
            int dy = yd + y, sy;
            const uint8_t *row;

            if (dy < 0 || dy >= screen.height) continue;
            sy = ys + (int)((int64_t)y * hs / hd);
            row = bitmap_row(sbm, sy);
            if (row == NULL) continue;
            for (x = 0; x < (wd < 0 ? -wd : wd); x++)
            {
                int dx = xd + x, sx = xs + (int)((int64_t)x * ws / wd);
                if (dx < 0 || dx >= screen.width || sx < 0 || sx >= sbm->width) continue;
                screen.pixels[(size_t)dy * screen.width + dx] = bitmap_pixel_bgra(sbm, row, sx, pal);
            }
        }
        screen_update();
        return 1;
    }

    dbm = object_get(dst->bitmap, GDI_BITMAP);
    if (dbm == NULL || dbm->bpp != sbm->bpp) return 0;
    for (y = 0; y < hd; y++)
    {
        int sy = ys + (int)((int64_t)y * hs / hd);
        uint8_t *drow = bitmap_row(dbm, yd + y);
        const uint8_t *srow = bitmap_row(sbm, sy);
        int bytes = sbm->bpp / 8;

        if (drow == NULL || srow == NULL) continue;
        for (x = 0; x < wd; x++)
        {
            int dx = xd + x, sx = xs + (int)((int64_t)x * ws / wd);
            if (dx < 0 || dx >= dbm->width || sx < 0 || sx >= sbm->width) continue;
            memcpy(drow + dx * bytes, srow + sx * bytes, bytes);
        }
    }
    return 1;
}

#ifdef __cplusplus
extern "C" {
#endif

/* --- device contexts --- */

void * CCALL CreateCompatibleDC_c(void *hdc)
{
    uint32_t handle;
    gdi_object *dc;

    log_init();
    (void)hdc;
    handle = object_new(GDI_DC);
    dc = object_get(handle, GDI_DC);
    if (dc != NULL) dc->stretch_mode = 1;
    GDI_LOG("CreateCompatibleDC -> %08x\n", handle);
    return GUEST_MEM(handle);
}

uint32_t CCALL DeleteDC_c(void *hdc)
{
    gdi_object *dc = object_get(GUEST_ADDR(hdc), GDI_DC);

    if (dc == NULL || dc->window) return dc != NULL;
    dc->type = GDI_FREE;
    return 1;
}

uint32_t CCALL GetDC_c(uint32_t hwnd)
{
    log_init();
    (void)hwnd;
    return window_dc();
}

uint32_t CCALL ReleaseDC_c(uint32_t hwnd, uint32_t hdc)
{
    (void)hwnd; (void)hdc;
    return 1;
}

/* PAINTSTRUCT: hdc, fErase, rcPaint, fRestore, fIncUpdate, rgbReserved[32] */
uint32_t CCALL BeginPaint_c(uint32_t hwnd, uint32_t ps)
{
    uint32_t dc = window_dc();

    (void)hwnd;
    if (ps != 0)
    {
        uint32_t *p = (uint32_t *)GUEST_MEM(ps);
        memset(p, 0, 64);
        p[0] = dc;
        p[4] = (uint32_t)screen.width;
        p[5] = (uint32_t)screen.height;
    }
    return dc;
}

uint32_t CCALL EndPaint_c(uint32_t hwnd, uint32_t ps)
{
    (void)hwnd; (void)ps;
    return 1;
}

void * CCALL SelectObject_c(void *hdc, void *hgdiobj)
{
    gdi_object *dc = object_get(GUEST_ADDR(hdc), GDI_DC);
    uint32_t handle = GUEST_ADDR(hgdiobj), old = 0;
    gdi_object *obj = object_get(handle, GDI_FREE);

    if (dc == NULL || obj == NULL) return NULL;
    switch (obj->type)
    {
        case GDI_BITMAP: old = dc->bitmap; dc->bitmap = handle; break;
        case GDI_FONT: old = dc->font; dc->font = handle; break;
        case GDI_PALETTE: old = dc->palette; dc->palette = handle; break;
        default: return NULL;
    }
    GDI_LOG("SelectObject(%08x, %08x) -> %08x\n", GUEST_ADDR(hdc), handle, old);
    return GUEST_MEM(old);
}

uint32_t CCALL DeleteObject_c(void *hObject)
{
    gdi_object *obj = object_get(GUEST_ADDR(hObject), GDI_FREE);

    if (obj == NULL || obj->type == GDI_DC) return 0;
    if (obj->type == GDI_BITMAP && obj->bits != 0) x86_free(GUEST_MEM(obj->bits));
    obj->type = GDI_FREE;
    return 1;
}

/* BITMAP, and DIBSECTION for a buffer that big */
uint32_t CCALL GetObjectA_c(uint32_t h, uint32_t size, uint32_t buffer)
{
    gdi_object *obj = object_get(h, GDI_FREE);
    uint8_t out[84];
    int32_t v;
    uint16_t w;

    if (obj == NULL) return 0;
    memset(out, 0, sizeof(out));
    switch (obj->type)
    {
        case GDI_BITMAP:
            v = obj->width; memcpy(out + 4, &v, 4);
            v = obj->height; memcpy(out + 8, &v, 4);
            v = obj->stride; memcpy(out + 12, &v, 4);
            w = 1; memcpy(out + 16, &w, 2);
            w = (uint16_t)obj->bpp; memcpy(out + 18, &w, 2);
            memcpy(out + 20, &obj->bits, 4);
            /* the BITMAPINFOHEADER of a DIBSECTION */
            v = 40; memcpy(out + 24, &v, 4);
            v = obj->width; memcpy(out + 28, &v, 4);
            v = obj->bottom_up ? obj->height : -obj->height; memcpy(out + 32, &v, 4);
            w = 1; memcpy(out + 36, &w, 2);
            w = (uint16_t)obj->bpp; memcpy(out + 38, &w, 2);
            v = obj->stride * obj->height; memcpy(out + 44, &v, 4);
            if (size > 84) size = 84;
            if (size < 24 && buffer != 0) return 0;
            break;
        case GDI_PALETTE:
            w = (uint16_t)obj->count; memcpy(out, &w, 2);
            if (size > 2) size = 2;
            break;
        case GDI_FONT:
            memcpy(out, obj->logfont, 60);
            if (size > 60) size = 60;
            break;
        default:
            return 0;
    }
    if (buffer == 0) return obj->type == GDI_BITMAP ? 84 : obj->type == GDI_PALETTE ? 2 : 60;
    memcpy(GUEST_MEM(buffer), out, size);
    return size;
}

/* --- bitmaps --- */

uint32_t CCALL CreateDIBSection_c(uint32_t hdc, uint32_t pbmi, uint32_t usage, uint32_t ppvBits, uint32_t hSection, uint32_t offset)
{
    const uint8_t *bmi = (const uint8_t *)GUEST_MEM(pbmi);
    int32_t width, height;
    uint16_t bpp;
    uint32_t handle;
    gdi_object *bm;

    log_init();
    (void)hdc; (void)hSection; (void)offset;
    if (bmi == NULL) return 0;
    memcpy(&width, bmi + 4, 4);
    memcpy(&height, bmi + 8, 4);
    memcpy(&bpp, bmi + 14, 2);
    handle = bitmap_new(width, height < 0 ? -height : height, bpp, height > 0);
    bm = object_get(handle, GDI_BITMAP);
    if (bm == NULL) return 0;
    bitmap_colors(bm, bmi, usage);
    if (ppvBits != 0) *(uint32_t *)GUEST_MEM(ppvBits) = bm->bits;
    GDI_LOG("CreateDIBSection(%ix%i, %i bpp, usage %u) -> %08x, bits %08x\n", width, height, bpp, usage, handle, bm->bits);
    return handle;
}

/* a bitmap of the display: 8 bits, its colors the display's palette */
uint32_t CCALL CreateCompatibleBitmap_c(uint32_t hdc, uint32_t width, uint32_t height)
{
    uint32_t handle = bitmap_new((int32_t)width, (int32_t)height, 8, 0);
    gdi_object *bm = object_get(handle, GDI_BITMAP);
    int i;

    (void)hdc;
    if (bm == NULL) return 0;
    bm->pal_colors = 1;
    for (i = 0; i < 256; i++) bm->indices[i] = (uint16_t)i;
    GDI_LOG("CreateCompatibleBitmap(%ux%u) -> %08x\n", width, height, handle);
    return handle;
}

uint32_t CCALL SetDIBColorTable_c(uint32_t hdc, uint32_t start, uint32_t count, uint32_t colors)
{
    gdi_object *dc = object_get(hdc, GDI_DC);
    gdi_object *bm = (dc != NULL) ? object_get(dc->bitmap, GDI_BITMAP) : NULL;
    const uint8_t *in = (const uint8_t *)GUEST_MEM(colors);
    uint32_t i;

    if (bm == NULL || in == NULL || bm->bpp != 8) return 0;
    for (i = 0; i < count && start + i < 256; i++) memcpy(bm->colors[start + i], in + i * 4, 4);
    bm->pal_colors = 0;
    return i;
}

/* the rows of a DIB into a bitmap */
uint32_t CCALL SetDIBits_c(uint32_t hdc, uint32_t hbm, uint32_t start, uint32_t lines, uint32_t pbits, uint32_t pbmi, uint32_t usage)
{
    gdi_object *bm = object_get(hbm, GDI_BITMAP);
    const uint8_t *bmi = (const uint8_t *)GUEST_MEM(pbmi);
    const uint8_t *bits = (const uint8_t *)GUEST_MEM(pbits);
    int32_t width, height;
    uint16_t bpp;
    int stride;
    uint32_t i;

    (void)hdc; (void)usage;
    if (bm == NULL || bmi == NULL || bits == NULL) return 0;
    memcpy(&width, bmi + 4, 4);
    memcpy(&height, bmi + 8, 4);
    memcpy(&bpp, bmi + 14, 2);
    if (bpp != bm->bpp) return 0;
    stride = ((width * bpp + 31) / 32) * 4;
    for (i = 0; i < lines; i++)
    {
        /* scan line start + i of a bottom-up DIB is counted from the bottom */
        int y = (height > 0) ? bm->height - 1 - (int)(start + i) : (int)(start + i);
        uint8_t *row = bitmap_row(bm, y);
        int n = (width < bm->width ? width : bm->width) * bpp / 8;

        if (row != NULL) memcpy(row, bits + (size_t)i * stride, n);
    }
    return lines;
}

uint32_t CCALL SetStretchBltMode_c(uint32_t hdc, uint32_t mode)
{
    gdi_object *dc = object_get(hdc, GDI_DC);
    uint32_t old;

    if (dc == NULL) return 0;
    old = dc->stretch_mode ? (uint32_t)dc->stretch_mode : 1;
    dc->stretch_mode = (int32_t)mode;
    return old;
}

uint32_t CCALL GdiFlush_c(void)
{
    return 1;
}

uint32_t CCALL BitBlt_c(void *hdcDest, int32_t nXDest, int32_t nYDest, int32_t nWidth, int32_t nHeight, void *hdcSrc, int32_t nXSrc, int32_t nYSrc, uint32_t dwRop)
{
    return blit(GUEST_ADDR(hdcDest), nXDest, nYDest, nWidth, nHeight, GUEST_ADDR(hdcSrc), nXSrc, nYSrc, nWidth, nHeight, dwRop);
}

uint32_t CCALL StretchBlt_c(uint32_t hdcDest, uint32_t xd, uint32_t yd, uint32_t wd, uint32_t hd,
                            uint32_t hdcSrc, uint32_t xs, uint32_t ys, uint32_t ws, uint32_t hs, uint32_t rop)
{
    return blit(hdcDest, (int32_t)xd, (int32_t)yd, (int32_t)wd, (int32_t)hd, hdcSrc, (int32_t)xs, (int32_t)ys, (int32_t)ws, (int32_t)hs, rop);
}

/* RT_BITMAP: a DIB without its file header, as an 8-bit (or wider) bitmap */
uint32_t CCALL LoadBitmapA_c(uint32_t hInstance, uint32_t lpBitmapName)
{
    uint32_t size = 0, handle, header;
    uint8_t *data;
    int32_t width, height;
    uint16_t bpp;
    gdi_object *bm;

    (void)hInstance;
    data = Winapi_LoadResource(2 /* RT_BITMAP */, lpBitmapName, &size);
    if (data == NULL || size < 40) { free(data); return 0; }
    memcpy(&header, data, 4);
    memcpy(&width, data + 4, 4);
    memcpy(&height, data + 8, 4);
    memcpy(&bpp, data + 14, 2);
    handle = bitmap_new(width, height < 0 ? -height : height, bpp, height > 0);
    bm = object_get(handle, GDI_BITMAP);
    if (bm != NULL)
    {
        uint32_t used, colors;

        memcpy(&used, data + 32, 4);
        colors = (bpp <= 8) ? (used ? used : (1u << bpp)) : 0;
        bitmap_colors(bm, data, 0);
        if (header + colors * 4 + (uint32_t)bm->stride * bm->height <= size)
        {
            memcpy(GUEST_MEM(bm->bits), data + header + colors * 4, (size_t)bm->stride * bm->height);
        }
    }
    free(data);
    return handle;
}

/* --- palettes --- */

uint32_t CCALL CreatePalette_c(uint32_t logpal)
{
    const uint8_t *in = (const uint8_t *)GUEST_MEM(logpal);
    uint32_t handle;
    gdi_object *pal;
    uint16_t count;

    log_init();
    if (in == NULL) return 0;
    handle = object_new(GDI_PALETTE);
    pal = object_get(handle, GDI_PALETTE);
    if (pal == NULL) return 0;
    memcpy(&count, in + 2, 2);
    pal->count = (count > 256) ? 256 : count;
    memcpy(pal->entries, in + 4, (size_t)pal->count * 4);
    GDI_LOG("CreatePalette(%i) -> %08x\n", pal->count, handle);
    return handle;
}

uint32_t CCALL SelectPalette_c(uint32_t hdc, uint32_t hpal, uint32_t background)
{
    gdi_object *dc = object_get(hdc, GDI_DC);
    uint32_t old;

    (void)background;
    if (dc == NULL || object_get(hpal, GDI_PALETTE) == NULL) return 0;
    old = dc->palette;
    dc->palette = hpal;
    return old;
}

/* the DC's palette becomes the display's */
uint32_t CCALL RealizePalette_c(uint32_t hdc)
{
    gdi_object *dc = object_get(hdc, GDI_DC);
    gdi_object *pal = (dc != NULL) ? object_get(dc->palette, GDI_PALETTE) : NULL;

    if (pal == NULL) return 0;
    memcpy(system_palette, pal->entries, (size_t)pal->count * 4);
    return (uint32_t)pal->count;
}

uint32_t CCALL AnimatePalette_c(uint32_t hpal, uint32_t start, uint32_t count, uint32_t entries)
{
    gdi_object *pal = object_get(hpal, GDI_PALETTE);
    const uint8_t *in = (const uint8_t *)GUEST_MEM(entries);
    uint32_t i;

    if (pal == NULL || in == NULL) return 0;
    for (i = 0; i < count && start + i < (uint32_t)pal->count; i++)
    {
        memcpy(pal->entries[start + i], in + i * 4, 4);
        memcpy(system_palette[start + i], in + i * 4, 4);
    }
    return 1;
}

uint32_t CCALL GetSystemPaletteEntries_c(uint32_t hdc, uint32_t start, uint32_t count, uint32_t entries)
{
    uint32_t i;

    (void)hdc;
    if (entries == 0) return 256;
    for (i = 0; i < count && start + i < 256; i++) memcpy((uint8_t *)GUEST_MEM(entries) + i * 4, system_palette[start + i], 4);
    return i;
}

uint32_t CCALL GetSystemPaletteUse_c(uint32_t hdc)
{
    (void)hdc;
    return (uint32_t)system_palette_use;
}

uint32_t CCALL SetSystemPaletteUse_c(uint32_t hdc, uint32_t use)
{
    uint32_t old = (uint32_t)system_palette_use;

    (void)hdc;
    system_palette_use = (int)use;
    return old;
}

/* a 256-color display with a palette, the window's size */
uint32_t CCALL GetDeviceCaps_c(uint32_t hdc, uint32_t index)
{
    (void)hdc;
    switch (index)
    {
        case 2: return 1;                       /* TECHNOLOGY: DT_RASDISPLAY */
        case 8: return GAME_WINDOW_WIDTH;       /* HORZRES */
        case 10: return GAME_WINDOW_HEIGHT;     /* VERTRES */
        case 12: return 8;                      /* BITSPIXEL */
        case 14: return 1;                      /* PLANES */
        case 24: return 20;                     /* NUMCOLORS: the static colors */
        case 38: return 0x0001 | 0x0008 | 0x0080 | 0x0100 | 0x0200 | 0x0800;    /* RASTERCAPS: BITBLT, BITMAP64, DI_BITMAP, PALETTE, DIBTODEV, STRETCHBLT */
        case 88: case 90: return 96;            /* LOGPIXELSX, LOGPIXELSY */
        case 104: return 256;                   /* SIZEPALETTE */
        case 106: return 20;                    /* NUMRESERVED */
        case 108: return 18;                    /* COLORRES */
        default: return 0;
    }
}

/* --- text: kept, not drawn --- */

void * CCALL CreateFontIndirectA_c(void *lplf)
{
    uint32_t handle = object_new(GDI_FONT);
    gdi_object *font = object_get(handle, GDI_FONT);

    if (font != NULL && lplf != NULL) memcpy(font->logfont, lplf, 60);
    return GUEST_MEM(handle);
}

int32_t CCALL SetBkMode_c(void *hdc, int32_t iBkMode)
{
    gdi_object *dc = object_get(GUEST_ADDR(hdc), GDI_DC);
    int32_t old;

    if (dc == NULL) return 0;
    old = dc->bk_mode ? dc->bk_mode : 2;        /* OPAQUE */
    dc->bk_mode = iBkMode;
    return old;
}

uint32_t CCALL SetTextColor_c(void *hdc, uint32_t crColor)
{
    gdi_object *dc = object_get(GUEST_ADDR(hdc), GDI_DC);
    uint32_t old;

    if (dc == NULL) return 0xffffffffu;         /* CLR_INVALID */
    old = dc->text_color;
    dc->text_color = crColor;
    return old;
}

uint32_t CCALL TextOutA_c(void *hdc, int32_t nXStart, int32_t nYStart, const char *lpString, int32_t cbString)
{
    GDI_LOG("TextOutA(%08x, %i, %i, \"%.*s\")\n", GUEST_ADDR(hdc), nXStart, nYStart, (int)cbString, lpString != NULL ? lpString : "");
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* GAME_WINAPI_GDI */
