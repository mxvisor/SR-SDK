/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#define DEBUG_DDRAW 0
#if DEBUG_DDRAW
#include <inttypes.h>
#endif
#include "WinApi-ddraw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "WinApi-config.h"
#include "Game_memory.h"
#include "WinApi-game.h"
#include "Game_hooks.h"
#include "WinApi-guest.h"
#include "WinApi-d3d.h"
#include "Game_defs.h"
#include "Game_thread.h"
#include "Game_ui.h"
#include "Game_osd.h"

/* GAME_WINAPI_TOUCH_GAMEPAD 1 (WinApi-user32.c): on Android every mode fills a
 * 4:3 display, as on the monitor of the game's time (320x200 is not 4:3 in
 * pixels), and the on-screen gamepad is drawn over the picture (default 0) */
#ifndef GAME_WINAPI_TOUCH_GAMEPAD
#define GAME_WINAPI_TOUCH_GAMEPAD 0
#endif


#define eprintf(...) fprintf(stderr,__VA_ARGS__)


#define make_hresult(s,f,c) ((uint32_t)(((unsigned long)(s)<<31)|((unsigned long)(f)<<16)|((unsigned long)(c))))

#define E_NOINTERFACE ((uint32_t)0x80004002L)
#define E_POINTER ((uint32_t)0x80004003L)
#define E_OUTOFMEMORY ((uint32_t)0x8007000EL)
#define E_INVALIDARG ((uint32_t)0x80070057L)

#define DD_OK 0

#define _FACDD  0x876
#define MAKE_DDHRESULT( code )  make_hresult( 1, _FACDD, code )

#define DDERR_OUTOFMEMORY                       E_OUTOFMEMORY
#define DDERR_INVALIDPARAMS                     E_INVALIDARG
#define DDERR_OUTOFVIDEOMEMORY                  MAKE_DDHRESULT( 380 )
#define DDERR_NOCLIPLIST                        MAKE_DDHRESULT( 205 )
#define DDERR_NOCLIPPERATTACHED                 MAKE_DDHRESULT( 202 )
#define DDERR_SURFACEBUSY                       MAKE_DDHRESULT( 430 )
#define DDERR_SURFACELOST                       MAKE_DDHRESULT( 450 )
#define DDERR_UNSUPPORTEDMODE                   MAKE_DDHRESULT( 590 )

#define DDSCL_FULLSCREEN                        0x00000001l
#define DDSCL_EXCLUSIVE                         0x00000010l

#define DDSCAPS_RESERVED1                       0x00000001l
#define DDSCAPS_ALPHA                           0x00000002l
#define DDSCAPS_BACKBUFFER                      0x00000004l
#define DDSCAPS_COMPLEX                         0x00000008l
#define DDSCAPS_FLIP                            0x00000010l
#define DDSCAPS_FRONTBUFFER                     0x00000020l
#define DDSCAPS_OFFSCREENPLAIN                  0x00000040l
#define DDSCAPS_OVERLAY                         0x00000080l
#define DDSCAPS_PALETTE                         0x00000100l
#define DDSCAPS_PRIMARYSURFACE                  0x00000200l
#define DDSCAPS_RESERVED3                       0x00000400l
#define DDSCAPS_PRIMARYSURFACELEFT              0x00000000l
#define DDSCAPS_SYSTEMMEMORY                    0x00000800l
#define DDSCAPS_TEXTURE                         0x00001000l
#define DDSCAPS_3DDEVICE                        0x00002000l
#define DDSCAPS_VIDEOMEMORY                     0x00004000l
#define DDSCAPS_VISIBLE                         0x00008000l
#define DDSCAPS_WRITEONLY                       0x00010000l
#define DDSCAPS_ZBUFFER                         0x00020000l
#define DDSCAPS_OWNDC                           0x00040000l
#define DDSCAPS_LIVEVIDEO                       0x00080000l
#define DDSCAPS_HWCODEC                         0x00100000l
#define DDSCAPS_MODEX                           0x00200000l
#define DDSCAPS_MIPMAP                          0x00400000l
#define DDSCAPS_RESERVED2                       0x00800000l
#define DDSCAPS_ALLOCONLOAD                     0x04000000l
#define DDSCAPS_VIDEOPORT                       0x08000000l
#define DDSCAPS_LOCALVIDMEM                     0x10000000l
#define DDSCAPS_NONLOCALVIDMEM                  0x20000000l
#define DDSCAPS_STANDARDVGAMODE                 0x40000000l
#define DDSCAPS_OPTIMIZED                       0x80000000l

#define DDSD_CAPS               0x00000001l     // default
#define DDSD_HEIGHT             0x00000002l
#define DDSD_WIDTH              0x00000004l
#define DDSD_PITCH              0x00000008l
#define DDSD_BACKBUFFERCOUNT    0x00000020l
#define DDSD_ZBUFFERBITDEPTH    0x00000040l
#define DDSD_ALPHABITDEPTH      0x00000080l
#define DDSD_LPSURFACE          0x00000800l
#define DDSD_PIXELFORMAT        0x00001000l
#define DDSD_CKDESTOVERLAY      0x00002000l
#define DDSD_CKDESTBLT          0x00004000l
#define DDSD_CKSRCOVERLAY       0x00008000l
#define DDSD_CKSRCBLT           0x00010000l
#define DDSD_MIPMAPCOUNT        0x00020000l
#define DDSD_REFRESHRATE        0x00040000l
#define DDSD_LINEARSIZE         0x00080000l
#define DDSD_TEXTURESTAGE       0x00100000l
#define DDSD_FVF                0x00200000l
#define DDSD_SRCVBHANDLE        0x00400000l
#define DDSD_DEPTH              0x00800000l
#define DDSD_ALL                0x00fff9eel

#define DDPF_ALPHAPIXELS                        0x00000001l
#define DDPF_ZBUFFER                            0x00000400l
#define DDPF_PALETTEINDEXED8                    0x00000020l
#define DDPF_RGB                                0x00000040l

#define DDLOCK_WAIT                             0x00000001L

#define DDBLT_ALPHADEST                         0x00000001l
#define DDBLT_ALPHADESTCONSTOVERRIDE            0x00000002l
#define DDBLT_ALPHADESTNEG                      0x00000004l
#define DDBLT_ALPHADESTSURFACEOVERRIDE          0x00000008l
#define DDBLT_ALPHAEDGEBLEND                    0x00000010l
#define DDBLT_ALPHASRC                          0x00000020l
#define DDBLT_ALPHASRCCONSTOVERRIDE             0x00000040l
#define DDBLT_ALPHASRCNEG                       0x00000080l
#define DDBLT_ALPHASRCSURFACEOVERRIDE           0x00000100l
#define DDBLT_ASYNC                             0x00000200l
#define DDBLT_COLORFILL                         0x00000400l
#define DDBLT_DDFX                              0x00000800l
#define DDBLT_DDROPS                            0x00001000l
#define DDBLT_KEYDEST                           0x00002000l
#define DDBLT_KEYDESTOVERRIDE                   0x00004000l
#define DDBLT_KEYSRC                            0x00008000l
#define DDBLT_KEYSRCOVERRIDE                    0x00010000l
#define DDBLT_ROP                               0x00020000l
#define DDBLT_ROTATIONANGLE                     0x00040000l
#define DDBLT_ZBUFFER                           0x00080000l
#define DDBLT_ZBUFFERDESTCONSTOVERRIDE          0x00100000l
#define DDBLT_ZBUFFERDESTOVERRIDE               0x00200000l
#define DDBLT_ZBUFFERSRCCONSTOVERRIDE           0x00400000l
#define DDBLT_ZBUFFERSRCOVERRIDE                0x00800000l
#define DDBLT_WAIT                              0x01000000l
#define DDBLT_DEPTHFILL                         0x02000000l
#define DDBLT_DONOTWAIT                         0x08000000l

#define SRCCOPY             0x00CC0020


typedef struct _ddcolorkey {
    uint32_t dwColorSpaceLowValue;
    uint32_t dwColorSpaceHighValue;
} ddcolorkey, *lpddcolorkey;

typedef struct _rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} rect, *lprect;

typedef struct _ddpixelformat {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    union
    {
        uint32_t dwRGBBitCount;
        uint32_t dwYUVBitCount;
        uint32_t dwAlphaBitDepth;
    };
    union
    {
        uint32_t dwRBitMask;
        uint32_t dwYBitMask;
    };
    union
    {
        uint32_t dwGBitMask;
        uint32_t dwUBitMask;
    };
    union
    {
        uint32_t dwBBitMask;
        uint32_t dwVBitMask;
    };
    union
    {
        uint32_t dwRGBAlphaBitMask;
    };
} ddpixelformat, *lpddpixelformat;

typedef struct _ddscaps {
    uint32_t dwCaps;
} ddscaps, *lpddscaps;

typedef struct _ddscaps2 {
    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
} ddscaps2, *lpddscaps2;

typedef struct _ddsurfacedesc {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    int32_t lPitch;
    uint32_t dwBackBufferCount;
    union {
        uint32_t dwRefreshRate;
        uint32_t dwMipMapCount;     /* with DDSD_MIPMAPCOUNT */
    };
    uint32_t dwAlphaBitDepth;
    uint32_t dwReserved;
    PTR32(void) lpSurface;
    ddcolorkey ddckCKDestOverlay;
    ddcolorkey ddckCKDestBlt;
    ddcolorkey ddckCKSrcOverlay;
    ddcolorkey ddckCKSrcBlt;
    ddpixelformat ddpfPixelFormat;
    ddscaps ddsCaps;
} ddsurfacedesc, *lpddsurfacedesc;

