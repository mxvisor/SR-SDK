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

/* Direct3D 2 (DirectX 5) for the games that use it: the COM objects of d3dim.dll over the
 * DirectDraw surfaces of WinApi-ddraw.c.  A game asks IDirectDraw::QueryInterface for
 * IDirect3D2, enumerates the devices, makes a device on a 3D-capable surface, takes
 * textures from the surfaces, makes a viewport and a material, and draws pre-transformed
 * (D3DTLVERTEX) primitives with DrawPrimitive and DrawIndexedPrimitive.  This file is
 * the part that answers the game: the objects, the description of the one device (the
 * "HAL": the rasteriser of the port) and the render state.  The drawing is not here yet. */

#include "WinApi-d3d.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "Game-Memory.h"
#include "WinApi-ddraw.h"
#include "WinApi-ddraw-priv.h"
#include "WinApi-game.h"
#include "Game_hooks.h"
#include "WinApi-guest.h"

#ifndef GAME_DIRECT3D
#define GAME_DIRECT3D 0     /* a game that uses Direct3D says so in Game_hooks.h */
#endif

#define D3D_OK              0u
#define D3DERR_GENERIC      0x80004005u     /* E_FAIL */
#define D3DERR_NOTIMPL      0x80004001u     /* E_NOTIMPL */
#define D3DERR_NOINTERFACE  0x80004002u
#define D3DERR_POINTER      0x80004003u
#define D3DERR_INVALIDARG   0x80070057u
#define D3DERR_NOTFOUND     0x887600ffu     /* DDERR_NOTFOUND */

#define DDSCAPS_ZBUFFER     0x00020000u

#define D3DCLEAR_TARGET     1u
#define D3DCLEAR_ZBUFFER    2u

#define MAX_STATES          64
#define MAX_VIEWPORTS       8
#define MAX_TEXTURES        1024
#define MAX_MATERIALS       64

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

/* <GAME>_D3D_LOG set: the calls and the state changes on stderr */
static int d3d_logging(void)
{
    static int v = -1;
    if (v < 0) v = (getenv(GAME_ENV("D3D_LOG")) != NULL);
    return v;
}
#define d3d_log(...) do { if (d3d_logging()) eprintf(__VA_ARGS__); } while (0)

/* <GAME>_D3D=0/1 overrides the game's choice (GAME_DIRECT3D) */
int Winapi_Direct3DEnabled(void)
{
    static int v = -1;
    if (v < 0)
    {
        const char *e = getenv(GAME_ENV("D3D"));
        v = (e != NULL) ? (atoi(e) != 0) : (GAME_DIRECT3D != 0);
    }
    return v;
}

/* ---- the objects: guest-visible, a vtable pointer first ---- */

typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    struct IDirectDraw_c *dd;
} d3d_t;

struct d3d_viewport;

typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_t *d3d;
    struct IDirectDrawSurface_c *target;
    struct d3d_viewport *viewports[MAX_VIEWPORTS];
    struct d3d_viewport *current;
    uint32_t state[MAX_STATES];
    int in_scene;
    uint32_t draws;                     /* DrawPrimitive calls in the scene */
    uint32_t vertices;
} d3d_device;

typedef struct d3d_viewport {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_device *device;
    uint32_t vp[11];                    /* D3DVIEWPORT / D3DVIEWPORT2 as the game set it */
    int has_vp;
    uint32_t background;                /* material handle */
} d3d_viewport;

typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    uint32_t mat[20];                   /* D3DMATERIAL, 80 bytes */
    uint32_t handle;
} d3d_material;

typedef struct {
    uint32_t lpVtbl;
    struct IDirectDrawSurface_c *surface;
    uint32_t handle;
} d3d_texture;

extern uint32_t IDirect3D2Vtbl_asm2c;
extern uint32_t IDirect3DDevice2Vtbl_asm2c;
extern uint32_t IDirect3DViewport2Vtbl_asm2c;
extern uint32_t IDirect3DMaterial2Vtbl_asm2c;
extern uint32_t IDirect3DTexture2Vtbl_asm2c;

/* handle - 1 indexes these */
static d3d_texture *texture_handles[MAX_TEXTURES];
static d3d_material *material_handles[MAX_MATERIALS];

