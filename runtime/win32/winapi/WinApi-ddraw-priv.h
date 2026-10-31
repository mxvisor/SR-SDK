/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer (WinApi-ddraw.c)
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

/* The DirectDraw objects, for the files that work on them (WinApi-d3d.c). */

#if !defined(_WINAPI_DDRAW_PRIV_H_INCLUDED_)
#define _WINAPI_DDRAW_PRIV_H_INCLUDED_

#include <SDL.h>
#include "ptr32.h"
#include "platform.h"

#if defined(PTROFS_64BIT)
extern uint64_t pointer_offset;
#define GUEST_ADDR(p) ((uint32_t)((p) ? ((uintptr_t)(p) - pointer_offset) : 0))
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))
#else
#define GUEST_ADDR(p) ((uint32_t)(uintptr_t)(p))
#define GUEST_MEM(a) ((void *)(uintptr_t)(a))
#endif

struct IDirectDraw_c {
    PTR32(void) lpVtbl;
    uint32_t RefCount;
    SDL_Window *Window;
    SDL_Renderer *Renderer;
    SDL_Texture *Texture;
    uint32_t width, height, bpp;
    struct IDirectDrawSurface_c *Primary;
    struct IDirectDraw_c *base;     /* IDirectDraw2 proxy only: the object it stands for */
    struct IDirectDraw_c *proxy;    /* the IDirectDraw2 proxy of this object, once asked for */
};

/* the object a method works on: a program that asked for IDirectDraw2 holds the proxy */
#define DDBase(p) (((p) != NULL && (p)->base != NULL) ? (p)->base : (p))

struct IDirectDrawClipper_c {
    PTR32(void) lpVtbl;
    uint32_t RefCount;
};

struct IDirectDrawPalette_c {
    PTR32(void) lpVtbl;
    uint32_t RefCount;
    uint32_t entries;
    uint32_t argb[256];
};

struct IDirectDrawSurface_c {
    PTR32(void) lpVtbl;
    uint32_t RefCount;
    struct IDirectDraw_c *dd;
    uint32_t width, height, bpp, pitch;
    uint8_t *mem;
    uint8_t *pixels;
    int primary, backbuffer, locked;
    int flipped;                    /* primary: the game has flipped it, Flip shows its frames */
    int has_colorkey;
    uint32_t colorkey_low, colorkey_high;
    struct IDirectDrawPalette_c *palette;
    struct IDirectDrawClipper_c *clipper;
    struct IDirectDrawSurface_c *lpBackbuffer;
    uint32_t caps;                  /* DDSCAPS_* the surface was made with */
    int zbuffer;                    /* a Z-buffer (pixel format DDPF_ZBUFFER) */
    struct IDirectDrawSurface_c *zsurf;  /* the Z-buffer attached to this surface */
    struct IDirectDrawSurface_c *lpmip;  /* the next mip level of a texture (the chain we own) */
    uint32_t mip_count;             /* levels of the chain, 0 when the surface has none */
    struct IDirectDrawSurface_c *mip_base;   /* a mip level: the texture's level 0 */
    uint32_t mips_filled;           /* level 0: the game wrote a level above 0 (mipmapping on) */
    void *d3dtex;                   /* its IDirect3DTexture2 object, once asked for (WinApi-d3d.c) */
    uint32_t texture_gen;           /* counts the changes of a texture's pixels and palette */
    uint32_t rmask, gmask, bmask, amask;    /* the channels of a 16 bit surface made with a pixel format */
};

uint32_t Winapi_DDrawCopyLevels(struct IDirectDrawSurface_c *dst, struct IDirectDrawSurface_c *src);

#endif