typedef struct _ddbltfx
{
    uint32_t dwSize;
    uint32_t dwDDFX;
    uint32_t dwROP;
    uint32_t dwDDROP;
    uint32_t dwRotationAngle;
    uint32_t dwZBufferOpCode;
    uint32_t dwZBufferLow;
    uint32_t dwZBufferHigh;
    uint32_t dwZBufferBaseDest;
    uint32_t dwZDestConstBitDepth;
    union
    {
        uint32_t dwZDestConst;
        PTR32(struct IDirectDrawSurface_c) lpDDSZBufferDest;
    };
    uint32_t dwZSrcConstBitDepth;
    union
    {
        uint32_t dwZSrcConst;
        PTR32(struct IDirectDrawSurface_c) lpDDSZBufferSrc;
    };
    uint32_t dwAlphaEdgeBlendBitDepth;
    uint32_t dwAlphaEdgeBlend;
    uint32_t dwReserved;
    uint32_t dwAlphaDestConstBitDepth;
    union
    {
        uint32_t dwAlphaDestConst;
        PTR32(struct IDirectDrawSurface_c) lpDDSAlphaDest;
    };
    uint32_t dwAlphaSrcConstBitDepth;
    union
    {
        uint32_t dwAlphaSrcConst;
        PTR32(struct IDirectDrawSurface_c) lpDDSAlphaSrc;
    };
    union
    {
        uint32_t dwFillColor;
        uint32_t dwFillDepth;
        uint32_t dwFillPixel;
        PTR32(struct IDirectDrawSurface_c) lpDDSPattern;
    };
    ddcolorkey ddckDestColorkey;
    ddcolorkey ddckSrcColorkey;
} ddbltfx;

#define DDERR_UNSUPPORTED                       ((uint32_t)0x80004001L)
#define DDERR_GENERIC                           ((uint32_t)0x80004005L)
#define DDERR_NOTFOUND                          MAKE_DDHRESULT( 1 )

#define DDSCL_NORMAL                            0x00000008l
#define DDSCL_ALLOWREBOOT                       0x00000002l
#define DDSCL_ALLOWMODEX                        0x00000040l

#define DDENUMRET_OK                            1

#define DDBLTFAST_SRCCOLORKEY                   0x00000001l

#define DDCKEY_SRCBLT                           0x00000008l

#define DDPCAPS_8BIT                            0x00000004l
#define DDPCAPS_4BIT                            0x00000001l
#define DDPCAPS_2BIT                            0x00000002l
#define DDPCAPS_1BIT                            0x00000100l

#define DDCAPS_3D                               0x00000001l
#define DDCAPS_BLT                              0x00000040l
#define DDCAPS_PALETTE                          0x00000400l
#define DDCAPS_COLORKEY                         0x00400000l
#define DDCAPS_BLTCOLORFILL                     0x04000000l
#define DDCKEYCAPS_SRCBLT                       0x00000008l


/* The game draws into 8-bit palettized surfaces, so unlike the 16-bit
 * only DirectDraw of the original a surface here is a plain pixel buffer of
 * 8, 16 or 32 bits per pixel. The primary surface is shown through OpenGL
 * (WinApi-d3dgl.c) in the window of Winapi_ScreenOpenGL whenever it changes. */

#include "WinApi-ddraw-priv.h"
#include "WinApi-d3d.h"
#include "WinApi-d3dgl.h"

/* the display whose primary surface the palette changes are shown on */
static struct IDirectDraw_c *winapi_dd_screen = NULL;
/* the window of Winapi_ScreenOpenGL */
static SDL_Window *dd_gl_window = NULL;
/* window ID of the active DirectDraw window, for focus events */
static uint32_t winapi_dd_window_id = 0;

uint32_t winapi_get_dd_window_id(void)
{
    return winapi_dd_window_id;
}

int winapi_is_dd_active(void)
{
    return winapi_dd_screen != NULL;
}

int winapi_get_dd_mode(int *width, int *height)
{
    if (winapi_dd_screen == NULL) return 0;
    if (width != NULL) *width = winapi_dd_screen->width;
    if (height != NULL) *height = winapi_dd_screen->height;
    return 1;
}

void Winapi_DestroyDirectDrawWindow(void)
{
    Winapi_D3dPresentDeinit();
    if (winapi_dd_screen != NULL && winapi_dd_screen->Window != NULL)
    {
        SDL_DestroyWindow(winapi_dd_screen->Window);
        winapi_dd_screen->Window = NULL;
    }
    if (dd_gl_window != NULL)
    {
        SDL_DestroyWindow(dd_gl_window);
        dd_gl_window = NULL;
    }
    winapi_dd_window_id = 0;
}

extern uint32_t IDirectDrawVtbl_asm2c;
extern uint32_t IDirectDraw2Vtbl_asm2c;
extern uint32_t IDirectDrawSurfaceVtbl_asm2c;
extern uint32_t IDirectDrawPaletteVtbl_asm2c;
extern uint32_t IDirectDrawClipperVtbl_asm2c;


void Winapi_SurfaceToBgra(struct IDirectDrawSurface_c *prim, uint32_t *dst, int dstpitch)
{
    uint32_t x, y;

    for (y = 0; y < prim->height; y++, dst += dstpitch / 4)
    {
        const uint8_t *src = prim->pixels + y * prim->pitch;

        if (prim->bpp == 8)
        {
            const uint32_t *pal = (prim->palette != NULL) ? prim->palette->argb : NULL;
            for (x = 0; x < prim->width; x++)
            {
                dst[x] = (pal != NULL) ? pal[src[x]] : (0xff000000u | (src[x] * 0x010101u));
            }
        }
        else if (prim->bpp == 16)
        {
            const uint16_t *s16 = (const uint16_t *)src;
            for (x = 0; x < prim->width; x++)
            {
                uint32_t p = s16[x];
                uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
                dst[x] = 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
            }
        }
        else
        {
            memcpy(dst, src, prim->width * 4);
        }
    }
}


/* <GAME>_FPS=1 prints the number of presented frames per second to stderr */
static void CountFrame(void)
{
    static int enabled = -1;
    static uint32_t t0, frames;
    uint32_t now;

    if (enabled < 0)
    {
        const char *e = getenv(GAME_ENV("FPS"));
        enabled = (e != NULL) && (e[0] == '1');
    }
    if (!enabled) return;
    now = SDL_GetTicks();
    if (frames == 0) t0 = now;
    frames++;
    if (now - t0 >= 2000)
    {
        fprintf(stderr, "FPS: %.1f\n", frames * 1000.0 / (now - t0));
        frames = 0;
    }
}

static void Present(struct IDirectDraw_c *dd)
{
    CountFrame();
    /* Direct3D's picture too: WinApi-d3dgl.c draws into the primary's mirror */
    if ((dd->Primary != NULL) && (dd->Window != NULL)) Winapi_D3dGLPresent(dd->Primary, dd->Window);
}

static struct IDirectDrawSurface_c *NewSurface(struct IDirectDraw_c *dd, uint32_t width, uint32_t height, uint32_t bpp)
{
    struct IDirectDrawSurface_c *s;
    uint32_t size;

    s = (struct IDirectDrawSurface_c *)x86_malloc(sizeof(struct IDirectDrawSurface_c));
    if (s == NULL) return NULL;
    memset(s, 0, sizeof(struct IDirectDrawSurface_c));

    s->lpVtbl = &IDirectDrawSurfaceVtbl_asm2c;
    s->RefCount = 1;
    s->dd = dd;
    s->width = width;
    s->height = height;
    s->bpp = bpp;
    s->pitch = (width * (bpp >> 3) + 3) & ~3u;

    size = height * s->pitch + 32;
    s->mem = (uint8_t *)x86_malloc(size);
    if (s->mem == NULL)
    {
        x86_free(s);
        return NULL;
    }
    memset(s->mem, 0, size);
    s->pixels = (uint8_t *)(((uintptr_t)s->mem + 15) & ~(uintptr_t)15);

    return s;
}

static void FreeSurface(struct IDirectDrawSurface_c *s)
{
    if (s->palette != NULL) s->palette->RefCount--;
    if (s->clipper != NULL) s->clipper->RefCount--;
    if (s->zsurf != NULL) IDirectDrawSurface_Release_c(s->zsurf);
    if (s->lpmip != NULL)
    {
        struct IDirectDrawSurface_c *next;
        for (next = s->lpmip; next != NULL; next = next->lpmip) next->mip_base = NULL;
        IDirectDrawSurface_Release_c(s->lpmip);
        s->lpmip = NULL;
    }
#ifdef GAME_D3D_GL
    Winapi_D3dGLTextureFree(s);
#endif
    Winapi_D3dSurfaceFree(s);
    x86_free(s->mem);
    x86_free(s);
}

/* a change of a mip level is a change of the texture: the GL mirror is keyed on
 * the level 0's generation */
static void TextureTouched(struct IDirectDrawSurface_c *s)
{
    s->texture_gen++;
    if (s->mip_base != NULL)
    {
        s->mip_base->texture_gen++;
        s->mip_base->mips_filled = 1;   /* the game filled a level above 0 */
    }
}

/* Clips [left,top,right,bottom] to the surface, returns 0 when nothing is left */
static int ClipRect(const struct _rect *in, uint32_t width, uint32_t height, struct _rect *out)
{
    if (in != NULL)
    {
        *out = *in;
    }
    else
    {
        out->left = 0;
        out->top = 0;
        out->right = width;
        out->bottom = height;
    }
    return 1;
}

static uint32_t GetPixel(const uint8_t *p, uint32_t bpp)
{
    switch (bpp)
    {
        case 8: return *p;
        case 16: return *(const uint16_t *)p;
        default: return *(const uint32_t *)p;
    }
}

static void PutPixel(uint8_t *p, uint32_t bpp, uint32_t v)
{
    switch (bpp)
    {
        case 8: *p = (uint8_t)v; break;
        case 16: *(uint16_t *)p = (uint16_t)v; break;
        default: *(uint32_t *)p = v; break;
    }
}

/* Copies (and stretches, nearest neighbour) a rectangle; ckey != NULL skips the
 * source pixels inside the colour range. Both rectangles may extend outside the
 * surfaces - the pixels outside are skipped. */