/* ---- GUIDs ---- */
static const uint8_t IID_IUnknown_[16]           = { 0,0,0,0, 0,0, 0,0, 0xc0,0,0,0,0,0,0,0x46 };
static const uint8_t IID_IDirect3D2_[16]         = { 0xc1,0x1e,0xae,0x6a, 0x2a,0x66, 0xd0,0x11, 0x88,0x9d,0x00,0xaa,0x00,0xbb,0xb7,0x6a };
static const uint8_t IID_IDirect3DDevice2_[16]   = { 0x01,0x15,0x28,0x93, 0xf8,0x8c, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DTexture2_[16]  = { 0x02,0x15,0x28,0x93, 0xf8,0x8c, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DViewport2_[16] = { 0x03,0x15,0x28,0x93, 0xf8,0x8c, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DMaterial2_[16] = { 0x02,0x4a,0xcf,0x93, 0x6e,0x21, 0xd1,0x11, 0xa7,0x97,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_HALDevice[16]           = { 0xe0,0x3d,0xe6,0x84, 0xaa,0x46, 0xcf,0x11, 0x81,0x6f,0x00,0x00,0xc0,0x20,0x15,0x6e };
static const uint8_t IID_RGBDevice[16]           = { 0x60,0x5c,0x66,0xa4, 0x73,0x26, 0xcf,0x11, 0xa4,0x9e,0x00,0xaa,0x00,0x6c,0x32,0x13 };

static int same_guid(const void *a, const uint8_t *b)
{
    return (a != NULL) && (memcmp(a, b, 16) == 0);
}

/* QueryInterface of an object that is only itself (and IUnknown) */
static uint32_t query_self(void *obj, uint32_t *refcount, const void *riid, const uint8_t *iid, void *ppvObj)
{
    uint32_t *out = (uint32_t *)ppvObj;
    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IUnknown_) || same_guid(riid, iid))
    {
        (*refcount)++;
        *out = GUEST_ADDR(obj);
        return D3D_OK;
    }
    *out = 0;
    return D3DERR_NOINTERFACE;
}

static void *new_object(size_t size, uint32_t *vtbl)
{
    uint32_t *o = (uint32_t *)x86_malloc(size);
    if (o == NULL) return NULL;
    memset(o, 0, size);
    o[0] = GUEST_ADDR(vtbl);
    o[1] = 1;
    return o;
}

/* ---- the description of the device: what the rasteriser of the port can do ---- */

#define D3DDEVICEDESC_V1_SIZE 0xccu     /* DirectX 5 */

static void fill_prim_caps(uint32_t *c)
{
    c[0]  = 56;             /* dwSize */
    c[1]  = 0x70;           /* dwMiscCaps: CULLNONE | CULLCW | CULLCCW */
    c[2]  = 0x1 | 0x10 | 0x20 | 0x80 | 0x4000;   /* dwRasterCaps: DITHER | ZTEST | SUBPIXEL | FOGVERTEX | ZBIAS */
    c[3]  = 0xff;           /* dwZCmpCaps: all */
    c[4]  = 0x1fff;         /* dwSrcBlendCaps: all */
    c[5]  = 0x1fff;         /* dwDestBlendCaps: all */
    c[6]  = 0xff;           /* dwAlphaCmpCaps: all */
    c[7]  = 0x2 | 0x8 | 0x80 | 0x200 | 0x1000 | 0x4000 | 0x40000 | 0x80000;   /* dwShadeCaps: flat and gouraud colour and specular, alpha blend, fog */
    c[8]  = 0x1 | 0x2 | 0x4 | 0x8;   /* dwTextureCaps: PERSPECTIVE | POW2 | ALPHA | TRANSPARENCY */
    c[9]  = 0x3;            /* dwTextureFilterCaps: NEAREST | LINEAR */
    c[10] = 0x1 | 0x2 | 0x4 | 0x8 | 0x40;    /* dwTextureBlendCaps: DECAL | MODULATE | DECALALPHA | MODULATEALPHA | COPY */
    c[11] = 0x1 | 0x2 | 0x4;    /* dwTextureAddressCaps: WRAP | MIRROR | CLAMP */
    c[12] = 32;             /* dwStippleWidth */
    c[13] = 32;             /* dwStippleHeight */
}

/* hw != 0: the description of the hardware part; else of the software emulation, left empty */
static void fill_device_desc(uint32_t *d, int hw)
{
    memset(d, 0, D3DDEVICEDESC_V1_SIZE);
    d[0] = D3DDEVICEDESC_V1_SIZE;
    if (!hw) return;
    d[1] = 0x7ff;           /* dwFlags: every group below is valid */
    d[2] = 2;               /* dcmColorModel: D3DCOLOR_RGB */
    d[3] = 0x1 | 0x20 | 0x80 | 0x200 | 0x400 | 0x800;   /* dwDevCaps: FLOATTLVERTEX | EXECUTEVIDEOMEMORY | TLVERTEXVIDEOMEMORY | TEXTUREVIDEOMEMORY | DRAWPRIMTLVERTEX | CANRENDERAFTERFLIP */
    d[4] = 8;               /* dtcTransformCaps.dwSize */
    d[5] = 1;               /* D3DTRANSFORMCAPS_CLIP */
    d[6] = 1;               /* bClipping */
    d[7] = 16;              /* dlcLightingCaps.dwSize */
    d[8] = 0;
    d[9] = 1;               /* D3DLIGHTINGMODEL_RGB */
    d[10] = 0;              /* dwNumLights */
    fill_prim_caps(d + 11); /* dpcLineCaps */
    fill_prim_caps(d + 25); /* dpcTriCaps */
    d[39] = 0x400 | 0x100;  /* dwDeviceRenderBitDepth: DDBD_16 | DDBD_32 */
    d[40] = 0x400;          /* dwDeviceZBufferBitDepth: DDBD_16 */
    d[41] = 0;              /* dwMaxBufferSize */
    d[42] = 65536;          /* dwMaxVertexCount */
    d[43] = 1;              /* dwMinTextureWidth */
    d[44] = 1;              /* dwMinTextureHeight */
    d[45] = 256;            /* dwMaxTextureWidth */
    d[46] = 256;            /* dwMaxTextureHeight */
    d[47] = 1;              /* dwMinStippleWidth */
    d[48] = 32;             /* dwMaxStippleWidth */
    d[49] = 1;              /* dwMinStippleHeight */
    d[50] = 32;             /* dwMaxStippleHeight */
}

/* ---- IDirectDraw::QueryInterface(IID_IDirect3D2) ---- */

int Winapi_D3dQuery(struct IDirectDraw_c *dd, const void *riid, uint32_t *out)
{
    d3d_t *d3d;

    if (!Winapi_Direct3DEnabled() || !same_guid(riid, IID_IDirect3D2_)) return 0;

    d3d = (d3d_t *)new_object(sizeof(d3d_t), &IDirect3D2Vtbl_asm2c);
    if (d3d == NULL) { *out = 0; return 0; }
    d3d->dd = dd;
    dd->RefCount++;
    *out = GUEST_ADDR(d3d);
    d3d_log("D3D: IDirect3D2\n");
    return 1;
}

/* ---- IDirect3D2 ---- */

uint32_t CCALL IDirect3D2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_t *d3d = (d3d_t *)lpThis;
    return query_self(d3d, &d3d->RefCount, riid, IID_IDirect3D2_, ppvObj);
}

uint32_t CCALL IDirect3D2_AddRef_c(void *lpThis)
{
    return ++((d3d_t *)lpThis)->RefCount;
}

uint32_t CCALL IDirect3D2_Release_c(void *lpThis)
{
    d3d_t *d3d = (d3d_t *)lpThis;
    if (--d3d->RefCount != 0) return d3d->RefCount;
    IDirectDraw_Release_c(d3d->dd);
    x86_free(d3d);
    return 0;
}

/* The devices, to the callback of the game:
 *   BOOL cb(GUID *guid, char *description, char *name, D3DDEVICEDESC *hw, D3DDEVICEDESC *hel, void *context);
 * the HAL (the hardware part of the description is filled) and the RGB device (the emulation) */
uint32_t CCALL IDirect3D2_EnumDevices_c(void *lpThis, uint32_t callback, uint32_t context)
{
    static const struct { const uint8_t *guid; const char *description; const char *name; int hw; } devices[2] = {
        { IID_HALDevice, "Direct3D HAL", "Direct3D Hardware", 1 },
        { IID_RGBDevice, "Direct3D RGB", "Direct3D RGB Emulation", 0 }
    };
    uint8_t *mem;
    unsigned i;

    if (callback == 0) return D3DERR_INVALIDARG;
    d3d_log("D3D: EnumDevices\n");

    mem = (uint8_t *)x86_malloc(16 + 64 + 64 + 2 * D3DDEVICEDESC_V1_SIZE);
    if (mem == NULL) return D3DERR_GENERIC;
    for (i = 0; i < 2; i++)
    {
        uint32_t args[6];
        uint8_t *guid = mem, *desc = mem + 16, *name = mem + 80, *hw = mem + 144, *hel = hw + D3DDEVICEDESC_V1_SIZE;

        memcpy(guid, devices[i].guid, 16);
        strcpy((char *)desc, devices[i].description);
        strcpy((char *)name, devices[i].name);
        fill_device_desc((uint32_t *)hw, devices[i].hw);
        fill_device_desc((uint32_t *)hel, !devices[i].hw);
        args[0] = GUEST_ADDR(guid);
        args[1] = GUEST_ADDR(desc);
        args[2] = GUEST_ADDR(name);
        args[3] = GUEST_ADDR(hw);
        args[4] = GUEST_ADDR(hel);
        args[5] = context;
        if (Winapi_CallGuest(callback, 6, args) == 0) break;    /* D3DENUMRET_CANCEL */
    }
    x86_free(mem);
    return D3D_OK;
}

uint32_t CCALL IDirect3D2_CreateLight_c(void *lpThis, void *a1, void *a2)
{
    d3d_log("D3D: CreateLight (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3D2_CreateMaterial_c(void *lpThis, void *a1, void *a2)
{
    d3d_material *m;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    m = (d3d_material *)new_object(sizeof(d3d_material), &IDirect3DMaterial2Vtbl_asm2c);
    if (m == NULL) return D3DERR_GENERIC;
    *(uint32_t *)a1 = GUEST_ADDR(m);
    return D3D_OK;
}

uint32_t CCALL IDirect3D2_CreateViewport_c(void *lpThis, void *a1, void *a2)
{
    d3d_viewport *v;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    v = (d3d_viewport *)new_object(sizeof(d3d_viewport), &IDirect3DViewport2Vtbl_asm2c);
    if (v == NULL) return D3DERR_GENERIC;
    *(uint32_t *)a1 = GUEST_ADDR(v);
    return D3D_OK;
}

/* D3DFINDDEVICERESULT: dwSize, guid, the descriptions of the hardware and the emulation */
uint32_t CCALL IDirect3D2_FindDevice_c(void *lpThis, void *a1, void *a2)
{
    uint8_t *r = (uint8_t *)a2;

    if (r == NULL) return D3DERR_INVALIDARG;
    d3d_log("D3D: FindDevice\n");
    memset(r + 4, 0, 16 + 2 * D3DDEVICEDESC_V1_SIZE);
    *(uint32_t *)r = 4 + 16 + 2 * D3DDEVICEDESC_V1_SIZE;
    memcpy(r + 4, IID_HALDevice, 16);
    fill_device_desc((uint32_t *)(r + 20), 1);
    fill_device_desc((uint32_t *)(r + 20 + D3DDEVICEDESC_V1_SIZE), 0);
    return D3D_OK;
}

/* the device: a 3D-capable surface of the game is its render target */
uint32_t CCALL IDirect3D2_CreateDevice_c(void *lpThis, void *a1, void *a2, void *a3)
{
    d3d_t *d3d = (d3d_t *)lpThis;
    struct IDirectDrawSurface_c *surface = (struct IDirectDrawSurface_c *)a2;
    d3d_device *dev;

    if ((a3 == NULL) || (surface == NULL)) return D3DERR_INVALIDARG;
    *(uint32_t *)a3 = 0;
    if (!same_guid(a1, IID_HALDevice) && !same_guid(a1, IID_RGBDevice))
    {
        d3d_log("D3D: CreateDevice: unknown device\n");
        return D3DERR_NOINTERFACE;
    }

    dev = (d3d_device *)new_object(sizeof(d3d_device), &IDirect3DDevice2Vtbl_asm2c);
    if (dev == NULL) return D3DERR_GENERIC;
    dev->d3d = d3d;
    d3d->RefCount++;
    dev->target = surface;
    surface->RefCount++;
    *(uint32_t *)a3 = GUEST_ADDR(dev);
    d3d_log("D3D: CreateDevice %ux%u, %u bpp, caps 0x%x, z %s\n", surface->width, surface->height, surface->bpp, surface->caps,
            (surface->zsurf != NULL) ? "yes" : "no");
    return D3D_OK;
}

/* ---- IDirect3DDevice2 ---- */

uint32_t CCALL IDirect3DDevice2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_device *dev = (d3d_device *)lpThis;
    return query_self(dev, &dev->RefCount, riid, IID_IDirect3DDevice2_, ppvObj);
}

uint32_t CCALL IDirect3DDevice2_AddRef_c(void *lpThis)
{
    return ++((d3d_device *)lpThis)->RefCount;
}

uint32_t CCALL IDirect3DDevice2_Release_c(void *lpThis)
{
    d3d_device *dev = (d3d_device *)lpThis;
    int i;

    if (--dev->RefCount != 0) return dev->RefCount;
    for (i = 0; i < MAX_VIEWPORTS; i++)
    {
        if (dev->viewports[i] != NULL)
        {
            dev->viewports[i]->device = NULL;
            IDirect3DViewport2_Release_c(dev->viewports[i]);
        }
    }
    if (dev->target != NULL) IDirectDrawSurface_Release_c(dev->target);
    IDirect3D2_Release_c(dev->d3d);
    x86_free(dev);
    return 0;
}

/* the hardware and the emulated description; the part that does not belong to the device stays empty */
uint32_t CCALL IDirect3DDevice2_GetCaps_c(void *lpThis, void *a1, void *a2)
{
    uint32_t *hw = (uint32_t *)a1, *hel = (uint32_t *)a2;

    if ((hw == NULL) || (hel == NULL) || (hw[0] < D3DDEVICEDESC_V1_SIZE) || (hel[0] < D3DDEVICEDESC_V1_SIZE)) return D3DERR_INVALIDARG;
    fill_device_desc(hw, 1);
    fill_device_desc(hel, 0);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SwapTextureHandles_c(void *lpThis, void *a1, void *a2)
{
    d3d_log("D3D: SwapTextureHandles (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_GetStats_c(void *lpThis, void *a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_AddViewport_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;
    d3d_viewport *vp = (d3d_viewport *)a1;
    int i;

    if (vp == NULL) return D3DERR_INVALIDARG;
    for (i = 0; i < MAX_VIEWPORTS; i++)
    {
        if (dev->viewports[i] == NULL)
        {
            dev->viewports[i] = vp;
            vp->device = dev;
            vp->RefCount++;
            return D3D_OK;
        }
    }
    return D3DERR_GENERIC;
}

uint32_t CCALL IDirect3DDevice2_DeleteViewport_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;
    d3d_viewport *vp = (d3d_viewport *)a1;
    int i;

    for (i = 0; i < MAX_VIEWPORTS; i++)
    {
        if ((vp != NULL) && (dev->viewports[i] == vp))
        {
            dev->viewports[i] = NULL;
            if (dev->current == vp) dev->current = NULL;
            vp->device = NULL;
            IDirect3DViewport2_Release_c(vp);
            return D3D_OK;
        }
    }
    return D3DERR_INVALIDARG;
}

uint32_t CCALL IDirect3DDevice2_NextViewport_c(void *lpThis, void *a1, void *a2, uint32_t a3)
{
    return D3DERR_NOTIMPL;
}

/* The pixel formats of the textures, to the callback of the game:
 *   HRESULT cb(DDSURFACEDESC *desc, void *context)
 * 8 bit with a palette, then 16 bit with 5-6-5, 1-5-5-5 and 4-4-4-4 */
uint32_t CCALL IDirect3DDevice2_EnumTextureFormats_c(void *lpThis, uint32_t callback, uint32_t context)
{
    static const struct { uint32_t flags, bits, r, g, b, a; } formats[4] = {
        { 0x40 | 0x20, 8, 0, 0, 0, 0 },
        { 0x40, 16, 0xf800, 0x07e0, 0x001f, 0 },
        { 0x40 | 0x01, 16, 0x7c00, 0x03e0, 0x001f, 0x8000 },
        { 0x40 | 0x01, 16, 0x0f00, 0x00f0, 0x000f, 0xf000 }
    };
    uint32_t *desc;
    unsigned i;

    if (callback == 0) return D3DERR_INVALIDARG;
    desc = (uint32_t *)x86_malloc(108);
    if (desc == NULL) return D3DERR_GENERIC;
    for (i = 0; i < 4; i++)
    {
        uint32_t args[2];

        memset(desc, 0, 108);
        desc[0] = 108;                  /* dwSize */
        desc[1] = 0x1000;               /* dwFlags: DDSD_PIXELFORMAT */
        desc[18] = 32;                  /* ddpfPixelFormat.dwSize */
        desc[19] = formats[i].flags;
        desc[21] = formats[i].bits;
        desc[22] = formats[i].r;
        desc[23] = formats[i].g;
        desc[24] = formats[i].b;
        desc[25] = formats[i].a;
        args[0] = GUEST_ADDR(desc);
        args[1] = context;
        if (Winapi_CallGuest(callback, 2, args) == 0) break;    /* D3DENUMRET_CANCEL */
    }
    x86_free(desc);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_BeginScene_c(void *lpThis)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (dev->in_scene) return D3DERR_GENERIC;
    dev->in_scene = 1;
    dev->draws = 0;
    dev->vertices = 0;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_EndScene_c(void *lpThis)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (!dev->in_scene) return D3DERR_GENERIC;
    dev->in_scene = 0;
    d3d_log("D3D: scene: %u draws, %u vertices\n", dev->draws, dev->vertices);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetDirect3D_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    dev->d3d->RefCount++;
    *(uint32_t *)a1 = GUEST_ADDR(dev->d3d);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetCurrentViewport_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;

    dev->current = (d3d_viewport *)a1;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetCurrentViewport_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    if (dev->current == NULL) { *(uint32_t *)a1 = 0; return D3DERR_NOTFOUND; }
    dev->current->RefCount++;
    *(uint32_t *)a1 = GUEST_ADDR(dev->current);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetRenderTarget_c(void *lpThis, void *a1, uint32_t a2)
{
    d3d_device *dev = (d3d_device *)lpThis;
    struct IDirectDrawSurface_c *surface = (struct IDirectDrawSurface_c *)a1;

    if (surface == NULL) return D3DERR_INVALIDARG;
    d3d_log("D3D: SetRenderTarget %ux%u, %u bpp, caps 0x%x\n", surface->width, surface->height, surface->bpp, surface->caps);
    surface->RefCount++;
    if (dev->target != NULL) IDirectDrawSurface_Release_c(dev->target);
    dev->target = surface;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetRenderTarget_c(void *lpThis, void *a1)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    dev->target->RefCount++;
    *(uint32_t *)a1 = GUEST_ADDR(dev->target);
    return D3D_OK;
}

/* the primitives of the execute-less interface of DirectX 3 */
uint32_t CCALL IDirect3DDevice2_Begin_c(void *lpThis, uint32_t a1, uint32_t a2, uint32_t a3)
{
    d3d_log("D3D: Begin (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_BeginIndexed_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5)
{
    d3d_log("D3D: BeginIndexed (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_Vertex_c(void *lpThis, void *a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_Index_c(void *lpThis, uint32_t a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_End_c(void *lpThis, uint32_t a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_GetRenderState_c(void *lpThis, uint32_t a1, void *a2)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if ((a2 == NULL) || (a1 >= MAX_STATES)) return D3DERR_INVALIDARG;
    *(uint32_t *)a2 = dev->state[a1];
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetRenderState_c(void *lpThis, uint32_t a1, uint32_t a2)
{
    d3d_device *dev = (d3d_device *)lpThis;

    d3d_log("D3D: SetRenderState %u = 0x%x\n", a1, a2);
    if (a1 >= MAX_STATES) return D3DERR_INVALIDARG;
    dev->state[a1] = a2;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetLightState_c(void *lpThis, uint32_t a1, void *a2)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_SetLightState_c(void *lpThis, uint32_t a1, uint32_t a2)
{
    d3d_log("D3D: SetLightState %u = 0x%x\n", a1, a2);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetTransform_c(void *lpThis, uint32_t a1, void *a2)
{
    d3d_log("D3D: SetTransform %u (ignored: the game sends transformed vertices)\n", a1);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetTransform_c(void *lpThis, uint32_t a1, void *a2)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_MultiplyTransform_c(void *lpThis, uint32_t a1, void *a2)
{
    return D3D_OK;
}

/* type, vertex type (D3DVT_TLVERTEX), vertices, count, flags: the drawing comes later */
uint32_t CCALL IDirect3DDevice2_DrawPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (!dev->in_scene) return D3DERR_GENERIC;
    dev->draws++;
    dev->vertices += a4;
    return D3D_OK;
}

/* type, vertex type, vertices, vertex count, indices (WORD), index count, flags */
uint32_t CCALL IDirect3DDevice2_DrawIndexedPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, void *a5, uint32_t a6, uint32_t a7)
{
    d3d_device *dev = (d3d_device *)lpThis;

    if (!dev->in_scene) return D3DERR_GENERIC;
    dev->draws++;
    dev->vertices += a6;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetClipStatus_c(void *lpThis, void *a1)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetClipStatus_c(void *lpThis, void *a1)
{
    return D3DERR_NOTIMPL;
}

/* ---- IDirect3DViewport2 ---- */

uint32_t CCALL IDirect3DViewport2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;
    return query_self(vp, &vp->RefCount, riid, IID_IDirect3DViewport2_, ppvObj);
}

uint32_t CCALL IDirect3DViewport2_AddRef_c(void *lpThis)
{
    return ++((d3d_viewport *)lpThis)->RefCount;
}

uint32_t CCALL IDirect3DViewport2_Release_c(void *lpThis)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;

    if (--vp->RefCount != 0) return vp->RefCount;
    x86_free(vp);
    return 0;
}

uint32_t CCALL IDirect3DViewport2_Initialize_c(void *lpThis, void *a1)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_GetViewport_c(void *lpThis, void *a1)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;
    uint32_t *out = (uint32_t *)a1;

    if (out == NULL) return D3DERR_INVALIDARG;
    memcpy(out + 1, vp->vp, sizeof(vp->vp) - 4);
    return D3D_OK;
}

/* D3DVIEWPORT: dwSize, dwX, dwY, dwWidth, dwHeight, dvScaleX, dvScaleY, dvMaxX, dvMaxY, dvMinZ, dvMaxZ */
uint32_t CCALL IDirect3DViewport2_SetViewport_c(void *lpThis, void *a1)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;
    const uint32_t *in = (const uint32_t *)a1;

    if (in == NULL) return D3DERR_INVALIDARG;
    memcpy(vp->vp, in, sizeof(vp->vp));
    vp->has_vp = 1;
    d3d_log("D3D: SetViewport %u,%u %ux%u\n", in[1], in[2], in[3], in[4]);
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_TransformVertices_c(void *lpThis, uint32_t a1, void *a2, uint32_t a3, void *a4)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_LightElements_c(void *lpThis, uint32_t a1, void *a2)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_SetBackground_c(void *lpThis, uint32_t a1)
{
    ((d3d_viewport *)lpThis)->background = a1;
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_GetBackground_c(void *lpThis, void *a1, void *a2)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;

    if ((a1 == NULL) || (a2 == NULL)) return D3DERR_INVALIDARG;
    *(uint32_t *)a1 = vp->background;
    *(uint32_t *)a2 = (vp->background != 0);
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_SetBackgroundDepth_c(void *lpThis, void *a1)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_GetBackgroundDepth_c(void *lpThis, void *a1, void *a2)
{
    return D3DERR_NOTIMPL;
}

/* fills a rectangle of a surface with one value */
static void fill_rect(struct IDirectDrawSurface_c *s, int x1, int y1, int x2, int y2, uint32_t value)
{
    int x, y;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)s->width) x2 = s->width;
    if (y2 > (int)s->height) y2 = s->height;
    for (y = y1; y < y2; y++)
    {
        uint8_t *row = s->pixels + (size_t)y * s->pitch;
        for (x = x1; x < x2; x++)
        {
            switch (s->bpp)
            {
                case 8:  row[x] = (uint8_t)value; break;
                case 16: ((uint16_t *)row)[x] = (uint16_t)value; break;
                default: ((uint32_t *)row)[x] = value; break;
            }
        }
    }
}

/* the colour of the material of the background, in the format of the target */
static uint32_t background_value(const d3d_viewport *vp, const struct IDirectDrawSurface_c *target)
{
    const d3d_material *m = NULL;
    const float *diffuse;
    uint32_t r, g, b;

    if ((vp->background >= 1) && (vp->background <= MAX_MATERIALS)) m = material_handles[vp->background - 1];
    if (m == NULL) return 0;
    diffuse = (const float *)&m->mat[1];        /* dcvDiffuse: r, g, b, a */
    r = (uint32_t)(diffuse[0] * 255.0f + 0.5f) & 255;
    g = (uint32_t)(diffuse[1] * 255.0f + 0.5f) & 255;
    b = (uint32_t)(diffuse[2] * 255.0f + 0.5f) & 255;
    if (target->bpp == 16) return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    if (target->bpp == 32) return (r << 16) | (g << 8) | b;
    return 0;
}

/* D3DCLEAR_TARGET: the background colour of the viewport; D3DCLEAR_ZBUFFER: the far plane */
uint32_t CCALL IDirect3DViewport2_Clear_c(void *lpThis, uint32_t a1, void *a2, uint32_t a3)
{
    d3d_viewport *vp = (d3d_viewport *)lpThis;
    const int32_t *rects = (const int32_t *)a2;     /* D3DRECT: x1, y1, x2, y2 */
    struct IDirectDrawSurface_c *target;
    uint32_t i;

    if (vp->device == NULL) return D3DERR_INVALIDARG;
    target = vp->device->target;
    d3d_log("D3D: Clear %u rects, flags 0x%x\n", a1, a3);

    for (i = 0; (i < a1) || (i == 0); i++)
    {
        int x1 = 0, y1 = 0, x2 = (int)target->width, y2 = (int)target->height;

        if ((a1 != 0) && (rects != NULL))
        {
            x1 = rects[i * 4]; y1 = rects[i * 4 + 1]; x2 = rects[i * 4 + 2]; y2 = rects[i * 4 + 3];
        }
        else if (vp->has_vp)
        {
            x1 = (int)vp->vp[1]; y1 = (int)vp->vp[2]; x2 = x1 + (int)vp->vp[3]; y2 = y1 + (int)vp->vp[4];
        }
        if (a3 & D3DCLEAR_TARGET) fill_rect(target, x1, y1, x2, y2, background_value(vp, target));
        if ((a3 & D3DCLEAR_ZBUFFER) && (target->zsurf != NULL)) fill_rect(target->zsurf, x1, y1, x2, y2, 0xffffffffu);
    }
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_AddLight_c(void *lpThis, void *a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_DeleteLight_c(void *lpThis, void *a1)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_NextLight_c(void *lpThis, void *a1, void *a2, uint32_t a3)
{
    return D3DERR_NOTIMPL;
}

/* D3DVIEWPORT2: the same eleven values, with the clip volume in place of the scale */
uint32_t CCALL IDirect3DViewport2_GetViewport2_c(void *lpThis, void *a1)
{
    return IDirect3DViewport2_GetViewport_c(lpThis, a1);
}

uint32_t CCALL IDirect3DViewport2_SetViewport2_c(void *lpThis, void *a1)
{
    return IDirect3DViewport2_SetViewport_c(lpThis, a1);
}

/* ---- IDirect3DMaterial2 ---- */

uint32_t CCALL IDirect3DMaterial2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_material *m = (d3d_material *)lpThis;
    return query_self(m, &m->RefCount, riid, IID_IDirect3DMaterial2_, ppvObj);
}

uint32_t CCALL IDirect3DMaterial2_AddRef_c(void *lpThis)
{
    return ++((d3d_material *)lpThis)->RefCount;
}

uint32_t CCALL IDirect3DMaterial2_Release_c(void *lpThis)
{
    d3d_material *m = (d3d_material *)lpThis;

    if (--m->RefCount != 0) return m->RefCount;
    if (m->handle != 0) material_handles[m->handle - 1] = NULL;
    x86_free(m);
    return 0;
}

uint32_t CCALL IDirect3DMaterial2_SetMaterial_c(void *lpThis, void *a1)
{
    d3d_material *m = (d3d_material *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    memcpy(m->mat, a1, sizeof(m->mat));
    return D3D_OK;
}

uint32_t CCALL IDirect3DMaterial2_GetMaterial_c(void *lpThis, void *a1)
{
    d3d_material *m = (d3d_material *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    memcpy(a1, m->mat, sizeof(m->mat));
    return D3D_OK;
}

uint32_t CCALL IDirect3DMaterial2_GetHandle_c(void *lpThis, void *a1, void *a2)
{
    d3d_material *m = (d3d_material *)lpThis;
    int i;

    if (a2 == NULL) return D3DERR_INVALIDARG;
    if (m->handle == 0)
    {
        for (i = 0; i < MAX_MATERIALS; i++)
        {
            if (material_handles[i] == NULL)
            {
                material_handles[i] = m;
                m->handle = (uint32_t)i + 1;
                break;
            }
        }
        if (m->handle == 0) return D3DERR_GENERIC;
    }
    *(uint32_t *)a2 = m->handle;
    return D3D_OK;
}

/* ---- IDirect3DTexture2: a view of a surface made with DDSCAPS_TEXTURE ---- */

int Winapi_D3dSurfaceQuery(struct IDirectDrawSurface_c *surface, const void *riid, uint32_t *out)
{
    d3d_texture *t;

    if (!Winapi_Direct3DEnabled() || !same_guid(riid, IID_IDirect3DTexture2_)) return 0;
    t = (d3d_texture *)surface->d3dtex;
    if (t == NULL)
    {
        t = (d3d_texture *)x86_malloc(sizeof(d3d_texture));
        if (t == NULL) { *out = 0; return 0; }
        memset(t, 0, sizeof(d3d_texture));
        t->lpVtbl = GUEST_ADDR(&IDirect3DTexture2Vtbl_asm2c);
        t->surface = surface;
        surface->d3dtex = t;
    }
    surface->RefCount++;        /* the texture and the surface are one object: one count */
    *out = GUEST_ADDR(t);
    return 1;
}

void Winapi_D3dSurfaceFree(struct IDirectDrawSurface_c *surface)
{
    d3d_texture *t = (d3d_texture *)surface->d3dtex;

    if (t == NULL) return;
    if (t->handle != 0) texture_handles[t->handle - 1] = NULL;
    surface->d3dtex = NULL;
    x86_free(t);
}

uint32_t CCALL IDirect3DTexture2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    return IDirectDrawSurface_QueryInterface_c(((d3d_texture *)lpThis)->surface, riid, (PTR32(void) *)ppvObj);
}

uint32_t CCALL IDirect3DTexture2_AddRef_c(void *lpThis)
{
    return IDirectDrawSurface_AddRef_c(((d3d_texture *)lpThis)->surface);
}

uint32_t CCALL IDirect3DTexture2_Release_c(void *lpThis)
{
    return IDirectDrawSurface_Release_c(((d3d_texture *)lpThis)->surface);
}

uint32_t CCALL IDirect3DTexture2_GetHandle_c(void *lpThis, void *a1, void *a2)
{
    d3d_texture *t = (d3d_texture *)lpThis;
    int i;

    if (a2 == NULL) return D3DERR_INVALIDARG;
    if (t->handle == 0)
    {
        for (i = 0; i < MAX_TEXTURES; i++)
        {
            if (texture_handles[i] == NULL)
            {
                texture_handles[i] = t;
                t->handle = (uint32_t)i + 1;
                break;
            }
        }
        if (t->handle == 0) return D3DERR_GENERIC;
    }
    *(uint32_t *)a2 = t->handle;
    return D3D_OK;
}

/* the palette of the surface is read when the texture is used */
uint32_t CCALL IDirect3DTexture2_PaletteChanged_c(void *lpThis, uint32_t a1, uint32_t a2)
{
    ((d3d_texture *)lpThis)->surface->texture_gen++;
    return D3D_OK;
}

/* the pixels (and the palette) of another texture of the same size and format */
uint32_t CCALL IDirect3DTexture2_Load_c(void *lpThis, void *a1)
{
    struct IDirectDrawSurface_c *dst = ((d3d_texture *)lpThis)->surface;
    d3d_texture *from = (d3d_texture *)a1;
    struct IDirectDrawSurface_c *src;
    uint32_t y, row;

    if (from == NULL) return D3DERR_INVALIDARG;
    src = from->surface;
    if ((src->width != dst->width) || (src->height != dst->height) || (src->bpp != dst->bpp)) return D3DERR_INVALIDARG;
    row = dst->width * (dst->bpp >> 3);
    for (y = 0; y < dst->height; y++)
    {
        memcpy(dst->pixels + (size_t)y * dst->pitch, src->pixels + (size_t)y * src->pitch, row);
    }
    if (src->palette != NULL && dst->palette != NULL) memcpy(dst->palette->argb, src->palette->argb, sizeof(dst->palette->argb));
    dst->texture_gen++;
    return D3D_OK;
}