static uint32_t BltRects(struct IDirectDrawSurface_c *dst, struct _rect d, struct IDirectDrawSurface_c *src, struct _rect s, const ddcolorkey *ckey)
{
    int32_t dw, dh, sw, sh, x, y;
    uint32_t bytes;

    if (dst->bpp != src->bpp) return DDERR_INVALIDPARAMS;

    dw = d.right - d.left;
    dh = d.bottom - d.top;
    sw = s.right - s.left;
    sh = s.bottom - s.top;
    if ((dw <= 0) || (dh <= 0) || (sw <= 0) || (sh <= 0)) return DD_OK;

    bytes = dst->bpp >> 3;

    for (y = 0; y < dh; y++)
    {
        int32_t dy = d.top + y;
        int32_t sy = s.top + (int32_t)(((int64_t)y * sh) / dh);

        if ((dy < 0) || (dy >= (int32_t)dst->height) || (sy < 0) || (sy >= (int32_t)src->height)) continue;

        if ((dw == sw) && (ckey == NULL))
        {
            int32_t x0 = 0, x1 = dw;

            if (d.left < 0) x0 = -d.left;
            if (s.left + x0 < 0) x0 = -s.left;
            if (d.left + x1 > (int32_t)dst->width) x1 = dst->width - d.left;
            if (s.left + x1 > (int32_t)src->width) x1 = src->width - s.left;
            if (x1 > x0)
            {
                memmove(dst->pixels + dy * dst->pitch + (d.left + x0) * bytes,
                        src->pixels + sy * src->pitch + (s.left + x0) * bytes,
                        (x1 - x0) * bytes);
            }
            continue;
        }

        for (x = 0; x < dw; x++)
        {
            int32_t dx = d.left + x;
            int32_t sx = s.left + (int32_t)(((int64_t)x * sw) / dw);
            uint32_t v;

            if ((dx < 0) || (dx >= (int32_t)dst->width) || (sx < 0) || (sx >= (int32_t)src->width)) continue;

            v = GetPixel(src->pixels + sy * src->pitch + sx * bytes, src->bpp);
            if ((ckey != NULL) && (v >= ckey->dwColorSpaceLowValue) && (v <= ckey->dwColorSpaceHighValue)) continue;
            PutPixel(dst->pixels + dy * dst->pitch + dx * bytes, dst->bpp, v);
        }
    }

    return DD_OK;
}

/* D3DOP_TEXTURELOAD: Direct3D copies a texture, mip levels included, from the
 * source surface into the destination one; the game relies on it to fill the
 * video-memory texture it later binds (its own writes go to the system one). */
uint32_t Winapi_DDrawCopyLevels(struct IDirectDrawSurface_c *dst, struct IDirectDrawSurface_c *src)
{
    struct IDirectDrawSurface_c *dl, *sl;
    struct _rect r;

    if ((dst == NULL) || (src == NULL) || (dst == src)) return DD_OK;

    r.left = 0; r.top = 0; r.right = (int32_t)dst->width; r.bottom = (int32_t)dst->height;
    {
        struct _rect s;
        s.left = 0; s.top = 0;
        s.right = (int32_t)((src->width < dst->width) ? src->width : dst->width);
        s.bottom = (int32_t)((src->height < dst->height) ? src->height : dst->height);
        BltRects(dst, r, src, s, NULL);
    }
    for (dl = dst->lpmip, sl = src->lpmip; (dl != NULL) && (sl != NULL); dl = dl->lpmip, sl = sl->lpmip)
    {
        struct _rect d, s;
        d.left = 0; d.top = 0; d.right = (int32_t)dl->width; d.bottom = (int32_t)dl->height;
        s.left = 0; s.top = 0;
        s.right = (int32_t)((sl->width < dl->width) ? sl->width : dl->width);
        s.bottom = (int32_t)((sl->height < dl->height) ? sl->height : dl->height);
        BltRects(dl, d, sl, s, NULL);
    }
    dst->mips_filled = (src->mips_filled != 0);
    dst->texture_gen++;
#ifdef GAME_D3D_GL
    if (Winapi_Direct3DGL()) Winapi_D3dGLUpload(dst);
#endif
    return DD_OK;
}


uint32_t CCALL DirectDrawCreate_c(void *lpGUID, PTR32(struct IDirectDraw_c) *lplpDD, void *pUnkOuter)
{
    struct IDirectDraw_c *lpDD_c;

#if DEBUG_DDRAW
    eprintf("DirectDrawCreate: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR " - ", (uintptr_t)lpGUID, (uintptr_t)lplpDD, (uintptr_t)pUnkOuter);
#endif

    if (pUnkOuter != NULL || lplpDD == NULL)
    {
        return DDERR_INVALIDPARAMS;
    }
    if (lpGUID != NULL)
    {
        /* GUID_NULL is the primary device too: a game that enumerated the
         * devices may pass a copy of the callback's NULL GUID. */
        const unsigned char *guid = (const unsigned char *)lpGUID;
        int i;
        for (i = 0; i < 16; i++)
        {
            if (guid[i] != 0) return DDERR_INVALIDPARAMS;
        }
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        eprintf("Error: SDL_InitSubSystem: Video: %s\n", SDL_GetError());
        exit(1);
    }

    lpDD_c = (struct IDirectDraw_c *)x86_malloc(sizeof(struct IDirectDraw_c));

    if (lpDD_c == NULL)
    {
        return DDERR_OUTOFMEMORY;
    }

    memset(lpDD_c, 0, sizeof(struct IDirectDraw_c));
    lpDD_c->lpVtbl = &IDirectDrawVtbl_asm2c;
    lpDD_c->RefCount = 1;
    /* a new object reports the mode the display is in before SetDisplayMode */
    lpDD_c->width = 640;
    lpDD_c->height = 480;
    lpDD_c->bpp = 16;

    *lplpDD = lpDD_c;

#if DEBUG_DDRAW
    eprintf("OK: 0x%" PRIxPTR "\n", (uintptr_t)lpDD_c);
#endif

    return DD_OK;
}

/* Display modes reported to the game: 8 bpp first, then 16 bpp */
static const uint16_t display_modes[][3] = {
    { 640, 480, 8 }, { 640, 480, 16 }, { 800, 600, 8 }, { 800, 600, 16 }
};

/* Calls the guest callback `BOOL FAR PASCAL cb(LPGUID, LPSTR, LPSTR, LPVOID)` once, for the primary display */
uint32_t Winapi_DirectDrawEnumerate(uint32_t callback, uint32_t context)
{
    static char *strings = NULL;
    uint32_t args[4];

    if (callback == 0) return DDERR_INVALIDPARAMS;

    if (strings == NULL)
    {
        strings = (char *)x86_malloc(32);
        if (strings == NULL) return DDERR_OUTOFMEMORY;
        strcpy(strings, "display");
        strcpy(strings + 16, "primary");
    }

    args[0] = 0;
    args[1] = GUEST_ADDR(strings);
    args[2] = GUEST_ADDR(strings + 16);
    args[3] = context;
    Winapi_CallGuest(callback, 4, args);

    return DD_OK;
}

uint32_t CCALL DirectDrawEnumerateA_c(uint32_t callback, uint32_t context)
{
    return Winapi_DirectDrawEnumerate(callback, context);
}


static const uint8_t IID_IDirectDraw[16] = { 0x80, 0xdb, 0x14, 0x6c, 0x33, 0xa7, 0xce, 0x11, 0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60 };
static const uint8_t IID_IDirectDraw2[16] = { 0xe0, 0xf3, 0xa6, 0xb3, 0x43, 0x2b, 0xcf, 0x11, 0xa2, 0xde, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56 };

uint32_t CCALL IDirectDraw_QueryInterface_c(struct IDirectDraw_c *lpThis, void * riid, PTR32(void)* ppvObj)
{
#if DEBUG_DDRAW
    eprintf("IDirectDraw_QueryInterface: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR "\n", (uintptr_t)lpThis, (uintptr_t)riid, (uintptr_t)ppvObj);
#endif

    if (ppvObj == NULL) return E_POINTER;
    lpThis = DDBase(lpThis);
    if ((lpThis != NULL) && (riid != NULL) && (memcmp(riid, IID_IDirectDraw, 16) == 0))
    {
        lpThis->RefCount++;
        *ppvObj = (void *)lpThis;
        return DD_OK;
    }
    if ((lpThis != NULL) && (riid != NULL) && (memcmp(riid, IID_IDirectDraw2, 16) == 0))
    {
        /* IDirectDraw2 differs from IDirectDraw in SetDisplayMode, which takes two more
         * arguments, and in GetAvailableVidMem (the last slot of the vtable): its own object
         * with its own vtable, that works on this one.  A program may hold both. */
        if (lpThis->proxy == NULL)
        {
            lpThis->proxy = (struct IDirectDraw_c *)x86_malloc(sizeof(struct IDirectDraw_c));
            if (lpThis->proxy == NULL) { *ppvObj = (void *)NULL; return DDERR_OUTOFMEMORY; }
            memset(lpThis->proxy, 0, sizeof(struct IDirectDraw_c));
            lpThis->proxy->lpVtbl = &IDirectDraw2Vtbl_asm2c;
            lpThis->proxy->base = lpThis;
        }
        lpThis->RefCount++;
        *ppvObj = (void *)lpThis->proxy;
        return DD_OK;
    }
    if ((lpThis != NULL) && (riid != NULL) && Winapi_D3dQuery(lpThis, riid, (uint32_t *)ppvObj))
    {
        return DD_OK;
    }
    *ppvObj = (void *)NULL;
    return E_NOINTERFACE;
}

uint32_t CCALL IDirectDraw_AddRef_c(struct IDirectDraw_c *lpThis)
{
    lpThis = DDBase(lpThis);
    if (lpThis == NULL) return 0;
    lpThis->RefCount++;
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDraw_Release_c(struct IDirectDraw_c *lpThis)
{
    lpThis = DDBase(lpThis);
    if (lpThis == NULL) return 0;

    lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        if (winapi_dd_screen == lpThis) winapi_dd_screen = NULL;
        /* the window is the screen's, Winapi_ScreenOpenGL keeps it and the next display mode
         * shows it again: a program that releases DirectDraw between modes (from a thread that
         * did not make the window) would hide it, and the hide waits for the thread that made it */
        lpThis->Window = NULL;

        x86_free(lpThis->proxy);
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDraw_Compact_c(struct IDirectDraw_c *lpThis)
{
    lpThis = DDBase(lpThis);
    return DD_OK;
}

uint32_t CCALL IDirectDraw_CreateClipper_c(struct IDirectDraw_c *lpThis, uint32_t param1, PTR32(void)* param2, void * param3)
{
    lpThis = DDBase(lpThis);
    struct IDirectDrawClipper_c *clipper;

    (void)param1;
    if ((lpThis == NULL) || (param2 == NULL) || (param3 != NULL)) return DDERR_INVALIDPARAMS;

    clipper = (struct IDirectDrawClipper_c *)x86_malloc(sizeof(struct IDirectDrawClipper_c));
    if (clipper == NULL) return DDERR_OUTOFMEMORY;

    clipper->lpVtbl = &IDirectDrawClipperVtbl_asm2c;
    clipper->RefCount = 1;
    *param2 = clipper;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_CreatePalette_c(struct IDirectDraw_c *lpThis, uint32_t dwFlags, void * lpColorTable, PTR32(void)* lplpDDPalette, void * pUnkOuter)
{
    lpThis = DDBase(lpThis);
    struct IDirectDrawPalette_c *pal;
    const uint8_t *colors = (const uint8_t *)lpColorTable;
    uint32_t i;

#if DEBUG_DDRAW
    eprintf("IDirectDraw_CreatePalette: 0x%" PRIxPTR ", 0x%x, 0x%" PRIxPTR "\n", (uintptr_t)lpThis, dwFlags, (uintptr_t)lpColorTable);
#endif

    if ((lpThis == NULL) || (lplpDDPalette == NULL) || (pUnkOuter != NULL)) return DDERR_INVALIDPARAMS;

    pal = (struct IDirectDrawPalette_c *)x86_malloc(sizeof(struct IDirectDrawPalette_c));
    if (pal == NULL) return DDERR_OUTOFMEMORY;

    memset(pal, 0, sizeof(struct IDirectDrawPalette_c));
    pal->lpVtbl = &IDirectDrawPaletteVtbl_asm2c;
    pal->RefCount = 1;
    pal->entries = (dwFlags & DDPCAPS_8BIT) ? 256 : ((dwFlags & DDPCAPS_4BIT) ? 16 : ((dwFlags & DDPCAPS_2BIT) ? 4 : ((dwFlags & DDPCAPS_1BIT) ? 2 : 256)));

    for (i = 0; i < 256; i++)
    {
        pal->argb[i] = 0xff000000u;
    }
    if (colors != NULL)
    {
        /* PALETTEENTRY: red, green, blue, flags */
        for (i = 0; i < pal->entries; i++)
        {
            pal->argb[i] = 0xff000000u | (colors[i * 4] << 16) | (colors[i * 4 + 1] << 8) | colors[i * 4 + 2];
        }
    }

    *lplpDDPalette = pal;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_CreateSurface_c(struct IDirectDraw_c *lpThis, struct _ddsurfacedesc *lpDDSurfaceDesc, PTR32(struct IDirectDrawSurface_c)* lplpDDSurface, void * pUnkOuter)
{
    lpThis = DDBase(lpThis);
    struct IDirectDrawSurface_c *s;
    uint32_t width, height, bpp;

#if DEBUG_DDRAW
    eprintf("IDirectDraw_CreateSurface: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR " - ", (uintptr_t)lpThis, (uintptr_t)lpDDSurfaceDesc, (uintptr_t)lplpDDSurface, (uintptr_t)pUnkOuter);
#endif

    if ((lpThis == NULL) || (lpDDSurfaceDesc == NULL) || (lplpDDSurface == NULL) || (pUnkOuter != NULL))
    {
        return DDERR_INVALIDPARAMS;
    }

    if ((lpDDSurfaceDesc->dwSize != 108) || ((lpDDSurfaceDesc->dwFlags & DDSD_CAPS) == 0))
    {
        return DDERR_INVALIDPARAMS;
    }
#if DEBUG_DDRAW
    eprintf("%ux%u flags 0x%x caps 0x%x back %u - ", lpDDSurfaceDesc->dwWidth, lpDDSurfaceDesc->dwHeight, lpDDSurfaceDesc->dwFlags, lpDDSurfaceDesc->ddsCaps.dwCaps, lpDDSurfaceDesc->dwBackBufferCount);
#endif

    if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
    {
        uint32_t pwidth = lpThis->width, pheight = lpThis->height, pbpp = lpThis->bpp;

        if (lpThis->Primary != NULL) return DDERR_GENERIC;
        /* no display mode set: a program that only probes what DirectX can do (the
         * version check at its start) gets a desktop-sized surface that is never shown */
        if (pwidth == 0) pwidth = 640;
        if (pheight == 0) pheight = 480;
        if (pbpp == 0) pbpp = 32;

        s = NewSurface(lpThis, pwidth, pheight, pbpp);
        if (s == NULL) return DDERR_OUTOFVIDEOMEMORY;
        s->primary = 1;
        s->caps = lpDDSurfaceDesc->ddsCaps.dwCaps;

        if ((lpDDSurfaceDesc->dwFlags & DDSD_BACKBUFFERCOUNT) && (lpDDSurfaceDesc->dwBackBufferCount != 0))
        {
            s->lpBackbuffer = NewSurface(lpThis, pwidth, pheight, pbpp);
            if (s->lpBackbuffer == NULL)
            {
                FreeSurface(s);
                return DDERR_OUTOFVIDEOMEMORY;
            }
            s->lpBackbuffer->backbuffer = 1;
            s->lpBackbuffer->caps = DDSCAPS_BACKBUFFER | DDSCAPS_VIDEOMEMORY;
        }

        lpThis->Primary = s;
        *lplpDDSurface = s;
        return DD_OK;
    }

    width = lpDDSurfaceDesc->dwWidth;
    height = lpDDSurfaceDesc->dwHeight;
    bpp = lpThis->bpp;
    if (!(lpDDSurfaceDesc->dwFlags & DDSD_WIDTH) || !(lpDDSurfaceDesc->dwFlags & DDSD_HEIGHT) || (width == 0) || (height == 0))
    {
        return DDERR_INVALIDPARAMS;
    }
    if (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT)
    {
        if (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_ZBUFFER)
        {
            /* a Z-buffer: dwZBufferBitDepth shares the slot of dwRGBBitCount */
            bpp = lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount;
        }
        else if (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
        {
            bpp = 8;
        }
        else if (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_RGB)
        {
            bpp = lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount;
        }
    }
    if ((bpp != 8) && (bpp != 16) && (bpp != 32)) return DDERR_UNSUPPORTEDMODE;

    s = NewSurface(lpThis, width, height, bpp);
    if (s == NULL) return DDERR_OUTOFVIDEOMEMORY;
    s->caps = lpDDSurfaceDesc->ddsCaps.dwCaps;
    if ((bpp == 16) && (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) && (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_RGB))
    {
        s->rmask = lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask;
        s->gmask = lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask;
        s->bmask = lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask;
        s->amask = (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_ALPHAPIXELS) ? lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask : 0;
    }
    s->zbuffer = ((lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) && (lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_ZBUFFER)) ||
                 (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_ZBUFFER);

    /* a texture with mip levels: the game gets the chain from GetAttachedSurface and
     * fills every level itself, as DirectDraw would hand it out */
    if (!s->zbuffer && (lpDDSurfaceDesc->dwFlags & DDSD_MIPMAPCOUNT) &&
        (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_MIPMAP) && (lpDDSurfaceDesc->dwMipMapCount > 1))
    {
        uint32_t w = width, h = height, levels = lpDDSurfaceDesc->dwMipMapCount, i;
        struct IDirectDrawSurface_c *prev = s;

        if (levels > 16) levels = 16;       /* the DirectDraw limit */
        for (i = 1; i < levels; i++)
        {
            struct IDirectDrawSurface_c *m;

            w = (w > 1) ? w / 2 : 1;
            h = (h > 1) ? h / 2 : 1;
            m = NewSurface(lpThis, w, h, bpp);
            if (m == NULL) break;
            m->caps = (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_TEXTURE) | DDSCAPS_MIPMAP;
            m->rmask = s->rmask; m->gmask = s->gmask; m->bmask = s->bmask; m->amask = s->amask;
            m->mip_base = s;
            prev->lpmip = m;                /* the chain's own reference */
            prev = m;
            s->mip_count = i + 1;
        }
    }

    *lplpDDSurface = s;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_DuplicateSurface_c(struct IDirectDraw_c *lpThis, struct IDirectDrawSurface_c * /*param1*/, PTR32(struct IDirectDrawSurface_c)* /*param2*/)
{
    lpThis = DDBase(lpThis);
    eprintf("Unsupported method: %s\n", "IDirectDraw_DuplicateSurface");
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDraw_EnumDisplayModes_c(struct IDirectDraw_c *lpThis, uint32_t /*dwFlags*/, void * /*lpDDSurfaceDesc*/, void * lpContext, void * lpEnumModesCallback)
{
    lpThis = DDBase(lpThis);
    struct _ddsurfacedesc *desc;
    uint32_t i, args[2];

    if ((lpThis == NULL) || (lpEnumModesCallback == NULL)) return DDERR_INVALIDPARAMS;

    desc = (struct _ddsurfacedesc *)x86_malloc(sizeof(struct _ddsurfacedesc));
    if (desc == NULL) return DDERR_OUTOFMEMORY;

    for (i = 0; i < sizeof(display_modes) / sizeof(display_modes[0]); i++)
    {
        memset(desc, 0, sizeof(*desc));
        desc->dwSize = sizeof(*desc);
        desc->dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
        desc->dwWidth = display_modes[i][0];
        desc->dwHeight = display_modes[i][1];
        desc->lPitch = display_modes[i][0] * (display_modes[i][2] >> 3);
        desc->ddpfPixelFormat.dwSize = sizeof(ddpixelformat);
        desc->ddpfPixelFormat.dwRGBBitCount = display_modes[i][2];
        if (display_modes[i][2] == 8)
        {
            desc->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
        }
        else
        {
            desc->ddpfPixelFormat.dwFlags = DDPF_RGB;
            desc->ddpfPixelFormat.dwRBitMask = 0xf800;
            desc->ddpfPixelFormat.dwGBitMask = 0x07e0;
            desc->ddpfPixelFormat.dwBBitMask = 0x001f;
        }

        args[0] = GUEST_ADDR(desc);
        args[1] = GUEST_ADDR(lpContext);
        if (Winapi_CallGuest(GUEST_ADDR(lpEnumModesCallback), 2, args) != DDENUMRET_OK) break;
    }

    x86_free(desc);
    return DD_OK;
}

uint32_t CCALL IDirectDraw_EnumSurfaces_c(struct IDirectDraw_c *lpThis, uint32_t /*param1*/, void * /*param2*/, void * /*param3*/, void * /*param4*/)
{
    lpThis = DDBase(lpThis);
    eprintf("Unsupported method: %s\n", "IDirectDraw_EnumSurfaces");
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDraw_FlipToGDISurface_c(struct IDirectDraw_c *lpThis)
{
    lpThis = DDBase(lpThis);
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetCaps_c(struct IDirectDraw_c *lpThis, void * lpDDDriverCaps, void * lpDDHELCaps)
{
    lpThis = DDBase(lpThis);
    void *caps[2];
    int i;

    caps[0] = lpDDDriverCaps;
    caps[1] = lpDDHELCaps;
    for (i = 0; i < 2; i++)
    {
        uint32_t *c = (uint32_t *)caps[i];
        uint32_t size;

        if (c == NULL) continue;
        size = c[0];
        if ((size < 64) || (size > 1024)) return DDERR_INVALIDPARAMS;
        memset(c, 0, size);
        c[0] = size;
        c[1] = DDCAPS_BLT | DDCAPS_PALETTE | DDCAPS_COLORKEY | DDCAPS_BLTCOLORFILL;    /* dwCaps */
        if ((i == 0) && Winapi_Direct3DEnabled()) c[1] |= DDCAPS_3D;    /* the driver, not the emulation */
        c[3] = DDCKEYCAPS_SRCBLT;                                                      /* dwCKeyCaps */
        c[15] = 16 * 1024 * 1024;                                                      /* dwVidMemTotal */
        c[16] = 16 * 1024 * 1024;                                                      /* dwVidMemFree */
    }
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetDisplayMode_c(struct IDirectDraw_c *lpThis, void * param1)
{
    lpThis = DDBase(lpThis);
    struct _ddsurfacedesc *desc = (struct _ddsurfacedesc *)param1;

    if ((lpThis == NULL) || (desc == NULL) || (desc->dwSize != 108)) return DDERR_INVALIDPARAMS;
    if (lpThis->width == 0) return DDERR_GENERIC;    /* no mode set */

    memset(desc, 0, sizeof(*desc));
    desc->dwSize = sizeof(*desc);
    desc->dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
    desc->dwWidth = lpThis->width;
    desc->dwHeight = lpThis->height;
    desc->lPitch = lpThis->width * (lpThis->bpp >> 3);
    desc->ddpfPixelFormat.dwSize = sizeof(ddpixelformat);
    desc->ddpfPixelFormat.dwRGBBitCount = lpThis->bpp;
    desc->ddpfPixelFormat.dwFlags = (lpThis->bpp == 8) ? (DDPF_PALETTEINDEXED8 | DDPF_RGB) : DDPF_RGB;
    if (lpThis->bpp == 16)
    {
        desc->ddpfPixelFormat.dwRBitMask = 0xf800;
        desc->ddpfPixelFormat.dwGBitMask = 0x07e0;
        desc->ddpfPixelFormat.dwBBitMask = 0x001f;
    }
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetFourCCCodes_c(struct IDirectDraw_c *lpThis, uint32_t * param1, uint32_t * /*param2*/)
{
    lpThis = DDBase(lpThis);
    if (param1 == NULL) return DDERR_INVALIDPARAMS;
    *param1 = 0;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetGDISurface_c(struct IDirectDraw_c *lpThis, PTR32(struct IDirectDrawSurface_c)* /*param1*/)
{
    lpThis = DDBase(lpThis);
    eprintf("Unsupported method: %s\n", "IDirectDraw_GetGDISurface");
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDraw_GetMonitorFrequency_c(struct IDirectDraw_c *lpThis, uint32_t * param1)
{
    lpThis = DDBase(lpThis);
    if (param1 == NULL) return DDERR_INVALIDPARAMS;
    *param1 = 60;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetScanLine_c(struct IDirectDraw_c *lpThis, uint32_t * param1)
{
    lpThis = DDBase(lpThis);
    if (param1 == NULL) return DDERR_INVALIDPARAMS;
    *param1 = 0;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetVerticalBlankStatus_c(struct IDirectDraw_c *lpThis, uint32_t * param1)
{
    lpThis = DDBase(lpThis);
    if (param1 == NULL) return DDERR_INVALIDPARAMS;
    *param1 = 1;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_Initialize_c(struct IDirectDraw_c *lpThis, void * /*param1*/)
{
    lpThis = DDBase(lpThis);
    return DD_OK;
}

uint32_t CCALL IDirectDraw_RestoreDisplayMode_c(struct IDirectDraw_c *lpThis)
{
    lpThis = DDBase(lpThis);
    return DD_OK;
}

uint32_t CCALL IDirectDraw_SetCooperativeLevel_c(struct IDirectDraw_c *lpThis, void * /*hWnd*/, uint32_t /*dwFlags*/)
{
    lpThis = DDBase(lpThis);
#if DEBUG_DDRAW
    eprintf("IDirectDraw_SetCooperativeLevel: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%x\n", (uintptr_t)lpThis, (uintptr_t)hWnd, dwFlags);
#endif

    if (lpThis == NULL) return DDERR_INVALIDPARAMS;
    return DD_OK;
}

uint32_t CCALL IDirectDraw2_SetDisplayMode_c(struct IDirectDraw_c *lpThis, uint32_t dwWidth, uint32_t dwHeight, uint32_t dwBPP, uint32_t dwRefreshRate, uint32_t dwFlags)
{
    (void)dwRefreshRate; (void)dwFlags;
    return IDirectDraw_SetDisplayMode_c(lpThis, dwWidth, dwHeight, dwBPP);
}

uint32_t CCALL IDirectDraw_SetDisplayMode_c(struct IDirectDraw_c *lpThis, uint32_t dwWidth, uint32_t dwHeight, uint32_t dwBPP)
{
    lpThis = DDBase(lpThis);
#if DEBUG_DDRAW
    eprintf("IDirectDraw_SetDisplayMode: 0x%" PRIxPTR ", %i, %i, %i\n", (uintptr_t)lpThis, dwWidth, dwHeight, dwBPP);
#endif

    if (lpThis == NULL)
    {
        return DDERR_INVALIDPARAMS;
    }

    if ((dwBPP != 8) && (dwBPP != 16) && (dwBPP != 32))
    {
        return DDERR_UNSUPPORTEDMODE;
    }

    /* a new mode resizes the window of the last one; the picture is shown through
     * WinApi-d3dgl.c, and Direct3D draws there */
    lpThis->Window = Winapi_ScreenOpenGL(dwWidth, dwHeight);
    if (lpThis->Window == NULL) return DDERR_UNSUPPORTEDMODE;
    Game_ShowWindow(lpThis->Window);
    winapi_dd_window_id = SDL_GetWindowID(lpThis->Window);

    /* The window was just shown; a real Windows would send WM_ACTIVATEAPP */
    Winapi_PostActivateApp(1);

    winapi_dd_screen = lpThis;
    lpThis->width = dwWidth;
    lpThis->height = dwHeight;
    lpThis->bpp = dwBPP;

    return DD_OK;
}

uint32_t CCALL IDirectDraw_GetAvailableVidMem_c(struct IDirectDraw_c *lpThis, void * lpDDSCaps, uint32_t * lpdwTotal, uint32_t * lpdwFree)
{
    lpThis = DDBase(lpThis);
    (void)lpThis; (void)lpDDSCaps;
    if (lpdwTotal != NULL) *lpdwTotal = 16 * 1024 * 1024;
    if (lpdwFree != NULL) *lpdwFree = 16 * 1024 * 1024;
    return DD_OK;
}

uint32_t CCALL IDirectDraw_WaitForVerticalBlank_c(struct IDirectDraw_c *lpThis, uint32_t /*param1*/, void * /*param2*/)
{
    lpThis = DDBase(lpThis);
    return DD_OK;
}


static const uint8_t IID_IDirectDrawSurface[16] = { 0x81, 0xdb, 0x14, 0x6c, 0x33, 0xa7, 0xce, 0x11, 0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60 };
static const uint8_t IID_IDirectDrawSurface2[16] = { 0x85, 0x58, 0x80, 0x57, 0xec, 0x6e, 0xcf, 0x11, 0x94, 0x41, 0xa8, 0x23, 0x03, 0xc1, 0x0e, 0x27 };
static const uint8_t IID_IDirectDrawSurface3[16] = { 0x00, 0x4e, 0x04, 0xda, 0xb2, 0x69, 0xd0, 0x11, 0xa1, 0xd5, 0x00, 0xaa, 0x00, 0xb8, 0xdf, 0xbb };

uint32_t CCALL IDirectDrawSurface_QueryInterface_c(struct IDirectDrawSurface_c *lpThis, void * riid, PTR32(void)* ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;
    if ((lpThis != NULL) && (riid != NULL) &&
        ((memcmp(riid, IID_IDirectDrawSurface, 16) == 0) || (memcmp(riid, IID_IDirectDrawSurface2, 16) == 0) ||
         (memcmp(riid, IID_IDirectDrawSurface3, 16) == 0)))
    {
        /* the later interfaces only add GetDDInterface, PageLock, PageUnlock (2) and
         * SetSurfaceDesc (3) at the end of the vtable: one object serves all three */
        lpThis->RefCount++;
        *ppvObj = (void *)lpThis;
        return DD_OK;
    }
    if ((lpThis != NULL) && (riid != NULL) && Winapi_D3dSurfaceQuery(lpThis, riid, (uint32_t *)ppvObj))
    {
        return DD_OK;
    }
    *ppvObj = (void *)NULL;
    return E_NOINTERFACE;
}

uint32_t CCALL IDirectDrawSurface_AddRef_c(struct IDirectDrawSurface_c *lpThis)
{
    if (lpThis == NULL) return 0;
    lpThis->RefCount++;
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawSurface_Release_c(struct IDirectDrawSurface_c *lpThis)
{
    if (lpThis == NULL) return 0;

    lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        if (lpThis->lpBackbuffer != NULL)
        {
            /* the flipping chain's reference; GetAttachedSurface handed out others */
            IDirectDrawSurface_Release_c(lpThis->lpBackbuffer);
            lpThis->lpBackbuffer = NULL;
        }
        if ((lpThis->dd != NULL) && (lpThis->dd->Primary == lpThis))
        {
            lpThis->dd->Primary = NULL;
        }
        FreeSurface(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawSurface_AddAttachedSurface_c(struct IDirectDrawSurface_c *lpThis, struct IDirectDrawSurface_c * param1)
{
    if ((lpThis == NULL) || (param1 == NULL)) return DDERR_INVALIDPARAMS;
    if (param1->zbuffer)
    {
        /* a Z-buffer for Direct3D: kept with the surface it was attached to */
        if (lpThis->zsurf != NULL) return DDERR_SURFACEBUSY;
        param1->RefCount++;
        lpThis->zsurf = param1;
        return DD_OK;
    }
    eprintf("Unsupported method: %s\n", "IDirectDrawSurface_AddAttachedSurface");
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_AddOverlayDirtyRect_c(struct IDirectDrawSurface_c */*lpThis*/, void * /*param1*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_Blt_c(struct IDirectDrawSurface_c *lpThis, struct _rect *lpDestRect, struct IDirectDrawSurface_c * lpDDSrcSurface, struct _rect *lpSrcRect, uint32_t dwFlags, struct _ddbltfx * lpDDBltFX)
{
    struct _rect d, s;
    uint32_t result;

#if DEBUG_DDRAW
    eprintf("IDirectDrawSurface_Blt: 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%" PRIxPTR ", 0x%x, 0x%" PRIxPTR "\n", (uintptr_t)lpThis, (uintptr_t)lpDestRect, (uintptr_t)lpDDSrcSurface, (uintptr_t)lpSrcRect, dwFlags, (uintptr_t)lpDDBltFX);
#endif

    if (lpThis == NULL) return DDERR_INVALIDPARAMS;

    ClipRect(lpDestRect, lpThis->width, lpThis->height, &d);

#ifdef GAME_D3D_GL
    /* mid-scene the FBO has the 3D: the guest reads it before the blit and
     * writes under it after (the calls no-op outside a scene of the target) */
    if (Winapi_Direct3DGL())
    {
        Winapi_D3dGLSync(lpThis);
        if (lpDDSrcSurface != NULL) Winapi_D3dGLSync(lpDDSrcSurface);
    }
#endif

    if (dwFlags & DDBLT_COLORFILL)
    {
        int32_t x, y;

        if (lpDDBltFX == NULL) return DDERR_INVALIDPARAMS;

        for (y = d.top; y < d.bottom; y++)
        {
            if ((y < 0) || (y >= (int32_t)lpThis->height)) continue;
            for (x = d.left; x < d.right; x++)
            {
                if ((x < 0) || (x >= (int32_t)lpThis->width)) continue;
                PutPixel(lpThis->pixels + y * lpThis->pitch + x * (lpThis->bpp >> 3), lpThis->bpp, lpDDBltFX->dwFillColor);
            }
        }
        result = DD_OK;
    }
    else
    {
        const ddcolorkey *ckey = NULL;

        if (lpDDSrcSurface == NULL) return DDERR_INVALIDPARAMS;

        ClipRect(lpSrcRect, lpDDSrcSurface->width, lpDDSrcSurface->height, &s);

        if (dwFlags & DDBLT_KEYSRCOVERRIDE)
        {
            if (lpDDBltFX == NULL) return DDERR_INVALIDPARAMS;
            ckey = &lpDDBltFX->ddckSrcColorkey;
        }
        else if ((dwFlags & DDBLT_KEYSRC) && lpDDSrcSurface->has_colorkey)
        {
            static ddcolorkey key;
            key.dwColorSpaceLowValue = lpDDSrcSurface->colorkey_low;
            key.dwColorSpaceHighValue = lpDDSrcSurface->colorkey_high;
            ckey = &key;
        }

        result = BltRects(lpThis, d, lpDDSrcSurface, s, ckey);
        if ((result == DD_OK) && (lpThis->mip_base != NULL)) lpThis->mip_base->mips_filled = 1;
    }

#ifdef GAME_D3D_GL
    if (Winapi_Direct3DGL()) Winapi_D3dGLUpload(lpThis);
#endif

    if ((result == DD_OK) && lpThis->primary) Present(lpThis->dd);

    return result;
}

uint32_t CCALL IDirectDrawSurface_BltBatch_c(struct IDirectDrawSurface_c */*lpThis*/, void * /*param1*/, uint32_t /*param2*/, uint32_t /*param3*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_BltFast_c(struct IDirectDrawSurface_c *lpThis, uint32_t dwX, uint32_t dwY, struct IDirectDrawSurface_c * lpDDSrcSurface, void * lpSrcRect, uint32_t dwTrans)
{
    struct _rect d, s;
    ddcolorkey key;
    uint32_t result;

    if ((lpThis == NULL) || (lpDDSrcSurface == NULL)) return DDERR_INVALIDPARAMS;

#ifdef GAME_D3D_GL
    if (Winapi_Direct3DGL())
    {
        Winapi_D3dGLSync(lpThis);
        Winapi_D3dGLSync(lpDDSrcSurface);
    }
#endif

    ClipRect((const struct _rect *)lpSrcRect, lpDDSrcSurface->width, lpDDSrcSurface->height, &s);
    d.left = dwX;
    d.top = dwY;
    d.right = dwX + (s.right - s.left);
    d.bottom = dwY + (s.bottom - s.top);

    key.dwColorSpaceLowValue = lpDDSrcSurface->colorkey_low;
    key.dwColorSpaceHighValue = lpDDSrcSurface->colorkey_high;

    result = BltRects(lpThis, d, lpDDSrcSurface, s, ((dwTrans & DDBLTFAST_SRCCOLORKEY) && lpDDSrcSurface->has_colorkey) ? &key : NULL);

#ifdef GAME_D3D_GL
    if (Winapi_Direct3DGL()) Winapi_D3dGLUpload(lpThis);
#endif

    if ((result == DD_OK) && lpThis->primary) Present(lpThis->dd);

    return result;
}

uint32_t CCALL IDirectDrawSurface_DeleteAttachedSurface_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*param1*/, struct IDirectDrawSurface_c * /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_EnumAttachedSurfaces_c(struct IDirectDrawSurface_c */*lpThis*/, void * /*param1*/, void * /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_EnumOverlayZOrders_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*param1*/, void * /*param2*/, void * /*param3*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_Flip_c(struct IDirectDrawSurface_c *lpThis, struct IDirectDrawSurface_c * lpDDSurfaceTargetOverride, uint32_t /*dwFlags*/)
{
    struct IDirectDrawSurface_c *src;

    if ((lpThis == NULL) || !lpThis->primary) return DDERR_INVALIDPARAMS;

    src = (lpDDSurfaceTargetOverride != NULL) ? lpDDSurfaceTargetOverride : lpThis->lpBackbuffer;
    if (src == NULL) return DDERR_INVALIDPARAMS;

    /* the back buffer keeps its contents, the front buffer is a copy of it */
    memcpy(lpThis->pixels, src->pixels, lpThis->height * lpThis->pitch);
    lpThis->flipped = 1;
    Present(lpThis->dd);

    SDL_Delay(Display_DelayAfterFlip);

    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetAttachedSurface_c(struct IDirectDrawSurface_c *lpThis, struct _ddscaps2 *lpDDSCaps, PTR32(struct IDirectDrawSurface_c)* lplpDDAttachedSurface)
{
    if ((lpThis == NULL) || (lpDDSCaps == NULL) || (lplpDDAttachedSurface == NULL)) return DDERR_INVALIDPARAMS;

    if ((lpDDSCaps->dwCaps & DDSCAPS_BACKBUFFER) && lpThis->primary && (lpThis->lpBackbuffer != NULL))
    {
        /* as DirectDraw: the caller gets its own reference (and releases it) */
        lpThis->lpBackbuffer->RefCount++;
        *lplpDDAttachedSurface = lpThis->lpBackbuffer;
        return DD_OK;
    }

    if ((lpDDSCaps->dwCaps & DDSCAPS_ZBUFFER) && (lpThis->zsurf != NULL))
    {
        lpThis->zsurf->RefCount++;
        *lplpDDAttachedSurface = lpThis->zsurf;
        return DD_OK;
    }

    if ((lpDDSCaps->dwCaps & DDSCAPS_MIPMAP) && (lpThis->lpmip != NULL))
    {
        lpThis->lpmip->RefCount++;
        *lplpDDAttachedSurface = lpThis->lpmip;
        return DD_OK;
    }

    return DDERR_NOTFOUND;
}

uint32_t CCALL IDirectDrawSurface_GetBltStatus_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*param1*/)
{
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetCaps_c(struct IDirectDrawSurface_c *lpThis, void * param1)
{
    struct _ddscaps *caps = (struct _ddscaps *)param1;

    if ((lpThis == NULL) || (caps == NULL)) return DDERR_INVALIDPARAMS;

    caps->dwCaps =
        ((lpThis->primary)?DDSCAPS_PRIMARYSURFACE:((lpThis->backbuffer)?DDSCAPS_BACKBUFFER:DDSCAPS_OFFSCREENPLAIN)) |
        ((lpThis->primary)?DDSCAPS_VIDEOMEMORY:DDSCAPS_SYSTEMMEMORY) |
        (lpThis->caps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER));
    if (lpThis->lpmip != NULL) caps->dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    else if (lpThis->mip_base != NULL) caps->dwCaps |= DDSCAPS_MIPMAP;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetClipper_c(struct IDirectDrawSurface_c *lpThis, PTR32(void)* param1)
{
    if ((lpThis == NULL) || (param1 == NULL)) return DDERR_INVALIDPARAMS;
    if (lpThis->clipper == NULL) return DDERR_NOCLIPPERATTACHED;

    lpThis->clipper->RefCount++;
    *param1 = lpThis->clipper;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetColorKey_c(struct IDirectDrawSurface_c *lpThis, uint32_t /*dwFlags*/, void * lpDDColorKey)
{
    struct _ddcolorkey *key = (struct _ddcolorkey *)lpDDColorKey;

    if ((lpThis == NULL) || (key == NULL)) return DDERR_INVALIDPARAMS;
    if (!lpThis->has_colorkey) return DDERR_NOTFOUND;

    key->dwColorSpaceLowValue = lpThis->colorkey_low;
    key->dwColorSpaceHighValue = lpThis->colorkey_high;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetDC_c(struct IDirectDrawSurface_c */*lpThis*/, PTR32(void)* /*lphDC*/)
{
    /* no GDI device contexts */
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_GetFlipStatus_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*dwFlags*/)
{
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetOverlayPosition_c(struct IDirectDrawSurface_c */*lpThis*/, int32_t * /*param1*/, int32_t * /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_GetPalette_c(struct IDirectDrawSurface_c *lpThis, PTR32(void)* param1)
{
    if ((lpThis == NULL) || (param1 == NULL)) return DDERR_INVALIDPARAMS;
    if (lpThis->palette == NULL) return MAKE_DDHRESULT(1);   /* DDERR_NOPALETTEATTACHED */

    lpThis->palette->RefCount++;
    *param1 = lpThis->palette;
    return DD_OK;
}

static void FillPixelFormat(struct IDirectDrawSurface_c *lpThis, struct _ddpixelformat *pf)
{
    memset(pf, 0, sizeof(*pf));
    pf->dwSize = sizeof(*pf);
    pf->dwFlags = (lpThis->bpp == 8) ? (DDPF_PALETTEINDEXED8 | DDPF_RGB) : DDPF_RGB;
    pf->dwRGBBitCount = lpThis->bpp;
    if ((lpThis->bpp == 16) && ((lpThis->rmask | lpThis->gmask | lpThis->bmask) != 0))
    {
        pf->dwRBitMask = lpThis->rmask;
        pf->dwGBitMask = lpThis->gmask;
        pf->dwBBitMask = lpThis->bmask;
        if (lpThis->amask != 0)
        {
            pf->dwFlags |= DDPF_ALPHAPIXELS;
            pf->dwRGBAlphaBitMask = lpThis->amask;
        }
    }
    else if (lpThis->bpp == 16)
    {
        pf->dwRBitMask = 0xf800;
        pf->dwGBitMask = 0x07e0;
        pf->dwBBitMask = 0x001f;
    }
    else if (lpThis->bpp == 32)
    {
        pf->dwRBitMask = 0x00ff0000;
        pf->dwGBitMask = 0x0000ff00;
        pf->dwBBitMask = 0x000000ff;
    }
}

uint32_t CCALL IDirectDrawSurface_GetPixelFormat_c(struct IDirectDrawSurface_c *lpThis, void * param1)
{
    if ((lpThis == NULL) || (param1 == NULL)) return DDERR_INVALIDPARAMS;

    FillPixelFormat(lpThis, (struct _ddpixelformat *)param1);
    return DD_OK;
}

static uint32_t GetSurfaceDesc(struct IDirectDrawSurface_c *lpThis, struct _ddsurfacedesc *lpDDSurfaceDesc)
{
    if (lpDDSurfaceDesc->dwSize != 108)
    {
        return DDERR_INVALIDPARAMS;
    }

    lpDDSurfaceDesc->dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
    lpDDSurfaceDesc->dwHeight = lpThis->height;
    lpDDSurfaceDesc->dwWidth = lpThis->width;
    lpDDSurfaceDesc->lPitch = lpThis->pitch;
    lpDDSurfaceDesc->dwBackBufferCount = (lpThis->lpBackbuffer != NULL)?1:0;
    if (lpThis->mip_count > 0)
    {
        lpDDSurfaceDesc->dwFlags |= DDSD_MIPMAPCOUNT;
        lpDDSurfaceDesc->dwMipMapCount = lpThis->mip_count;
    }
    lpDDSurfaceDesc->ddsCaps.dwCaps = lpThis->caps;
    if (lpThis->lpmip != NULL) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    else if (lpThis->mip_base != NULL) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_MIPMAP;
    /* texture surfaces live in the accelerator's memory here (the GL texture);
       the game keeps a texture page only if the desc says video memory */
    if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_TEXTURE) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY;
    if (lpThis->primary) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_PRIMARYSURFACE;
    if (lpThis->backbuffer) lpDDSurfaceDesc->ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
    if (lpDDSurfaceDesc->ddsCaps.dwCaps == 0)
    {
        lpDDSurfaceDesc->ddsCaps.dwCaps = ((lpThis->primary)?DDSCAPS_PRIMARYSURFACE:((lpThis->backbuffer)?DDSCAPS_BACKBUFFER:DDSCAPS_OFFSCREENPLAIN)) |
            ((lpThis->primary)?DDSCAPS_VIDEOMEMORY:DDSCAPS_SYSTEMMEMORY);
    }
    FillPixelFormat(lpThis, &lpDDSurfaceDesc->ddpfPixelFormat);

    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_GetSurfaceDesc_c(struct IDirectDrawSurface_c *lpThis, struct _ddsurfacedesc *lpDDSurfaceDesc)
{
    if ((lpThis == NULL) || (lpDDSurfaceDesc == NULL)) return DDERR_INVALIDPARAMS;

    return GetSurfaceDesc(lpThis, lpDDSurfaceDesc);
}

uint32_t CCALL IDirectDrawSurface_Initialize_c(struct IDirectDrawSurface_c */*lpThis*/, struct IDirectDraw_c * /*param1*/, void * /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_IsLost_c(struct IDirectDrawSurface_c *lpThis)
{
    if (lpThis == NULL) return DDERR_INVALIDPARAMS;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_Lock_c(struct IDirectDrawSurface_c *lpThis, void * lpDestRect, struct _ddsurfacedesc *lpDDSurfaceDesc, uint32_t /*dwFlags*/, void * /*hEvent*/)
{
    const struct _rect *r = (const struct _rect *)lpDestRect;
    uint8_t *p;

    if ((lpThis == NULL) || (lpDDSurfaceDesc == NULL)) return DDERR_INVALIDPARAMS;

    if (lpThis->locked) return DDERR_SURFACEBUSY;

    if (GetSurfaceDesc(lpThis, lpDDSurfaceDesc) != DD_OK) return DDERR_INVALIDPARAMS;

    p = lpThis->pixels;
    if (r != NULL)
    {
        if ((r->left < 0) || (r->top < 0) || (r->right > (int32_t)lpThis->width) || (r->bottom > (int32_t)lpThis->height) || (r->left >= r->right) || (r->top >= r->bottom))
        {
            return DDERR_INVALIDPARAMS;
        }
        p += r->top * lpThis->pitch + r->left * (lpThis->bpp >> 3);
    }

    lpDDSurfaceDesc->dwFlags |= DDSD_LPSURFACE;
    lpDDSurfaceDesc->lpSurface = p;
    lpThis->locked = 1;

#ifdef GAME_D3D_GL
    /* the guest reads the pixels mid-scene: the memory wants the 3D so far */
    if (Winapi_Direct3DGL()) Winapi_D3dGLSync(lpThis);
#endif

    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_ReleaseDC_c(struct IDirectDrawSurface_c */*lpThis*/, void * /*param1*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_Restore_c(struct IDirectDrawSurface_c */*lpThis*/)
{
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_SetClipper_c(struct IDirectDrawSurface_c *lpThis, void * param1)
{
    struct IDirectDrawClipper_c *clipper = (struct IDirectDrawClipper_c *)param1;

    if (lpThis == NULL) return DDERR_INVALIDPARAMS;

    /* the picture is drawn whole into the window: the clipper only has to be held, as
     * the palette is (released with the surface; the object itself is the game's) */
    if (clipper != NULL) clipper->RefCount++;
    if (lpThis->clipper != NULL) lpThis->clipper->RefCount--;
    lpThis->clipper = clipper;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_SetColorKey_c(struct IDirectDrawSurface_c *lpThis, uint32_t dwFlags, void * lpDDColorKey)
{
    const struct _ddcolorkey *key = (const struct _ddcolorkey *)lpDDColorKey;

    if (lpThis == NULL) return DDERR_INVALIDPARAMS;

    if (dwFlags & DDCKEY_SRCBLT)
    {
        struct IDirectDrawSurface_c *m;

        if (key == NULL)
        {
            lpThis->has_colorkey = 0;
        }
        else
        {
            lpThis->has_colorkey = 1;
            lpThis->colorkey_low = key->dwColorSpaceLowValue;
            lpThis->colorkey_high = (dwFlags & 1) ? key->dwColorSpaceHighValue : key->dwColorSpaceLowValue;
        }
        /* the key of a texture is the key of every mip level */
        for (m = lpThis->lpmip; m != NULL; m = m->lpmip)
        {
            m->has_colorkey = lpThis->has_colorkey;
            m->colorkey_low = lpThis->colorkey_low;
            m->colorkey_high = lpThis->colorkey_high;
        }
        TextureTouched(lpThis);
    }
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_SetOverlayPosition_c(struct IDirectDrawSurface_c */*lpThis*/, int32_t /*param1*/, int32_t /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_SetPalette_c(struct IDirectDrawSurface_c *lpThis, void * param1)
{
    struct IDirectDrawPalette_c *pal = (struct IDirectDrawPalette_c *)param1;

    if (lpThis == NULL) return DDERR_INVALIDPARAMS;

    if (pal != NULL) pal->RefCount++;
    if (lpThis->palette != NULL) lpThis->palette->RefCount--;
    lpThis->palette = pal;
    TextureTouched(lpThis);

    /* the same palette for the whole chain until a level gets its own */
    if (lpThis->lpmip != NULL)
    {
        struct IDirectDrawSurface_c *m;
        for (m = lpThis->lpmip; m != NULL; m = m->lpmip)
        {
            if (pal != NULL) pal->RefCount++;
            if (m->palette != NULL) m->palette->RefCount--;
            m->palette = pal;
            m->texture_gen++;
        }
    }

    if (lpThis->primary) Present(lpThis->dd);
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_Unlock_c(struct IDirectDrawSurface_c *lpThis, void * /*lpRect*/)
{
    if (lpThis == NULL) return DDERR_INVALIDPARAMS;

    lpThis->locked = 0;
    TextureTouched(lpThis);
#ifdef GAME_D3D_GL
    /* the guest wrote the pixels (or a palette): under the 3D drawn so far */
    if (Winapi_Direct3DGL()) Winapi_D3dGLUpload(lpThis);
#endif
    if (lpThis->primary) Present(lpThis->dd);
    else if (lpThis->backbuffer && lpThis->dd->Primary != NULL && !lpThis->dd->Primary->flipped)
    {
        /* the game draws into the back buffer and never flips it; one that flips
         * (clears the back buffer, draws, flips) shows only whole frames */
        struct IDirectDrawSurface_c *prim = lpThis->dd->Primary;
        memcpy(prim->pixels, lpThis->pixels, prim->height * prim->pitch);
        Present(lpThis->dd);
    }

    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_UpdateOverlay_c(struct IDirectDrawSurface_c */*lpThis*/, void * /*param1*/, struct IDirectDrawSurface_c * /*param2*/, void * /*param3*/, uint32_t /*param4*/, void * /*param5*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_UpdateOverlayDisplay_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*param1*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_UpdateOverlayZOrder_c(struct IDirectDrawSurface_c */*lpThis*/, uint32_t /*param1*/, struct IDirectDrawSurface_c * /*param2*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawSurface_GetDDInterface_c(struct IDirectDrawSurface_c *lpThis, PTR32(void)* lplpDD)
{
    if ((lpThis == NULL) || (lplpDD == NULL)) return DDERR_INVALIDPARAMS;
    IDirectDraw_AddRef_c(lpThis->dd);
    *lplpDD = (void *)lpThis->dd;
    return DD_OK;
}

uint32_t CCALL IDirectDrawSurface_PageLock_c(struct IDirectDrawSurface_c *lpThis, uint32_t dwFlags)
{
    /* the pixels are host memory, always present */
    (void)dwFlags;
    return (lpThis != NULL) ? DD_OK : DDERR_INVALIDPARAMS;
}

uint32_t CCALL IDirectDrawSurface_PageUnlock_c(struct IDirectDrawSurface_c *lpThis, uint32_t dwFlags)
{
    (void)dwFlags;
    return (lpThis != NULL) ? DD_OK : DDERR_INVALIDPARAMS;
}

/* DirectDraw 4: the surface takes the memory the program gives it (DDSD_LPSURFACE), for a
 * texture the program fills in a buffer of its own; the owned memory stays as it is */
uint32_t CCALL IDirectDrawSurface_SetSurfaceDesc_c(struct IDirectDrawSurface_c *lpThis, struct _ddsurfacedesc *lpDDSD, uint32_t dwFlags)
{
    (void)dwFlags;
    if ((lpThis == NULL) || (lpDDSD == NULL)) return DDERR_INVALIDPARAMS;
    if ((lpDDSD->dwFlags & DDSD_LPSURFACE) == 0) return DDERR_UNSUPPORTED;
    if ((void *)lpDDSD->lpSurface == NULL) return DDERR_INVALIDPARAMS;

    lpThis->pixels = (uint8_t *)(void *)lpDDSD->lpSurface;
    if ((lpDDSD->dwFlags & DDSD_PITCH) && (lpDDSD->lPitch > 0)) lpThis->pitch = (uint32_t)lpDDSD->lPitch;
    lpThis->texture_gen++;
    return DD_OK;
}


uint32_t CCALL IDirectDrawClipper_QueryInterface_c(struct IDirectDrawClipper_c *lpThis, void * riid, PTR32(void)* ppvObj)
{
    (void)lpThis; (void)riid;
    if (ppvObj == NULL) return E_POINTER;
    *ppvObj = (void *)NULL;
    return E_NOINTERFACE;
}

uint32_t CCALL IDirectDrawClipper_AddRef_c(struct IDirectDrawClipper_c *lpThis)
{
    if (lpThis == NULL) return 0;
    lpThis->RefCount++;
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawClipper_Release_c(struct IDirectDrawClipper_c *lpThis)
{
    if (lpThis == NULL) return 0;

    lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawClipper_GetClipList_c(struct IDirectDrawClipper_c *lpThis, void * lpRect, void * lpClipList, uint32_t * lpdwSize)
{
    (void)lpThis; (void)lpRect; (void)lpClipList; (void)lpdwSize;
    return DDERR_NOCLIPLIST;
}

uint32_t CCALL IDirectDrawClipper_GetHWnd_c(struct IDirectDrawClipper_c *lpThis, PTR32(void)* lphWnd)
{
    (void)lpThis;
    if (lphWnd == NULL) return DDERR_INVALIDPARAMS;
    *lphWnd = (void *)NULL;
    return DD_OK;
}

uint32_t CCALL IDirectDrawClipper_Initialize_c(struct IDirectDrawClipper_c *lpThis, void * lpDD, uint32_t dwFlags)
{
    (void)lpThis; (void)lpDD; (void)dwFlags;
    return DD_OK;
}

uint32_t CCALL IDirectDrawClipper_IsClipListChanged_c(struct IDirectDrawClipper_c *lpThis, uint32_t * lpbChanged)
{
    (void)lpThis;
    if (lpbChanged == NULL) return DDERR_INVALIDPARAMS;
    *lpbChanged = 0;
    return DD_OK;
}

uint32_t CCALL IDirectDrawClipper_SetClipList_c(struct IDirectDrawClipper_c *lpThis, void * lpClipList, uint32_t dwFlags)
{
    (void)lpThis; (void)lpClipList; (void)dwFlags;
    return DD_OK;
}

uint32_t CCALL IDirectDrawClipper_SetHWnd_c(struct IDirectDrawClipper_c *lpThis, uint32_t dwFlags, void * hWnd)
{
    (void)lpThis; (void)dwFlags; (void)hWnd;
    return DD_OK;
}

uint32_t CCALL IDirectDrawPalette_QueryInterface_c(struct IDirectDrawPalette_c */*lpThis*/, void * /*riid*/, PTR32(void)* ppvObj)
{
    if (ppvObj == NULL) return E_POINTER;
    return E_NOINTERFACE;
}

uint32_t CCALL IDirectDrawPalette_AddRef_c(struct IDirectDrawPalette_c *lpThis)
{
    if (lpThis == NULL) return 0;
    lpThis->RefCount++;
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawPalette_Release_c(struct IDirectDrawPalette_c *lpThis)
{
    if (lpThis == NULL) return 0;

    lpThis->RefCount--;
    if (lpThis->RefCount == 0)
    {
        x86_free(lpThis);
        return 0;
    }
    return lpThis->RefCount;
}

uint32_t CCALL IDirectDrawPalette_GetCaps_c(struct IDirectDrawPalette_c *lpThis, uint32_t * param1)
{
    if ((lpThis == NULL) || (param1 == NULL)) return DDERR_INVALIDPARAMS;
    *param1 = DDPCAPS_8BIT;
    return DD_OK;
}

uint32_t CCALL IDirectDrawPalette_GetEntries_c(struct IDirectDrawPalette_c *lpThis, uint32_t /*dwFlags*/, uint32_t dwBase, uint32_t dwNumEntries, void * lpEntries)
{
    uint8_t *out = (uint8_t *)lpEntries;
    uint32_t i;

    if ((lpThis == NULL) || (out == NULL) || (dwBase > 256) || (dwNumEntries > 256 - dwBase)) return DDERR_INVALIDPARAMS;

    for (i = 0; i < dwNumEntries; i++)
    {
        uint32_t c = lpThis->argb[dwBase + i];
        out[i * 4] = c >> 16;
        out[i * 4 + 1] = c >> 8;
        out[i * 4 + 2] = c;
        out[i * 4 + 3] = 0;
    }
    return DD_OK;
}

uint32_t CCALL IDirectDrawPalette_Initialize_c(struct IDirectDrawPalette_c */*lpThis*/, void * /*param1*/, uint32_t /*param2*/, void * /*param3*/)
{
    return DDERR_UNSUPPORTED;
}

uint32_t CCALL IDirectDrawPalette_SetEntries_c(struct IDirectDrawPalette_c *lpThis, uint32_t /*dwFlags*/, uint32_t dwStartingEntry, uint32_t dwCount, void * lpEntries)
{
    const uint8_t *in = (const uint8_t *)lpEntries;
    uint32_t i;

    if ((lpThis == NULL) || (in == NULL) || (dwStartingEntry > 256) || (dwCount > 256 - dwStartingEntry)) return DDERR_INVALIDPARAMS;

    for (i = 0; i < dwCount; i++)
    {
        lpThis->argb[dwStartingEntry + i] = 0xff000000u | (in[i * 4] << 16) | (in[i * 4 + 1] << 8) | in[i * 4 + 2];
    }

#ifdef GAME_D3D_GL
    /* the 8-bit textures are read through the palette when they are made */
    if (Winapi_Direct3DGL()) Winapi_D3dGLPaletteChanged(lpThis);
#endif

    /* a palette change shows at once on the screen */
    if (winapi_dd_screen != NULL) Present(winapi_dd_screen);
    return DD_OK;
}


SDL_Window *Winapi_ScreenOpenGL(uint32_t width, uint32_t height)
{
    if (dd_gl_window != NULL)
    {
        /* the window of Winapi_EarlyWindow or of the last mode gets the size of the game's mode */
        Game_WindowFit(dd_gl_window, width, height);
        Game_ShowWindow(dd_gl_window);
        return dd_gl_window;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        eprintf("Error: SDL_InitSubSystem: Video: %s\n", SDL_GetError());
        return NULL;
    }

#if defined(GAME_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    dd_gl_window = Game_WindowCreate(width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (dd_gl_window == NULL)
    {
        eprintf("Error: SDL_CreateWindow: %s\n", SDL_GetError());
        return NULL;
    }

    Game_ShowWindow(dd_gl_window);
#ifdef GAME_EARLY_GL_WINDOW
    SDL_RaiseWindow(dd_gl_window);
#endif
    winapi_dd_window_id = SDL_GetWindowID(dd_gl_window);
    Winapi_PostActivateApp(1);
    return dd_gl_window;
}

#ifdef GAME_EARLY_GL_WINDOW
/* The window at the start of the program, before the game's long loading: a desktop
 * lets a window appear in front when it comes up right after the launch, not many seconds
 * later.  It gets one black frame (a Wayland window is mapped by its first buffer).  Only
 * with Direct3D on OpenGL (3D=software has its window with the first display mode). */
void Winapi_EarlyWindow(uint32_t width, uint32_t height)
{
    SDL_Window *window;
    SDL_GLContext context;
    void (SDLCALL *clear_color)(float, float, float, float);
    void (SDLCALL *clear)(unsigned int);

    if (!Winapi_Direct3DEnabled() || !Winapi_Direct3DGL()) return;
    window = Winapi_ScreenOpenGL(width, height);
    if (window == NULL) return;
    context = SDL_GL_CreateContext(window);
    if (context == NULL) return;
    clear_color = (void (SDLCALL *)(float, float, float, float))SDL_GL_GetProcAddress("glClearColor");
    clear = (void (SDLCALL *)(unsigned int))SDL_GL_GetProcAddress("glClear");
    if (clear_color != NULL && clear != NULL)
    {
        clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        clear(0x4000); /* GL_COLOR_BUFFER_BIT */
    }
    SDL_GL_SwapWindow(window);
    SDL_GL_MakeCurrent(window, NULL);
    SDL_GL_DeleteContext(context);
}
#endif

void Winapi_ScreenCloseGL(void)
{
    if (dd_gl_window != NULL)
    {
        /* hidden, not destroyed, as in IDirectDraw_Release; Winapi_DestroyDirectDrawWindow
         * and SDL_Quit take it down */
        Game_HideWindow(dd_gl_window);
    }
}
