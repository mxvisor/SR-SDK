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
 * "HAL": the rasteriser of the port) and the render state and the software rasteriser. */

#include "WinApi-d3d.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "Game-Memory.h"
#include "WinApi-ddraw.h"
#include "WinApi-ddraw-priv.h"
#include "WinApi-game.h"
#include "WinApi-config.h"
#ifdef GAME_D3D_GL
#include "WinApi-d3dgl.h"
#endif
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

/* The renderer: the game's setting (3D= of the config file, Winapi_Direct3DSetMode)
 * or the default (GAME_DIRECT3D, OpenGL in a d3d_gl build); <GAME>_D3D=0/1 and
 * <GAME>_D3D_GL=0/1 override both. */
static int d3d_cfg_enabled = -1;    /* -1: not set */
static int d3d_cfg_gl = -1;
static int d3d_gl_failed;

/* 3D=direct3d | software | off: Direct3D on OpenGL, Direct3D with the software
 * rasteriser, the game's own software rendering (DirectDraw).  Returns 0 for
 * an unknown value. */
int Winapi_Direct3DSetMode(const char *mode)
{
    if (strcasecmp(mode, "direct3d") == 0) { d3d_cfg_enabled = 1; d3d_cfg_gl = 1; }
    else if (strcasecmp(mode, "software") == 0) { d3d_cfg_enabled = 1; d3d_cfg_gl = 0; }
    else if (strcasecmp(mode, "off") == 0) { d3d_cfg_enabled = 0; }
    else return 0;
    return 1;
}

/* the OpenGL window could not be made: Direct3D goes on with the software rasteriser */
void Winapi_Direct3DNoGL(void)
{
    d3d_gl_failed = 1;
    fprintf(stderr, "Direct3D: no OpenGL, the software rasteriser is used\n");
}

int Winapi_Direct3DEnabled(void)
{
    const char *e = getenv(GAME_ENV("D3D"));
    if (e != NULL) return atoi(e) != 0;
    if (d3d_cfg_enabled >= 0) return d3d_cfg_enabled;
    return GAME_DIRECT3D != 0;
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
    int gl_scene;                       /* the scene is in the GL backend's FBO */
    uint32_t draws;                     /* DrawPrimitive calls in the scene */
    uint32_t vertices;
    void *v1;                           /* IDirect3DDevice (DirectX 3) view of this device */
    void *v3;                           /* IDirect3DDevice3 (DirectX 6) view of this device */
    uint32_t matrices[16][16];
    uint32_t matrix_used[16];
} d3d_device;

/* IID_IDirect3DDevice: the same device, the DirectX 3 method order */
typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_device *dev;
} d3d_device1;

/* IDirect3DDevice3 (DirectX 6): the same device, the DirectX 6 method order */
typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_device *dev;
} d3d_device3;

/* IDirect3DExecuteBuffer (DirectX 3): the buffer the game fills with instructions
 * and vertices, and a copy of its D3DEXECUTEDATA (48 bytes) */
typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_device1 *device;
    uint32_t data;                      /* the buffer, a guest address */
    uint32_t size;
    uint32_t owner;                     /* the buffer was allocated here */
    uint32_t exec_data[12];
} d3d_execbuf;

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
    void *v1;                           /* its IDirect3DTexture (DirectX 3) view */
} d3d_texture;

/* IDirect3DTexture (DirectX 3): a view of the DirectX 5 texture object */
typedef struct {
    uint32_t lpVtbl;
    uint32_t RefCount;
    d3d_texture *tex;
} d3d_texture1;

extern uint32_t IDirect3D2Vtbl_asm2c;
extern uint32_t IDirect3DDevice2Vtbl_asm2c;
extern uint32_t IDirect3DDeviceVtbl_asm2c;
extern uint32_t IDirect3DDevice3Vtbl_asm2c;
extern uint32_t IDirect3DExecuteBufferVtbl_asm2c;
extern uint32_t IDirect3DViewport2Vtbl_asm2c;
extern uint32_t IDirect3DMaterial2Vtbl_asm2c;
extern uint32_t IDirect3DTexture2Vtbl_asm2c;
extern uint32_t IDirect3DTextureVtbl_asm2c;

/* handle - 1 indexes these */
static d3d_texture *texture_handles[MAX_TEXTURES];
static d3d_material *material_handles[MAX_MATERIALS];

/* ---- GUIDs ---- */
static const uint8_t IID_IUnknown_[16]           = { 0,0,0,0, 0,0, 0,0, 0xc0,0,0,0,0,0,0,0x46 };
static const uint8_t IID_IDirect3D2_[16]         = { 0xc1,0x1e,0xae,0x6a, 0x2a,0x66, 0xd0,0x11, 0x88,0x9d,0x00,0xaa,0x00,0xbb,0xb7,0x6a };
static const uint8_t IID_IDirect3DDevice2_[16]   = { 0x01,0x15,0x28,0x93, 0xf8,0x8c, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DDevice_[16]    = { 0x00,0x88,0x10,0x64, 0x7d,0x95, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DDevice3_[16]   = { 0x60,0x3b,0xab,0xb0, 0xd7,0x33, 0xd1,0x11, 0xa9,0x81,0x00,0xc0,0x4f,0xd7,0xb1,0x74 };
static const uint8_t IID_IDirect3DTexture2_[16]  = { 0x02,0x15,0x28,0x93, 0xf8,0x8c, 0xd0,0x11, 0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29 };
static const uint8_t IID_IDirect3DTexture_[16]   = { 0xe0,0xd9,0xdc,0x2c, 0xa0,0x25, 0xcf,0x11, 0xa3,0x1a,0x00,0xaa,0x00,0xb9,0x33,0x56 };
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
    c[8]  = 0x1 | 0x2 | 0x4 | 0x8 | 0x4000;   /* dwTextureCaps: PERSPECTIVE | POW2 | ALPHA | TRANSPARENCY | MIPMAP */
    /* dwTextureFilterCaps: NEAREST/LINEAR and the mip filters.  DirectX 3/5 and
     * DirectX 6 number the same capability differently, and a game reads the
     * fields with the headers it was built with, so both encodings are set:
     * DX3/5 NEAREST/LINEAR/MIPNEAREST/MIPLINEAR/LINEARMIPNEAREST/LINEARMIPLINEAR
     * and DX6 MINF/MIPF/MAGF POINT and LINEAR.  The textures of the game have a
     * mip chain (it fills the levels itself, WinApi-ddraw.c hands it out) and
     * both rasterisers sample it. */
    c[9]  = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 |
            0x100 | 0x200 | 0x10000 | 0x20000 | 0x01000000 | 0x02000000;
    c[10] = 0x1 | 0x2 | 0x4 | 0x8 | 0x40;    /* dwTextureBlendCaps: DECAL | MODULATE | DECALALPHA | MODULATEALPHA | COPY */
    c[11] = 0x1 | 0x2 | 0x4;    /* dwTextureAddressCaps: WRAP | MIRROR | CLAMP */
    c[12] = 32;             /* dwStippleWidth */
    c[13] = 32;             /* dwStippleHeight */
}

/* hw != 0: the description of the hardware part; else of the software emulation, left empty */
static void fill_device_desc_size(uint32_t *d, int hw, uint32_t size)
{
    memset(d, 0, size);
    d[0] = size;
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
    if (size < 0xcc) return;    /* the DirectX 3 description ends at dwMaxVertexCount */
    d[43] = 1;              /* dwMinTextureWidth */
    d[44] = 1;              /* dwMinTextureHeight */
    d[45] = 256;            /* dwMaxTextureWidth */
    d[46] = 256;            /* dwMaxTextureHeight */
    d[47] = 1;              /* dwMinStippleWidth */
    d[48] = 32;             /* dwMaxStippleWidth */
    d[49] = 1;              /* dwMinStippleHeight */
    d[50] = 32;             /* dwMaxStippleHeight */
}

static void fill_device_desc(uint32_t *d, int hw)
{
    fill_device_desc_size(d, hw, D3DDEVICEDESC_V1_SIZE);
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
uint32_t CCALL IDirect3D2_EnumDevices_c(void */*lpThis*/, uint32_t callback, uint32_t context)
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

uint32_t CCALL IDirect3D2_CreateLight_c(void */*lpThis*/, void */*a1*/, void */*a2*/)
{
    d3d_log("D3D: CreateLight (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3D2_CreateMaterial_c(void */*lpThis*/, void *a1, void */*a2*/)
{
    d3d_material *m;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    m = (d3d_material *)new_object(sizeof(d3d_material), &IDirect3DMaterial2Vtbl_asm2c);
    if (m == NULL) return D3DERR_GENERIC;
    *(uint32_t *)a1 = GUEST_ADDR(m);
    d3d_log("D3D: CreateMaterial -> %p\n", (void *)m);
    return D3D_OK;
}

uint32_t CCALL IDirect3D2_CreateViewport_c(void */*lpThis*/, void *a1, void */*a2*/)
{
    d3d_viewport *v;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    v = (d3d_viewport *)new_object(sizeof(d3d_viewport), &IDirect3DViewport2Vtbl_asm2c);
    if (v == NULL) return D3DERR_GENERIC;
    *(uint32_t *)a1 = GUEST_ADDR(v);
    return D3D_OK;
}

/* D3DFINDDEVICERESULT: dwSize, guid, the descriptions of the hardware and the emulation */
uint32_t CCALL IDirect3D2_FindDevice_c(void */*lpThis*/, void */*a1*/, void *a2)
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
    uint32_t *out = (uint32_t *)ppvObj;

    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IDirect3DDevice_))
    {
        d3d_device1 *v1 = (d3d_device1 *)dev->v1;

        if (v1 == NULL)
        {
            v1 = (d3d_device1 *)new_object(sizeof(d3d_device1), &IDirect3DDeviceVtbl_asm2c);
            if (v1 == NULL) { *out = 0; return D3DERR_GENERIC; }
            v1->dev = dev;
            dev->v1 = v1;
        }
        v1->RefCount++;
        dev->RefCount++;
        *out = GUEST_ADDR(v1);
        d3d_log("D3D: IDirect3DDevice\n");
        return D3D_OK;
    }
    if (same_guid(riid, IID_IDirect3DDevice3_))
    {
        d3d_device3 *v3 = (d3d_device3 *)dev->v3;

        if (v3 == NULL)
        {
            v3 = (d3d_device3 *)new_object(sizeof(d3d_device3), &IDirect3DDevice3Vtbl_asm2c);
            if (v3 == NULL) { *out = 0; return D3DERR_GENERIC; }
            v3->dev = dev;
            dev->v3 = v3;
        }
        v3->RefCount++;
        dev->RefCount++;
        *out = GUEST_ADDR(v3);
        d3d_log("D3D: IDirect3DDevice3\n");
        return D3D_OK;
    }
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
uint32_t CCALL IDirect3DDevice2_GetCaps_c(void */*lpThis*/, void *a1, void *a2)
{
    uint32_t *hw = (uint32_t *)a1, *hel = (uint32_t *)a2;

    if ((hw == NULL) || (hel == NULL) || (hw[0] < D3DDEVICEDESC_V1_SIZE) || (hel[0] < D3DDEVICEDESC_V1_SIZE)) return D3DERR_INVALIDARG;
    fill_device_desc(hw, 1);
    fill_device_desc(hel, 0);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SwapTextureHandles_c(void */*lpThis*/, void */*a1*/, void */*a2*/)
{
    d3d_log("D3D: SwapTextureHandles (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_GetStats_c(void */*lpThis*/, void */*a1*/)
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

uint32_t CCALL IDirect3DDevice2_NextViewport_c(void */*lpThis*/, void */*a1*/, void */*a2*/, uint32_t /*a3*/)
{
    return D3DERR_NOTIMPL;
}

/* The pixel formats of the textures, to the callback of the game:
 *   HRESULT cb(DDSURFACEDESC *desc, void *context)
 * 8 bit with a palette, then 16 bit with 5-6-5, 1-5-5-5 and 4-4-4-4 */
uint32_t CCALL IDirect3DDevice2_EnumTextureFormats_c(void */*lpThis*/, uint32_t callback, uint32_t context)
{
    static const struct { uint32_t flags, bits, r, g, b, a; } formats[4] = {
        { 0x20, 8, 0, 0, 0, 0 },
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
#ifdef GAME_D3D_GL
    /* the GL backend takes the scene when it can (else the software one) */
    dev->gl_scene = Winapi_Direct3DGL() && Winapi_D3dGLBeginScene(dev->target);
    if (dev->gl_scene)
    {
        uint32_t s;
        for (s = 0; s < MAX_STATES; s++) Winapi_D3dGLSetState(s, dev->state[s]);
    }
#endif
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_EndScene_c(void *lpThis)
{
    d3d_device *dev = (d3d_device *)lpThis;


    if (!dev->in_scene) return D3DERR_GENERIC;
    dev->in_scene = 0;
#ifdef GAME_D3D_GL
    if (dev->gl_scene)
    {
        Winapi_D3dGLEndScene(dev->target);
        dev->gl_scene = 0;
    }
#endif
    d3d_log("D3D: scene: %u draws, %u vertices\n", (unsigned)dev->draws, (unsigned)dev->vertices);
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

uint32_t CCALL IDirect3DDevice2_SetRenderTarget_c(void *lpThis, void *a1, uint32_t /*a2*/)
{
    d3d_device *dev = (d3d_device *)lpThis;
    struct IDirectDrawSurface_c *surface = (struct IDirectDrawSurface_c *)a1;

    if (surface == NULL) return D3DERR_INVALIDARG;
    d3d_log("D3D: SetRenderTarget %ux%u, %u bpp, caps 0x%x\n", surface->width, surface->height, surface->bpp, surface->caps);
#ifdef GAME_D3D_GL
    /* a scene moves to the new target: read the old FBO back first */
    if (dev->gl_scene)
    {
        Winapi_D3dGLEndScene(dev->target);
        dev->gl_scene = 0;
    }
#endif
    surface->RefCount++;
    if (dev->target != NULL) IDirectDrawSurface_Release_c(dev->target);
    dev->target = surface;
#ifdef GAME_D3D_GL
    if (dev->in_scene) dev->gl_scene = Winapi_Direct3DGL() && Winapi_D3dGLBeginScene(dev->target);
#endif
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
uint32_t CCALL IDirect3DDevice2_Begin_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
    d3d_log("D3D: Begin (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_BeginIndexed_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/, void */*a3*/, uint32_t /*a4*/, uint32_t /*a5*/)
{
    d3d_log("D3D: BeginIndexed (not supported)\n");
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_Vertex_c(void */*lpThis*/, void */*a1*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_Index_c(void */*lpThis*/, uint32_t /*a1*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_End_c(void */*lpThis*/, uint32_t /*a1*/)
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
#ifdef GAME_D3D_GL
    if (dev->gl_scene) Winapi_D3dGLSetState(a1, a2);
#endif
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetLightState_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_SetLightState_c(void */*lpThis*/, uint32_t a1, uint32_t a2)
{
    d3d_log("D3D: SetLightState %u = 0x%x\n", a1, a2);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_SetTransform_c(void */*lpThis*/, uint32_t a1, void */*a2*/)
{
    d3d_log("D3D: SetTransform %u (ignored: the game sends transformed vertices)\n", a1);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetTransform_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice2_MultiplyTransform_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/)
{
    return D3D_OK;
}

/* ---- the rasteriser: pre-transformed triangles into the target surface, in software ----
 * The surfaces are memory of the host (WinApi-ddraw.c presents them), so the primitives are
 * drawn into the target surface and its Z-buffer directly: 2D blits, the 3D scene and the
 * overlay stay in one picture.  Done: Z test and write, flat and Gouraud colour, textures
 * (8 bit palette, 5-6-5, 1-5-5-5, 4-4-4-4) with perspective correction, wrap/mirror/clamp,
 * the colour key, the texture blend modes, alpha blending, culling, lines. */

#define RS_TEXTUREHANDLE    1
#define RS_TEXTUREADDRESS   3
#define RS_TEXTUREPERSPECTIVE 4
#define RS_ZENABLE          7
#define RS_FILLMODE         8
#define RS_SHADEMODE        9
#define RS_ZWRITEENABLE     14
#define RS_TEXTUREMIN       18
#define RS_TEXTUREMAG       17
#define RS_SRCBLEND         19
#define RS_DESTBLEND        20
#define RS_TEXTUREMAPBLEND  21
#define RS_CULLMODE         22
#define RS_ZFUNC            23
#define RS_ALPHABLENDENABLE 27
#define RS_SPECULARENABLE   29
#define RS_COLORKEYENABLE   41

typedef struct {
    float x, y, z, rhw, u, v;
    int r, g, b, a, sr, sg, sb;
} rvert;

#define RT_MAX_MIPS 16

typedef struct {
    const struct IDirectDrawSurface_c *s;
    int w, h;
    int shift[4], max[4];       /* r, g, b, a of a 16 bit format */
    int levels;                 /* the mip chain: 1 when the texture has none */
    const struct IDirectDrawSurface_c *lv[RT_MAX_MIPS];
} rtex;

static void rt_channel(uint32_t mask, int *shift, int *max)
{
    int sh = 0;
    if (mask == 0) { *shift = 0; *max = 0; return; }
    while (!(mask & 1)) { mask >>= 1; sh++; }
    *shift = sh;
    *max = (int)mask;
}

static void rt_setup(rtex *t, const struct IDirectDrawSurface_c *s)
{
    uint32_t r = s->rmask, g = s->gmask, b = s->bmask, a = s->amask;
    const struct IDirectDrawSurface_c *lv;

    t->s = s; t->w = (int)s->width; t->h = (int)s->height;
    if ((r | g | b) == 0) { r = 0xf800; g = 0x07e0; b = 0x001f; }
    rt_channel(r, &t->shift[0], &t->max[0]);
    rt_channel(g, &t->shift[1], &t->max[1]);
    rt_channel(b, &t->shift[2], &t->max[2]);
    rt_channel(a, &t->shift[3], &t->max[3]);

    t->levels = 0;
    for (lv = s; (lv != NULL) && (t->levels < RT_MAX_MIPS); lv = lv->lpmip)
    {
        t->lv[t->levels++] = lv;
    }
    if (t->levels == 0) { t->lv[0] = s; t->levels = 1; }
}

static int rt_addr(int c, int n, uint32_t mode)
{
    if (mode == 3) return (c < 0) ? 0 : (c >= n ? n - 1 : c);       /* CLAMP */
    if (mode == 2)                                                  /* MIRROR */
    {
        int m = c % (2 * n);
        if (m < 0) m += 2 * n;
        return (m >= n) ? (2 * n - 1 - m) : m;
    }
    c %= n;                                                         /* WRAP */
    return (c < 0) ? c + n : c;
}

/* argb of the texel of a level; returns 0 when the colour key hides it */
static int rt_sample(const rtex *t, const struct IDirectDrawSurface_c *s, int x, int y, int ckey, int *r, int *g, int *b, int *a)
{
    uint32_t raw;

    if (s->bpp == 8)
    {
        uint32_t c;
        raw = s->pixels[(size_t)y * s->pitch + x];
        if (ckey && s->has_colorkey && (raw >= s->colorkey_low) && (raw <= s->colorkey_high)) return 0;
        c = (s->palette != NULL) ? s->palette->argb[raw] : (raw * 0x010101u);
        *r = (c >> 16) & 255; *g = (c >> 8) & 255; *b = c & 255; *a = 255;
        return 1;
    }
    if (s->bpp == 16)
    {
        int c[4], i;
        raw = ((const uint16_t *)(s->pixels + (size_t)y * s->pitch))[x];
        if (ckey && s->has_colorkey && (raw >= s->colorkey_low) && (raw <= s->colorkey_high)) return 0;
        for (i = 0; i < 4; i++)
        {
            c[i] = (t->max[i] == 0) ? 255 : (int)((((raw >> t->shift[i]) & t->max[i]) * 255u + t->max[i] / 2) / t->max[i]);
        }
        *r = c[0]; *g = c[1]; *b = c[2]; *a = c[3];
        return 1;
    }
    raw = ((const uint32_t *)(s->pixels + (size_t)y * s->pitch))[x];
    if (ckey && s->has_colorkey && (raw >= s->colorkey_low) && (raw <= s->colorkey_high)) return 0;
    *r = (raw >> 16) & 255; *g = (raw >> 8) & 255; *b = raw & 255; *a = (raw >> 24) ? (int)(raw >> 24) : 255;
    return 1;
}

/* linear filtering: the four texels around the sample, weights of the fraction;
 * the colour key cuts the footprint, then the nearest texel decides */
static int rt_sample_linear(const rtex *t, const struct IDirectDrawSurface_c *s, float fx, float fy, int ckey, uint32_t mode,
                            int *r, int *g, int *b, int *a)
{
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float wx = fx - (float)x0, wy = fy - (float)y0;
    int w = (int)s->width, h = (int)s->height;
    int c[4][4], ok[4], i, j;

    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < 2; i++)
        {
            int k = j * 2 + i;
            ok[k] = rt_sample(t, s, rt_addr(x0 + i, w, mode), rt_addr(y0 + j, h, mode),
                              ckey, &c[k][0], &c[k][1], &c[k][2], &c[k][3]);
        }
    }
    if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
    {
        int nx = (int)floorf(fx + 0.5f), ny = (int)floorf(fy + 0.5f);
        return rt_sample(t, s, rt_addr(nx, w, mode), rt_addr(ny, h, mode), ckey, r, g, b, a);
    }
    *r = (int)(((c[0][0] * (1.0f - wx) + c[1][0] * wx) * (1.0f - wy) +
                (c[2][0] * (1.0f - wx) + c[3][0] * wx) * wy) + 0.5f);
    *g = (int)(((c[0][1] * (1.0f - wx) + c[1][1] * wx) * (1.0f - wy) +
                (c[2][1] * (1.0f - wx) + c[3][1] * wx) * wy) + 0.5f);
    *b = (int)(((c[0][2] * (1.0f - wx) + c[1][2] * wx) * (1.0f - wy) +
                (c[2][2] * (1.0f - wx) + c[3][2] * wx) * wy) + 0.5f);
    *a = (int)(((c[0][3] * (1.0f - wx) + c[1][3] * wx) * (1.0f - wy) +
                (c[2][3] * (1.0f - wx) + c[3][3] * wx) * wy) + 0.5f);
    return 1;
}

/* the texel of one mip level, at the normalized (u,v) */
static int rt_texel(const rtex *t, int level, float u, float v, int linear, int ckey, uint32_t mode,
                    int *r, int *g, int *b, int *a)
{
    const struct IDirectDrawSurface_c *s = t->lv[level];
    int w = (int)s->width, h = (int)s->height;

    if (linear)
        return rt_sample_linear(t, s, u * (float)w - 0.5f, v * (float)h - 0.5f, ckey, mode, r, g, b, a);
    return rt_sample(t, s, rt_addr((int)floorf(u * (float)w), w, mode),
                     rt_addr((int)floorf(v * (float)h), h, mode), ckey, r, g, b, a);
}

/* the filtered texel: the mip level of the LOD, and two levels blended for the
 * linear mip filters (trilinear); a texture without a chain, or a filter that
 * asks for no mip level, samples level 0 */
static int rt_texel_lod(const rtex *t, float u, float v, float lod, uint32_t mn, uint32_t mg,
                        int ckey, uint32_t mode, int *r, int *g, int *b, int *a)
{
    int texel_linear = (mn == 2) || (mn == 4) || (mn == 6) || ((mg >= 2) && ((mg & 1) == 0));
    int levels = t->levels;
    int l0;
    float frac;
    int c0[4], c1[4];

    if ((levels < 2) || (mn < 3)) return rt_texel(t, 0, u, v, texel_linear, ckey, mode, r, g, b, a);

    if (lod < 0.0f) lod = 0.0f;
    if (lod > (float)(levels - 1)) lod = (float)(levels - 1);

    if ((mn == 3) || (mn == 4))     /* a nearest mip level */
    {
        l0 = (int)(lod + 0.5f);
        if (l0 >= levels) l0 = levels - 1;
        return rt_texel(t, l0, u, v, texel_linear, ckey, mode, r, g, b, a);
    }

    l0 = (int)floorf(lod);          /* linear between two levels */
    if (l0 >= levels - 1) return rt_texel(t, levels - 1, u, v, texel_linear, ckey, mode, r, g, b, a);
    frac = lod - (float)l0;

    if (!rt_texel(t, l0, u, v, texel_linear, ckey, mode, &c0[0], &c0[1], &c0[2], &c0[3])) return 0;
    if (!rt_texel(t, l0 + 1, u, v, texel_linear, ckey, mode, &c1[0], &c1[1], &c1[2], &c1[3])) return 0;
    *r = (int)floorf((float)c0[0] + ((float)c1[0] - (float)c0[0]) * frac + 0.5f);
    *g = (int)floorf((float)c0[1] + ((float)c1[1] - (float)c0[1]) * frac + 0.5f);
    *b = (int)floorf((float)c0[2] + ((float)c1[2] - (float)c0[2]) * frac + 0.5f);
    *a = (int)floorf((float)c0[3] + ((float)c1[3] - (float)c0[3]) * frac + 0.5f);
    return 1;
}

static int blend_factor(uint32_t mode, int sc, int sa, int dc, int da)
{
    switch (mode)
    {
        case 1: return 0;
        case 2: return 255;
        case 3: return sc;
        case 4: return 255 - sc;
        case 5: return sa;
        case 6: return 255 - sa;
        case 7: return da;
        case 8: return 255 - da;
        case 9: return dc;
        case 10: return 255 - dc;
        case 11: return sa;                 /* SRCALPHASAT */
        case 12: return sa;                 /* BOTHSRCALPHA */
        case 13: return 255 - sa;           /* BOTHINVSRCALPHA */
        default: return 255;
    }
}

static int z_test(uint32_t func, uint32_t z, uint32_t old)
{
    switch (func)
    {
        case 1: return 0;
        case 2: return z < old;
        case 3: return z == old;
        case 4: return z <= old;
        case 5: return z > old;
        case 6: return z != old;
        case 7: return z >= old;
        default: return 1;
    }
}

static inline int clamp255(int v) { return (v < 0) ? 0 : (v > 255 ? 255 : v); }

static void put_pixel(struct IDirectDrawSurface_c *t, int x, int y, int r, int g, int b)
{
    uint8_t *row = t->pixels + (size_t)y * t->pitch;
    if (t->bpp == 16) ((uint16_t *)row)[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    else if (t->bpp == 32) ((uint32_t *)row)[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void get_pixel(const struct IDirectDrawSurface_c *t, int x, int y, int *r, int *g, int *b)
{
    const uint8_t *row = t->pixels + (size_t)y * t->pitch;
    if (t->bpp == 16)
    {
        uint32_t v = ((const uint16_t *)row)[x];
        *r = (int)(((v >> 11) & 31) * 255 / 31); *g = (int)(((v >> 5) & 63) * 255 / 63); *b = (int)((v & 31) * 255 / 31);
    }
    else
    {
        uint32_t v = ((const uint32_t *)row)[x];
        *r = (v >> 16) & 255; *g = (v >> 8) & 255; *b = v & 255;
    }
}

static void load_vertex(rvert *o, const float *p)
{
    uint32_t c = ((const uint32_t *)p)[4], sp = ((const uint32_t *)p)[5];

    o->x = p[0]; o->y = p[1]; o->z = p[2]; o->rhw = p[3];
    o->a = (c >> 24) & 255; o->r = (c >> 16) & 255; o->g = (c >> 8) & 255; o->b = c & 255;
    o->sr = (sp >> 16) & 255; o->sg = (sp >> 8) & 255; o->sb = sp & 255;
    o->u = p[6]; o->v = p[7];
}

typedef struct {
    d3d_device *dev;
    struct IDirectDrawSurface_c *target, *zs;
    int cx1, cy1, cx2, cy2;
    rtex tex;
    int textured;
    int filt_linear;
} rctx;

static void raster_triangle(rctx *c, const rvert *v0, const rvert *v1, const rvert *v2)
{
    const uint32_t *st = c->dev->state;
    float area, ia;
    int minx, maxx, miny, maxy, x, y;
    int flat = (st[RS_SHADEMODE] == 1);
    int persp = (st[RS_TEXTUREPERSPECTIVE] != 0) && (v0->rhw > 0) && (v1->rhw > 0) && (v2->rhw > 0);
    int ztest = (st[RS_ZENABLE] != 0) && (c->zs != NULL);
    int zwrite = ztest && (st[RS_ZWRITEENABLE] != 0);
    int blend = (st[RS_ALPHABLENDENABLE] != 0);
    int ckey = (st[RS_COLORKEYENABLE] != 0);
    int spec = (st[RS_SPECULARENABLE] != 0);
    uint32_t cull = st[RS_CULLMODE], tmb = st[RS_TEXTUREMAPBLEND], addr = st[RS_TEXTUREADDRESS];
    uint32_t mn = st[RS_TEXTUREMIN], mg = st[RS_TEXTUREMAG];
    float fminx, fmaxx, fminy, fmaxy;
    /* the gradients of the barycentrics, and the texture coordinate gradients with
     * them: the mip level of a pixel comes from how many texels it covers */
    float dw0dx, dw1dx, dw2dx, dw0dy, dw1dy, dw2dy;
    float dux_a, dvx_a, duy_a, dvy_a;           /* affine interpolation */
    float dAux, dAvx, dBx, dAuy, dAvy, dBy;     /* perspective interpolation */
    int mips;

    area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
    if (area == 0.0f) return;
    if ((cull == 2) && (area > 0)) return;      /* CW: clockwise on the screen (y down) */
    if ((cull == 3) && (area < 0)) return;      /* CCW */
    ia = 1.0f / area;

    dw0dx = (v1->y - v2->y) * ia; dw1dx = (v2->y - v0->y) * ia; dw2dx = (v0->y - v1->y) * ia;
    dw0dy = (v2->x - v1->x) * ia; dw1dy = (v0->x - v2->x) * ia; dw2dy = (v1->x - v0->x) * ia;
    dux_a = dw0dx * v0->u + dw1dx * v1->u + dw2dx * v2->u;
    dvx_a = dw0dx * v0->v + dw1dx * v1->v + dw2dx * v2->v;
    duy_a = dw0dy * v0->u + dw1dy * v1->u + dw2dy * v2->u;
    dvy_a = dw0dy * v0->v + dw1dy * v1->v + dw2dy * v2->v;
    dAux = dw0dx * v0->u * v0->rhw + dw1dx * v1->u * v1->rhw + dw2dx * v2->u * v2->rhw;
    dAvx = dw0dx * v0->v * v0->rhw + dw1dx * v1->v * v1->rhw + dw2dx * v2->v * v2->rhw;
    dBx  = dw0dx * v0->rhw + dw1dx * v1->rhw + dw2dx * v2->rhw;
    dAuy = dw0dy * v0->u * v0->rhw + dw1dy * v1->u * v1->rhw + dw2dy * v2->u * v2->rhw;
    dAvy = dw0dy * v0->v * v0->rhw + dw1dy * v1->v * v1->rhw + dw2dy * v2->v * v2->rhw;
    dBy  = dw0dy * v0->rhw + dw1dy * v1->rhw + dw2dy * v2->rhw;
    mips = c->textured && (c->tex.levels > 1) && (mn >= 3) && (c->tex.lv[0]->mips_filled != 0);

    fminx = fminf(v0->x, fminf(v1->x, v2->x)); fmaxx = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    fminy = fminf(v0->y, fminf(v1->y, v2->y)); fmaxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    minx = (int)floorf(fminx); maxx = (int)ceilf(fmaxx);
    miny = (int)floorf(fminy); maxy = (int)ceilf(fmaxy);
    if (minx < c->cx1) minx = c->cx1;
    if (miny < c->cy1) miny = c->cy1;
    if (maxx > c->cx2) maxx = c->cx2;
    if (maxy > c->cy2) maxy = c->cy2;

    for (y = miny; y < maxy; y++)
    {
        float py = (float)y + 0.5f;
        for (x = minx; x < maxx; x++)
        {
            float px = (float)x + 0.5f;
            float w0 = ((v1->x - px) * (v2->y - py) - (v2->x - px) * (v1->y - py)) * ia;
            float w1 = ((v2->x - px) * (v0->y - py) - (v0->x - px) * (v2->y - py)) * ia;
            float w2 = 1.0f - w0 - w1;
            float z;
            uint32_t zi, zold = 0;
            int r, g, b, a, tr, tg, tb, ta;

            if ((w0 < 0) || (w1 < 0) || (w2 < 0)) continue;
            z = w0 * v0->z + w1 * v1->z + w2 * v2->z;
            if (z < 0) z = 0; else if (z > 1) z = 1;
            if (c->zs != NULL)
            {
                zi = (c->zs->bpp == 16) ? (uint32_t)(z * 65535.0f) : (uint32_t)(z * 4294967040.0f);
                if (c->zs->bpp == 16) zold = ((const uint16_t *)(c->zs->pixels + (size_t)y * c->zs->pitch))[x];
                else zold = ((const uint32_t *)(c->zs->pixels + (size_t)y * c->zs->pitch))[x];
                if (ztest && !z_test(st[23], zi, zold)) continue;
            }
            else zi = 0;

            if (flat)
            {
                r = v0->r; g = v0->g; b = v0->b; a = v0->a;
            }
            else
            {
                r = (int)(w0 * v0->r + w1 * v1->r + w2 * v2->r + 0.5f);
                g = (int)(w0 * v0->g + w1 * v1->g + w2 * v2->g + 0.5f);
                b = (int)(w0 * v0->b + w1 * v1->b + w2 * v2->b + 0.5f);
                a = (int)(w0 * v0->a + w1 * v1->a + w2 * v2->a + 0.5f);
            }

            if (c->textured)
            {
                float u, v;
                if (persp)
                {
                    float rw = w0 * v0->rhw + w1 * v1->rhw + w2 * v2->rhw;
                    float k = (rw != 0.0f) ? 1.0f / rw : 0.0f;
                    u = (w0 * v0->u * v0->rhw + w1 * v1->u * v1->rhw + w2 * v2->u * v2->rhw) * k;
                    v = (w0 * v0->v * v0->rhw + w1 * v1->v * v1->rhw + w2 * v2->v * v2->rhw) * k;
                }
                else
                {
                    u = w0 * v0->u + w1 * v1->u + w2 * v2->u;
                    v = w0 * v0->v + w1 * v1->v + w2 * v2->v;
                }
                if (mips)
                {
                    float sw = (float)c->tex.w, sh = (float)c->tex.h, lod, rho;
                    float rx, ry, sx, sy;
                    if (persp)
                    {
                        float rw = w0 * v0->rhw + w1 * v1->rhw + w2 * v2->rhw;
                        float k = (rw != 0.0f) ? 1.0f / rw : 0.0f;
                        rx = (dAux - u * dBx) * k * sw; ry = (dAvx - v * dBx) * k * sh;
                        sx = (dAuy - u * dBy) * k * sw; sy = (dAvy - v * dBy) * k * sh;
                    }
                    else
                    {
                        rx = dux_a * sw; ry = dvx_a * sh;
                        sx = duy_a * sw; sy = dvy_a * sh;
                    }
                    rho = fmaxf(sqrtf(rx * rx + ry * ry), sqrtf(sx * sx + sy * sy));
                    lod = (rho > 1e-6f) ? log2f(rho) : 0.0f;
                    if (!rt_texel_lod(&c->tex, u, v, lod, mn, mg, ckey, addr, &tr, &tg, &tb, &ta)) continue;
                }
                else
                {
                    if (!rt_texel(&c->tex, 0, u, v, c->filt_linear, ckey, addr, &tr, &tg, &tb, &ta)) continue;
                }
                switch (tmb)
                {
                    case 1: r = tr; g = tg; b = tb; break;                                          /* DECAL */
                    case 3: r = (tr * ta + r * (255 - ta)) / 255; g = (tg * ta + g * (255 - ta)) / 255;
                            b = (tb * ta + b * (255 - ta)) / 255; break;                            /* DECALALPHA */
                    case 4: r = r * tr / 255; g = g * tg / 255; b = b * tb / 255; a = a * ta / 255; break;   /* MODULATEALPHA */
                    case 7: r = tr; g = tg; b = tb; a = ta; break;                                  /* COPY */
                    default: r = r * tr / 255; g = g * tg / 255; b = b * tb / 255; a = ta; break;   /* MODULATE */
                }
            }
            if (spec)
            {
                r += (int)(w0 * v0->sr + w1 * v1->sr + w2 * v2->sr);
                g += (int)(w0 * v0->sg + w1 * v1->sg + w2 * v2->sg);
                b += (int)(w0 * v0->sb + w1 * v1->sb + w2 * v2->sb);
            }
            r = clamp255(r); g = clamp255(g); b = clamp255(b); a = clamp255(a);

            if (blend)
            {
                int dr, dg, db, fs, fd;
                uint32_t sm = st[RS_SRCBLEND], dm = st[RS_DESTBLEND];
                get_pixel(c->target, x, y, &dr, &dg, &db);
                fs = blend_factor(sm, r, a, dr, 255); fd = blend_factor(dm, r, a, dr, 255);
                if (sm == 12) fd = 255 - a;
                r = clamp255((r * fs + dr * fd) / 255);
                fs = blend_factor(sm, g, a, dg, 255); fd = blend_factor(dm, g, a, dg, 255);
                if (sm == 12) fd = 255 - a;
                g = clamp255((g * fs + dg * fd) / 255);
                fs = blend_factor(sm, b, a, db, 255); fd = blend_factor(dm, b, a, db, 255);
                if (sm == 12) fd = 255 - a;
                b = clamp255((b * fs + db * fd) / 255);
            }
            put_pixel(c->target, x, y, r, g, b);
            if (zwrite)
            {
                if (c->zs->bpp == 16) ((uint16_t *)(c->zs->pixels + (size_t)y * c->zs->pitch))[x] = (uint16_t)zi;
                else ((uint32_t *)(c->zs->pixels + (size_t)y * c->zs->pitch))[x] = zi;
            }
        }
    }
}

static void raster_line(rctx *c, const rvert *a, const rvert *b)
{
    int x0 = (int)a->x, y0 = (int)a->y, x1 = (int)b->x, y1 = (int)b->y;
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1, err = dx + dy;

    for (;;)
    {
        if ((x0 >= c->cx1) && (x0 < c->cx2) && (y0 >= c->cy1) && (y0 < c->cy2)) put_pixel(c->target, x0, y0, a->r, a->g, a->b);
        if ((x0 == x1) && (y0 == y1)) break;
        if (2 * err >= dy) { err += dy; x0 += sx; }
        if (2 * err <= dx) { err += dx; y0 += sy; }
    }
}

/* <GAME>_D3D_DUMP=dir: each texture as a PPM, once per handle */
static void dump_texture(uint32_t th, const struct IDirectDrawSurface_c *ts)
{
    static uint8_t done[MAX_TEXTURES];
    const char *dir = getenv(GAME_ENV("D3D_DUMP"));
    char name[512];
    FILE *f;
    uint32_t x, y;

    if (done[th - 1]) return;
    done[th - 1] = 1;
    d3d_log("D3D: texture %u: %ux%u bpp=%u pixels=%s\n", th,
            (unsigned)ts->width, (unsigned)ts->height, (unsigned)ts->bpp, ts->pixels != NULL ? "yes" : "no");
    if ((dir == NULL) || (ts->bpp != 16)) return;
    done[th - 1] = 1;
    snprintf(name, sizeof(name), "%s/tex%03u.ppm", dir, th);
    f = fopen(name, "wb");
    if (f == NULL) return;
    fprintf(f, "P6 %u %u 255\n", ts->width, ts->height);
    for (y = 0; y < ts->height; y++)
    {
        for (x = 0; x < ts->width; x++)
        {
            uint32_t v = ((const uint16_t *)(ts->pixels + (size_t)y * ts->pitch))[x];
            fputc((int)(((v >> 11) & 31) * 255 / 31), f); fputc((int)(((v >> 5) & 63) * 255 / 63), f); fputc((int)((v & 31) * 255 / 31), f);
        }
    }
    fclose(f);
}

/* type: 2 line list, 3 line strip, 4 triangle list, 5 strip, 6 fan; index == NULL: in order */
static uint32_t draw_primitive(d3d_device *dev, uint32_t type, uint32_t vtype, const float *verts, uint32_t nverts,
                               const uint16_t *index, uint32_t count)
{
    rctx c;
    rvert *rv;
    uint32_t i, n;
    uint32_t th = dev->state[RS_TEXTUREHANDLE];

    if (!dev->in_scene) return D3DERR_GENERIC;
    dev->draws++;
    dev->vertices += count;
    if ((vtype != 3) || (verts == NULL) || (dev->target == NULL)) return D3D_OK;     /* D3DVT_TLVERTEX only */
    if ((index != NULL) && (nverts == 0)) return D3D_OK;

    memset(&c, 0, sizeof(c));
    c.dev = dev;
    c.target = dev->target;
    c.zs = dev->target->zsurf;
    if ((c.target->bpp != 16) && (c.target->bpp != 32)) return D3D_OK;
    if ((c.zs != NULL) && (c.zs->bpp != 16) && (c.zs->bpp != 32)) c.zs = NULL;
    c.cx1 = 0; c.cy1 = 0; c.cx2 = (int)c.target->width; c.cy2 = (int)c.target->height;
    if (dev->current != NULL && dev->current->has_vp)
    {
        int x1 = (int)dev->current->vp[1], y1 = (int)dev->current->vp[2];
        int x2 = x1 + (int)dev->current->vp[3], y2 = y1 + (int)dev->current->vp[4];
        if (x1 > c.cx1) c.cx1 = x1;
        if (y1 > c.cy1) c.cy1 = y1;
        if (x2 < c.cx2) c.cx2 = x2;
        if (y2 < c.cy2) c.cy2 = y2;
    }
    if (th != 0 && !((th <= MAX_TEXTURES) && (texture_handles[th - 1] != NULL)))
    {
        static uint32_t warned;
        if (warned != th + 1) { warned = th + 1; d3d_log("D3D: texture handle %u is not bound\n", th); }
    }
    if ((th >= 1) && (th <= MAX_TEXTURES) && (texture_handles[th - 1] != NULL))
    {
        const struct IDirectDrawSurface_c *ts = texture_handles[th - 1]->surface;
        if ((ts->width != 0) && (ts->height != 0) && (ts->pixels != NULL))
        {
            /* D3DFILTER_LINEAR (2), MIPLINEAR (4) and LINEARMIPLINEAR (6) filter
             * the texels linearly; 3 and 5 sample the nearest texel */
            uint32_t mn = dev->state[RS_TEXTUREMIN], mg = dev->state[RS_TEXTUREMAG];
            rt_setup(&c.tex, ts);
            c.textured = 1;
            c.filt_linear = (mn == 2) || (mn == 4) || (mn == 6) || ((mg >= 2) && ((mg & 1) == 0));
        }
        dump_texture(th, ts);
    }

    n = (index != NULL) ? count : nverts;
    rv = (rvert *)malloc((size_t)(n ? n : 1) * sizeof(rvert));
    if (rv == NULL) return D3DERR_GENERIC;
    for (i = 0; i < n; i++)
    {
        uint32_t k = (index != NULL) ? index[i] : i;
        if (index != NULL && k >= nverts) k = 0;
        load_vertex(&rv[i], verts + (size_t)k * 8);
    }
#ifdef GAME_D3D_GL
    if (dev->gl_scene)
    {
        /* rvert and d3dgl_vert are the same pre-transformed vertex */
        const struct IDirectDrawSurface_c *ts = NULL;
        if (c.textured) ts = c.tex.s;
        Winapi_D3dGLDraw(type, (const d3dgl_vert *)rv, n, NULL, n, ts,
                         c.cx1, c.cy1, c.cx2 - c.cx1, c.cy2 - c.cy1);
        free(rv);
        return D3D_OK;
    }
#endif
    switch (type)
    {
        case 2: for (i = 0; i + 1 < n; i += 2) raster_line(&c, &rv[i], &rv[i + 1]); break;
        case 3: for (i = 0; i + 1 < n; i++) raster_line(&c, &rv[i], &rv[i + 1]); break;
        case 4: for (i = 0; i + 2 < n; i += 3) raster_triangle(&c, &rv[i], &rv[i + 1], &rv[i + 2]); break;
        case 5:
            for (i = 0; i + 2 < n; i++)
            {
                if (i & 1) raster_triangle(&c, &rv[i + 1], &rv[i], &rv[i + 2]);
                else raster_triangle(&c, &rv[i], &rv[i + 1], &rv[i + 2]);
            }
            break;
        case 6: for (i = 1; i + 1 < n; i++) raster_triangle(&c, &rv[0], &rv[i], &rv[i + 1]); break;
        default: break;
    }
    free(rv);
    return D3D_OK;
}

/* type, vertex type (D3DVT_TLVERTEX), vertices, count, flags */
uint32_t CCALL IDirect3DDevice2_DrawPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t /*a5*/)
{
    return draw_primitive((d3d_device *)lpThis, a1, a2, (const float *)a3, a4, NULL, a4);
}

/* type, vertex type, vertices, vertex count, indices (WORD), index count, flags */
uint32_t CCALL IDirect3DDevice2_DrawIndexedPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, void *a5, uint32_t a6, uint32_t /*a7*/)
{
    return draw_primitive((d3d_device *)lpThis, a1, a2, (const float *)a3, a4, (const uint16_t *)a5, a6);
}

uint32_t CCALL IDirect3DDevice2_SetClipStatus_c(void */*lpThis*/, void */*a1*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice2_GetClipStatus_c(void */*lpThis*/, void */*a1*/)
{
    return D3DERR_NOTIMPL;
}

/* ---- IDirect3DDevice (DirectX 3) ---- */
extern "C" {

uint32_t CCALL IDirect3DDevice_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_device1 *v1 = (d3d_device1 *)lpThis;
    uint32_t *out = (uint32_t *)ppvObj;

    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IUnknown_) || same_guid(riid, IID_IDirect3DDevice_))
    {
        v1->RefCount++;
        v1->dev->RefCount++;
        *out = GUEST_ADDR(v1);
        return D3D_OK;
    }
    if (same_guid(riid, IID_IDirect3DDevice2_))
    {
        v1->dev->RefCount++;
        *out = GUEST_ADDR(v1->dev);
        return D3D_OK;
    }
    if (same_guid(riid, IID_IDirect3DDevice3_))
    {
        d3d_device *dev = v1->dev;
        d3d_device3 *v3 = (d3d_device3 *)dev->v3;

        if (v3 == NULL)
        {
            v3 = (d3d_device3 *)new_object(sizeof(d3d_device3), &IDirect3DDevice3Vtbl_asm2c);
            if (v3 == NULL) { *out = 0; return D3DERR_GENERIC; }
            v3->dev = dev;
            dev->v3 = v3;
        }
        v3->RefCount++;
        dev->RefCount++;
        *out = GUEST_ADDR(v3);
        d3d_log("D3D: IDirect3DDevice3\n");
        return D3D_OK;
    }
    *out = 0;
    return D3DERR_NOINTERFACE;
}

uint32_t CCALL IDirect3DDevice_AddRef_c(void *lpThis)
{
    d3d_device1 *v1 = (d3d_device1 *)lpThis;
    v1->dev->RefCount++;
    return ++v1->RefCount;
}

uint32_t CCALL IDirect3DDevice_Release_c(void *lpThis)
{
    d3d_device1 *v1 = (d3d_device1 *)lpThis;

    if (--v1->RefCount != 0)
    {
        v1->dev->RefCount--;
        return v1->RefCount;
    }
    if (v1->dev->v1 == v1) v1->dev->v1 = NULL;
    v1->dev->RefCount--;
    IDirect3DDevice2_Release_c(v1->dev);
    x86_free(v1);
    return 0;
}

uint32_t CCALL IDirect3DDevice_Initialize_c(void */*lpThis*/, void */*a1*/, void */*a2*/, void */*a3*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_GetCaps_c(void */*lpThis*/, void *a1, void *a2)
{
    uint32_t *hw = (uint32_t *)a1, *hel = (uint32_t *)a2;
    uint32_t hwsize, helsize;

    if ((hw == NULL) || (hel == NULL)) return D3DERR_INVALIDARG;
    hwsize = hw[0];
    helsize = hel[0];
    if ((hwsize < 0xac) || (helsize < 0xac)) return D3DERR_INVALIDARG;
    if (hwsize > 0xcc) hwsize = 0xcc;
    if (helsize > 0xcc) helsize = 0xcc;
    fill_device_desc_size(hw, 1, hwsize);
    fill_device_desc_size(hel, 0, helsize);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_SwapTextureHandles_c(void */*lpThis*/, void *a1, void *a2)
{
    d3d_log("D3D: SwapTextureHandles(%p, %p)\n", a1, a2);
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice_CreateExecuteBuffer_c(void *lpThis, void *a1, void *a2, void */*a3*/)
{
    /* D3DEXECUTEBUFFERDESC: dwSize, dwFlags, dwCaps, dwBufferSize, lpData */
    uint32_t *desc = (uint32_t *)a1, *out = (uint32_t *)a2;
    d3d_execbuf *b;

    if (out == NULL) return D3DERR_POINTER;
    *out = 0;
    if ((desc == NULL) || (desc[0] < 20)) return D3DERR_INVALIDARG;

    b = (d3d_execbuf *)new_object(sizeof(d3d_execbuf), &IDirect3DExecuteBufferVtbl_asm2c);
    if (b == NULL) return D3DERR_GENERIC;
    b->device = (d3d_device1 *)lpThis;
    b->size = desc[3];
    if (((desc[1] & 0x4) != 0) && (desc[4] != 0))    /* D3DDEB_LPDATA: the game's memory */
    {
        b->data = desc[4];
    }
    else
    {
        void *mem = x86_malloc(b->size != 0 ? b->size : 1);
        if (mem == NULL) { x86_free(b); return D3DERR_GENERIC; }
        b->data = GUEST_ADDR(mem);
        b->owner = 1;
    }
    d3d_log("D3D: CreateExecuteBuffer size=%u\n", b->size);
    *out = GUEST_ADDR(b);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_GetStats_c(void */*lpThis*/, void */*a1*/)
{
    return D3DERR_NOTIMPL;
}

/* the instruction stream of an execute buffer, when <GAME>_D3D_LOG is set */
static void execute_buffer_trace(d3d_execbuf *b)
{
    uint8_t *base = (uint8_t *)(uintptr_t)GUEST_MEM(b->data);
    uint32_t off = b->exec_data[3], len = b->exec_data[4], end = off + len;

    d3d_log("D3D: Execute buffer=%08x vertices=%u+%u instructions=%u+%u\n",
            b->data, b->exec_data[1], b->exec_data[2], off, len);
    while ((base != NULL) && (off + 4 <= end))
    {
        uint8_t op = base[off], size = base[off + 1];
        uint16_t count = (uint16_t)(base[off + 2] | (base[off + 3] << 8));
        uint8_t *d = base + off + 4;
        unsigned i;

        d3d_log("  at %u: op=%u size=%u count=%u\n", off, op, size, count);
        if ((op == 11) || (size == 0)) break;       /* D3DOP_EXIT */
        switch (op)
        {
        case 8:     /* D3DOP_STATERENDER: D3DSTATE */
            for (i = 0; i < count; i++)
                d3d_log("    render state %u = 0x%x\n",
                        (unsigned)(((uint32_t *)d)[i * 2]), ((uint32_t *)d)[i * 2 + 1]);
            break;
        case 6:     /* D3DOP_STATETRANSFORM */
        case 7:     /* D3DOP_STATELIGHT */
            for (i = 0; i < count; i++)
                d3d_log("    state %u = 0x%x\n",
                        (unsigned)(((uint32_t *)d)[i * 2]), ((uint32_t *)d)[i * 2 + 1]);
            break;
        case 9:     /* D3DOP_PROCESSVERTICES: D3DPROCESSVERTICES */
            for (i = 0; i < count; i++)
            {
                uint32_t *pv = (uint32_t *)d + i * 4;
                d3d_log("    process flags=0x%x start=%u dest=%u count=%u\n",
                        pv[0], (unsigned)(pv[1] & 0xffff), (unsigned)(pv[1] >> 16), pv[2]);
            }
            break;
        case 4:     /* D3DOP_MATRIXLOAD: D3DMATRIXLOAD */
        case 5:     /* D3DOP_MATRIXMULTIPLY */
        case 10:    /* D3DOP_TEXTURELOAD: D3DTEXTURELOAD */
            for (i = 0; i < count; i++)
                d3d_log("    handles %u -> %u\n",
                        (unsigned)(((uint32_t *)d)[i * 2]), ((uint32_t *)d)[i * 2 + 1]);
            break;
        case 3:     /* D3DOP_TRIANGLE: D3DTRIANGLE */
            if (count != 0)
                d3d_log("    first triangle %u %u %u flags=0x%x\n",
                        (unsigned)(((uint16_t *)d)[0]), (unsigned)(((uint16_t *)d)[1]),
                        (unsigned)(((uint16_t *)d)[2]), (unsigned)(((uint16_t *)d)[3]));
            break;
        case 12:    /* D3DOP_BRANCHFORWARD: D3DBRANCH */
            d3d_log("    branch mask=0x%x value=0x%x negate=%u offset=%u\n",
                    ((uint32_t *)d)[0], ((uint32_t *)d)[1], ((uint32_t *)d)[2], ((uint32_t *)d)[3]);
            break;
        }
        off += 4 + (uint32_t)size * count;
    }
}

/* The instructions of an execute buffer, in order: the render states go to the
 * device, the triangle lists (pre-transformed vertices, the game sends
 * D3DPROCESSVERTICES_COPY) are drawn through the DirectX 5 device. */
static uint32_t execute_buffer_run(d3d_execbuf *b)
{
    d3d_device *dev = b->device->dev;
    uint8_t *base = (uint8_t *)(uintptr_t)GUEST_MEM(b->data);
    uint32_t voff = b->exec_data[1], vcount = b->exec_data[2];
    uint32_t off, end;

    if (base == NULL) return D3DERR_INVALIDARG;
    off = b->exec_data[3];
    end = off + b->exec_data[4];
    while (off + 4 <= end)
    {
        uint8_t op = base[off], size = base[off + 1];
        uint16_t count = (uint16_t)(base[off + 2] | (base[off + 3] << 8));
        uint8_t *d = base + off + 4;
        unsigned i;

        if (size == 0) break;

        switch (op)
        {
        case 8:             /* D3DOP_STATERENDER: D3DSTATE */
            for (i = 0; i < count; i++)
            {
                uint32_t state = ((uint32_t *)d)[i * 2], value = ((uint32_t *)d)[i * 2 + 1];
                if (state < MAX_STATES) dev->state[state] = value;
#ifdef GAME_D3D_GL
                if (dev->gl_scene) Winapi_D3dGLSetState(state, value);
#endif
                d3d_log("    render state %u = 0x%x\n", (unsigned)state, value);
            }
            break;
        case 6:             /* D3DOP_STATETRANSFORM */
        case 7:             /* D3DOP_STATELIGHT */
            for (i = 0; i < count; i++)
            {
                uint32_t state = ((uint32_t *)d)[i * 2], value = ((uint32_t *)d)[i * 2 + 1];
                d3d_log("    %s state %u = 0x%x\n", (op == 6) ? "transform" : "light",
                        (unsigned)state, value);
            }
            break;
        case 9:             /* D3DOP_PROCESSVERTICES: D3DPROCESSVERTICES */
            for (i = 0; i < count; i++)
            {
                uint32_t *pv = (uint32_t *)d + i * 4;
                uint32_t start = pv[1] & 0xffff, dest = pv[1] >> 16, n = pv[2];
                if ((pv[0] & 7) == 2)   /* D3DPROCESSVERTICES_COPY: already transformed */
                {
                    if ((start != dest) && (n != 0))
                        memmove(base + voff + dest * 32, base + voff + start * 32, (size_t)n * 32);
                }
                else
                {
                    d3d_log("    process vertices: flags 0x%x are not transformed\n", pv[0]);
                }
            }
            break;
        case 10:            /* D3DOP_TEXTURELOAD: D3DTEXTURELOAD */
            for (i = 0; i < count; i++)
            {
                uint32_t dst = ((uint32_t *)d)[i * 2], src = ((uint32_t *)d)[i * 2 + 1];
                if ((dst >= 1) && (dst <= MAX_TEXTURES) && (src >= 1) && (src <= MAX_TEXTURES))
                {
                    if ((texture_handles[dst - 1] != NULL) && (texture_handles[src - 1] != NULL))
                        Winapi_DDrawCopyLevels(texture_handles[dst - 1]->surface, texture_handles[src - 1]->surface);
                    texture_handles[dst - 1] = texture_handles[src - 1];
                }
            }
            break;
        case 3:             /* D3DOP_TRIANGLE: D3DTRIANGLE */
            if (count != 0)
            {
                static long exec_no;
                uint16_t *index = (uint16_t *)malloc((size_t)count * 3 * sizeof(uint16_t));
                if (index != NULL)
                {
                    for (i = 0; i < count; i++)
                    {
                        index[i * 3 + 0] = ((uint16_t *)d)[i * 4 + 0];
                        index[i * 3 + 1] = ((uint16_t *)d)[i * 4 + 1];
                        index[i * 3 + 2] = ((uint16_t *)d)[i * 4 + 2];
                    }
                    exec_no++;
                    draw_primitive(dev, 4 /* D3DPT_TRIANGLELIST */, 3 /* D3DVT_TLVERTEX */,
                                   (const float *)(base + voff), vcount, index, count * 3);
                    free(index);
                }
            }
            break;
        case 12:            /* D3DOP_BRANCHFORWARD: D3DBRANCH */
        {
            uint32_t mask = ((uint32_t *)d)[0], value = ((uint32_t *)d)[1];
            uint32_t negate = ((uint32_t *)d)[2], offset = ((uint32_t *)d)[3];
            int taken = (((b->exec_data[7] /* dsStatus.dwStatus */) & mask) == value) != (negate != 0);
            if (taken)
            {
                off += offset;
                continue;
            }
            break;
        }
        }
        off += 4 + (uint32_t)size * count;
    }
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_Execute_c(void */*lpThis*/, void *a1, void */*a2*/, uint32_t /*a3*/)
{
    d3d_execbuf *b = (d3d_execbuf *)a1;

    if (b == NULL) return D3DERR_INVALIDARG;
    d3d_log("D3D: Execute\n");
    if (d3d_logging()) execute_buffer_trace(b);
    return execute_buffer_run(b);
}

uint32_t CCALL IDirect3DDevice_AddViewport_c(void *lpThis, void *a1)
{
    return IDirect3DDevice2_AddViewport_c(((d3d_device1 *)lpThis)->dev, a1);
}

uint32_t CCALL IDirect3DDevice_DeleteViewport_c(void *lpThis, void *a1)
{
    return IDirect3DDevice2_DeleteViewport_c(((d3d_device1 *)lpThis)->dev, a1);
}

uint32_t CCALL IDirect3DDevice_NextViewport_c(void *lpThis, void *a1, void *a2, uint32_t a3)
{
    return IDirect3DDevice2_NextViewport_c(((d3d_device1 *)lpThis)->dev, a1, a2, a3);
}

uint32_t CCALL IDirect3DDevice_Pick_c(void */*lpThis*/, void */*a1*/, void */*a2*/, uint32_t /*a3*/, void */*a4*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice_GetPickRecords_c(void */*lpThis*/, void */*a1*/, void */*a2*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DDevice_EnumTextureFormats_c(void *lpThis, uint32_t cb, uint32_t ctx)
{
    return IDirect3DDevice2_EnumTextureFormats_c(((d3d_device1 *)lpThis)->dev, cb, ctx);
}

uint32_t CCALL IDirect3DDevice_CreateMatrix_c(void *lpThis, void *a1)
{
    d3d_device *dev = ((d3d_device1 *)lpThis)->dev;
    uint32_t i;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    for (i = 0; i < 16; i++)
    {
        if (!dev->matrix_used[i])
        {
            dev->matrix_used[i] = 1;
            *(uint32_t *)a1 = i + 1;
            return D3D_OK;
        }
    }
    return D3DERR_GENERIC;
}

uint32_t CCALL IDirect3DDevice_DeleteMatrix_c(void *lpThis, uint32_t handle)
{
    d3d_device *dev = ((d3d_device1 *)lpThis)->dev;

    if ((handle >= 1) && (handle <= 16)) dev->matrix_used[handle - 1] = 0;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_SetMatrix_c(void *lpThis, uint32_t handle, void *m)
{
    d3d_device *dev = ((d3d_device1 *)lpThis)->dev;

    if ((handle < 1) || (handle > 16) || (m == NULL)) return D3DERR_INVALIDARG;
    memcpy(dev->matrices[handle - 1], m, 16 * sizeof(uint32_t));
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_GetMatrix_c(void *lpThis, uint32_t handle, void *m)
{
    d3d_device *dev = ((d3d_device1 *)lpThis)->dev;

    if ((handle < 1) || (handle > 16) || (m == NULL)) return D3DERR_INVALIDARG;
    memcpy(m, dev->matrices[handle - 1], 16 * sizeof(uint32_t));
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice_BeginScene_c(void *lpThis)
{
    return IDirect3DDevice2_BeginScene_c(((d3d_device1 *)lpThis)->dev);
}

uint32_t CCALL IDirect3DDevice_EndScene_c(void *lpThis)
{
    return IDirect3DDevice2_EndScene_c(((d3d_device1 *)lpThis)->dev);
}

uint32_t CCALL IDirect3DDevice_GetDirect3D_c(void *lpThis, void *a1)
{
    return IDirect3DDevice2_GetDirect3D_c(((d3d_device1 *)lpThis)->dev, a1);
}

}

/* ---- IDirect3DDevice3 (DirectX 6) ---- */
extern "C" {

static uint32_t texture_handle_of(d3d_texture *t)
{
    int i;

    if (t == NULL) return 0;
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
    }
    return t->handle;
}

uint32_t CCALL IDirect3DDevice3_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_device3 *v3 = (d3d_device3 *)lpThis;
    uint32_t *out = (uint32_t *)ppvObj;

    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IUnknown_) || same_guid(riid, IID_IDirect3DDevice3_))
    {
        v3->RefCount++;
        v3->dev->RefCount++;
        *out = GUEST_ADDR(v3);
        return D3D_OK;
    }
    return IDirect3DDevice2_QueryInterface_c(v3->dev, riid, ppvObj);
}

uint32_t CCALL IDirect3DDevice3_AddRef_c(void *lpThis)
{
    d3d_device3 *v3 = (d3d_device3 *)lpThis;
    v3->dev->RefCount++;
    return ++v3->RefCount;
}

uint32_t CCALL IDirect3DDevice3_Release_c(void *lpThis)
{
    d3d_device3 *v3 = (d3d_device3 *)lpThis;

    if (--v3->RefCount != 0) { v3->dev->RefCount--; return v3->RefCount; }
    if (v3->dev->v3 == v3) v3->dev->v3 = NULL;
    IDirect3DDevice2_Release_c(v3->dev);
    x86_free(v3);
    return 0;
}

#define FORWARD1(name) \
uint32_t CCALL IDirect3DDevice3_##name##_c(void *lpThis) \
{ return IDirect3DDevice2_##name##_c(((d3d_device3 *)lpThis)->dev); }

#define FORWARD2P(name, A1) \
uint32_t CCALL IDirect3DDevice3_##name##_c(void *lpThis, void *A1) \
{ return IDirect3DDevice2_##name##_c(((d3d_device3 *)lpThis)->dev, A1); }

#define FORWARD2U(name, A1) \
uint32_t CCALL IDirect3DDevice3_##name##_c(void *lpThis, uint32_t A1) \
{ return IDirect3DDevice2_##name##_c(((d3d_device3 *)lpThis)->dev, A1); }

#define FORWARD3PU(name, A1, A2) \
uint32_t CCALL IDirect3DDevice3_##name##_c(void *lpThis, uint32_t A1, void *A2) \
{ return IDirect3DDevice2_##name##_c(((d3d_device3 *)lpThis)->dev, A1, A2); }

#define FORWARD3UU(name, A1, A2) \
uint32_t CCALL IDirect3DDevice3_##name##_c(void *lpThis, uint32_t A1, uint32_t A2) \
{ return IDirect3DDevice2_##name##_c(((d3d_device3 *)lpThis)->dev, A1, A2); }

uint32_t CCALL IDirect3DDevice3_GetCaps_c(void *lpThis, void *a1, void *a2)
{ return IDirect3DDevice2_GetCaps_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

FORWARD2P(GetStats, a1)
FORWARD2P(AddViewport, a1)
FORWARD2P(DeleteViewport, a1)

uint32_t CCALL IDirect3DDevice3_NextViewport_c(void *lpThis, void *a1, void *a2, uint32_t a3)
{ return IDirect3DDevice2_NextViewport_c(((d3d_device3 *)lpThis)->dev, a1, a2, a3); }

uint32_t CCALL IDirect3DDevice3_EnumTextureFormats_c(void *lpThis, uint32_t a1, uint32_t a2)
{ return IDirect3DDevice2_EnumTextureFormats_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

FORWARD1(BeginScene)
FORWARD1(EndScene)
FORWARD2P(GetDirect3D, a1)
FORWARD2P(SetCurrentViewport, a1)
FORWARD2P(GetCurrentViewport, a1)

uint32_t CCALL IDirect3DDevice3_SetRenderTarget_c(void *lpThis, void *a1, uint32_t a2)
{ return IDirect3DDevice2_SetRenderTarget_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

FORWARD2P(GetRenderTarget, a1)

uint32_t CCALL IDirect3DDevice3_Begin_c(void *lpThis, uint32_t a1, uint32_t a2, uint32_t a3)
{ return IDirect3DDevice2_Begin_c(((d3d_device3 *)lpThis)->dev, a1, a2, a3); }

uint32_t CCALL IDirect3DDevice3_BeginIndexed_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5)
{ return IDirect3DDevice2_BeginIndexed_c(((d3d_device3 *)lpThis)->dev, a1, a2, a3, a4, a5); }

uint32_t CCALL IDirect3DDevice3_Vertex_c(void *lpThis, void *a1)
{ return IDirect3DDevice2_Vertex_c(((d3d_device3 *)lpThis)->dev, a1); }

FORWARD2U(Index, a1)
FORWARD2U(End, a1)
FORWARD3PU(GetRenderState, a1, a2)
FORWARD3UU(SetRenderState, a1, a2)
FORWARD3PU(GetLightState, a1, a2)
FORWARD3UU(SetLightState, a1, a2)

uint32_t CCALL IDirect3DDevice3_SetTransform_c(void *lpThis, uint32_t a1, void *a2)
{ return IDirect3DDevice2_SetTransform_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

uint32_t CCALL IDirect3DDevice3_GetTransform_c(void *lpThis, uint32_t a1, void *a2)
{ return IDirect3DDevice2_GetTransform_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

uint32_t CCALL IDirect3DDevice3_MultiplyTransform_c(void *lpThis, uint32_t a1, void *a2)
{ return IDirect3DDevice2_MultiplyTransform_c(((d3d_device3 *)lpThis)->dev, a1, a2); }

uint32_t CCALL IDirect3DDevice3_DrawPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, uint32_t a5)
{ return IDirect3DDevice2_DrawPrimitive_c(((d3d_device3 *)lpThis)->dev, a1, a2, a3, a4, a5); }

uint32_t CCALL IDirect3DDevice3_DrawIndexedPrimitive_c(void *lpThis, uint32_t a1, uint32_t a2, void *a3, uint32_t a4, void *a5, uint32_t a6, uint32_t a7)
{ return IDirect3DDevice2_DrawIndexedPrimitive_c(((d3d_device3 *)lpThis)->dev, a1, a2, a3, a4, a5, a6, a7); }

uint32_t CCALL IDirect3DDevice3_SetClipStatus_c(void *lpThis, void *a1)
{ return IDirect3DDevice2_SetClipStatus_c(((d3d_device3 *)lpThis)->dev, a1); }

uint32_t CCALL IDirect3DDevice3_GetClipStatus_c(void *lpThis, void *a1)
{ return IDirect3DDevice2_GetClipStatus_c(((d3d_device3 *)lpThis)->dev, a1); }

uint32_t CCALL IDirect3DDevice3_DrawPrimitiveStrided_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/, void */*a3*/, uint32_t /*a4*/, uint32_t /*a5*/)
{ return D3DERR_NOTIMPL; }

uint32_t CCALL IDirect3DDevice3_DrawIndexedPrimitiveStrided_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/, void */*a3*/, uint32_t /*a4*/, void */*a5*/, uint32_t /*a6*/, uint32_t /*a7*/)
{ return D3DERR_NOTIMPL; }

uint32_t CCALL IDirect3DDevice3_DrawPrimitiveVB_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/, uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/)
{ return D3DERR_NOTIMPL; }

uint32_t CCALL IDirect3DDevice3_DrawIndexedPrimitiveVB_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/, void */*a3*/, uint32_t /*a4*/, uint32_t /*a5*/)
{ return D3DERR_NOTIMPL; }

uint32_t CCALL IDirect3DDevice3_ComputeSphereVisibility_c(void */*lpThis*/, void */*a1*/, void */*a2*/, uint32_t /*a3*/, uint32_t /*a4*/, void *a5)
{
    if (a5 != NULL) *(uint32_t *)a5 = 0;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice3_GetTexture_c(void */*lpThis*/, uint32_t /*a1*/, void *a2)
{
    if (a2 != NULL) *(uint32_t *)a2 = 0;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice3_SetTexture_c(void *lpThis, uint32_t a1, void *a2)
{
    d3d_device *dev = ((d3d_device3 *)lpThis)->dev;
    d3d_texture *t = (d3d_texture *)a2;

    d3d_log("D3D: SetTexture stage %u -> %p\n", (unsigned)a1, a2);
    if (a1 == 0)
    {
        dev->state[RS_TEXTUREHANDLE] = texture_handle_of(t);
#ifdef GAME_D3D_GL
        if (dev->gl_scene) Winapi_D3dGLSetState(RS_TEXTUREHANDLE, dev->state[RS_TEXTUREHANDLE]);
#endif
    }
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice3_GetTextureStageState_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/, void *a3)
{
    if (a3 != NULL) *(uint32_t *)a3 = 0;
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice3_SetTextureStageState_c(void *lpThis, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)lpThis;
    d3d_log("D3D: SetTextureStageState %u %u = 0x%x\n", (unsigned)a1, (unsigned)a2, a3);
    return D3D_OK;
}

uint32_t CCALL IDirect3DDevice3_ValidateDevice_c(void */*lpThis*/, void *a1)
{
    if (a1 != NULL) *(uint32_t *)a1 = 1;
    return D3D_OK;
}

}

/* ---- IDirect3DExecuteBuffer (DirectX 3) ---- */
extern "C" {

uint32_t CCALL IDirect3DExecuteBuffer_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_execbuf *b = (d3d_execbuf *)lpThis;
    uint32_t *out = (uint32_t *)ppvObj;

    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IUnknown_))
    {
        b->RefCount++;
        *out = GUEST_ADDR(b);
        return D3D_OK;
    }
    *out = 0;
    return D3DERR_NOINTERFACE;
}

uint32_t CCALL IDirect3DExecuteBuffer_AddRef_c(void *lpThis)
{
    return ++((d3d_execbuf *)lpThis)->RefCount;
}

uint32_t CCALL IDirect3DExecuteBuffer_Release_c(void *lpThis)
{
    d3d_execbuf *b = (d3d_execbuf *)lpThis;

    if (--b->RefCount != 0) return b->RefCount;
    if (b->owner && (b->data != 0)) x86_free((void *)(uintptr_t)GUEST_MEM(b->data));
    x86_free(b);
    return 0;
}

uint32_t CCALL IDirect3DExecuteBuffer_Initialize_c(void *lpThis, void *a1, void *a2)
{
    /* D3DEXECUTEBUFFERDESC: dwSize, dwFlags, dwCaps, dwBufferSize, lpData */
    d3d_execbuf *b = (d3d_execbuf *)lpThis;
    uint32_t *desc = (uint32_t *)a2;

    if (a1 != NULL) b->device = (d3d_device1 *)a1;
    if ((desc != NULL) && (desc[0] >= 20))
    {
        if (desc[3] != 0) b->size = desc[3];
        if (((desc[1] & 0x4) != 0) && (desc[4] != 0) && !b->owner) b->data = desc[4];
    }
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_Lock_c(void *lpThis, void *a1)
{
    /* D3DEXECUTEBUFFERDESC: dwSize, dwFlags, dwCaps, dwBufferSize, lpData */
    d3d_execbuf *b = (d3d_execbuf *)lpThis;
    uint32_t *desc = (uint32_t *)a1;

    if ((desc == NULL) || (desc[0] < 20)) return D3DERR_INVALIDARG;
    desc[3] = b->size;
    desc[4] = b->data;
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_Unlock_c(void */*lpThis*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_SetExecuteData_c(void *lpThis, void *a1)
{
    d3d_execbuf *b = (d3d_execbuf *)lpThis;
    uint32_t *data = (uint32_t *)a1;

    if ((data == NULL) || (data[0] < sizeof(b->exec_data))) return D3DERR_INVALIDARG;
    memcpy(b->exec_data, data, sizeof(b->exec_data));
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_GetExecuteData_c(void *lpThis, void *a1)
{
    d3d_execbuf *b = (d3d_execbuf *)lpThis;
    uint32_t *data = (uint32_t *)a1;

    if ((data == NULL) || (data[0] < sizeof(b->exec_data))) return D3DERR_INVALIDARG;
    memcpy(data, b->exec_data, sizeof(b->exec_data));
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_Validate_c(void */*lpThis*/, void */*a1*/, void */*a2*/, void */*a3*/, uint32_t /*a4*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DExecuteBuffer_Optimize_c(void */*lpThis*/, uint32_t /*a1*/)
{
    return D3D_OK;
}

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

uint32_t CCALL IDirect3DViewport2_Initialize_c(void */*lpThis*/, void */*a1*/)
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

uint32_t CCALL IDirect3DViewport2_TransformVertices_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/, uint32_t /*a3*/, void */*a4*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_LightElements_c(void */*lpThis*/, uint32_t /*a1*/, void */*a2*/)
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

uint32_t CCALL IDirect3DViewport2_SetBackgroundDepth_c(void */*lpThis*/, void */*a1*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_GetBackgroundDepth_c(void */*lpThis*/, void */*a1*/, void */*a2*/)
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

/* the colour of the material of the background as 0xFFRRGGBB, 0 if none */
static uint32_t background_argb(const d3d_viewport *vp)
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
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

/* the colour of the material of the background, in the format of the target */
static uint32_t background_value(const d3d_viewport *vp, const struct IDirectDrawSurface_c *target)
{
    uint32_t c = background_argb(vp);
    uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;

    if (c == 0) return 0;
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
#ifdef GAME_D3D_GL
        if (vp->device->gl_scene)
        {
            Winapi_D3dGLClear(target, x1, y1, x2, y2,
                              (a3 & D3DCLEAR_TARGET) != 0, background_argb(vp),
                              (a3 & D3DCLEAR_ZBUFFER) != 0);
        }
#endif
    }
    return D3D_OK;
}

uint32_t CCALL IDirect3DViewport2_AddLight_c(void */*lpThis*/, void */*a1*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_DeleteLight_c(void */*lpThis*/, void */*a1*/)
{
    return D3DERR_NOTIMPL;
}

uint32_t CCALL IDirect3DViewport2_NextLight_c(void */*lpThis*/, void */*a1*/, void */*a2*/, uint32_t /*a3*/)
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
    d3d_log("D3D: SetMaterial %p size=%u hTexture=%u ramp=%u\n", (void *)m,
            (unsigned)m->mat[0], (unsigned)m->mat[18], (unsigned)m->mat[19]);
    return D3D_OK;
}

uint32_t CCALL IDirect3DMaterial2_GetMaterial_c(void *lpThis, void *a1)
{
    d3d_material *m = (d3d_material *)lpThis;

    if (a1 == NULL) return D3DERR_INVALIDARG;
    memcpy(a1, m->mat, sizeof(m->mat));
    return D3D_OK;
}

uint32_t CCALL IDirect3DMaterial2_GetHandle_c(void *lpThis, void */*a1*/, void *a2)
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
    d3d_log("D3D: IDirect3DMaterial2::GetHandle %p -> %u (hTexture=%u)\n",
            (void *)m, (unsigned)m->handle, (unsigned)m->mat[18]);
    return D3D_OK;
}

/* ---- IDirect3DTexture2: a view of a surface made with DDSCAPS_TEXTURE ---- */

static d3d_texture *texture_for_surface(struct IDirectDrawSurface_c *surface)
{
    d3d_texture *t = (d3d_texture *)surface->d3dtex;

    if (t == NULL)
    {
        t = (d3d_texture *)x86_malloc(sizeof(d3d_texture));
        if (t == NULL) return NULL;
        memset(t, 0, sizeof(d3d_texture));
        t->lpVtbl = GUEST_ADDR(&IDirect3DTexture2Vtbl_asm2c);
        t->surface = surface;
        surface->d3dtex = t;
    }
    return t;
}

int Winapi_D3dSurfaceQuery(struct IDirectDrawSurface_c *surface, const void *riid, uint32_t *out)
{
    d3d_texture *t;

    if (!Winapi_Direct3DEnabled()) return 0;
    if (same_guid(riid, IID_IDirect3DTexture2_))
    {
        t = texture_for_surface(surface);
        if (t == NULL) { *out = 0; return 0; }
        surface->RefCount++;    /* the texture and the surface are one object: one count */
        d3d_log("D3D: surface QI -> IDirect3DTexture2 (DX5) %ux%u bpp %u\n",
                (unsigned)t->surface->width, (unsigned)t->surface->height, (unsigned)t->surface->bpp);
        *out = GUEST_ADDR(t);
        return 1;
    }
    if (same_guid(riid, IID_IDirect3DTexture_))
    {
        d3d_texture1 *v1;

        t = texture_for_surface(surface);
        if (t == NULL) { *out = 0; return 0; }
        v1 = (d3d_texture1 *)t->v1;
        if (v1 == NULL)
        {
            v1 = (d3d_texture1 *)x86_malloc(sizeof(d3d_texture1));
            if (v1 == NULL) { *out = 0; return 0; }
            memset(v1, 0, sizeof(d3d_texture1));
            v1->lpVtbl = GUEST_ADDR(&IDirect3DTextureVtbl_asm2c);
            v1->RefCount = 1;
            v1->tex = t;
            t->v1 = v1;
        }
        else v1->RefCount++;
        surface->RefCount++;
        d3d_log("D3D: surface QI -> IDirect3DTexture (DX3)\n");
        *out = GUEST_ADDR(v1);
        return 1;
    }
    return 0;
}

void Winapi_D3dSurfaceFree(struct IDirectDrawSurface_c *surface)
{
    d3d_texture *t = (d3d_texture *)surface->d3dtex;

    if (t == NULL) return;
    if (t->handle != 0) texture_handles[t->handle - 1] = NULL;
    if (t->v1 != NULL) x86_free(t->v1);
    surface->d3dtex = NULL;
    x86_free(t);
}

uint32_t CCALL IDirect3DTexture2_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    return IDirectDrawSurface_QueryInterface_c(((d3d_texture *)lpThis)->surface, riid, (PTR32(void) *)ppvObj);
}

uint32_t CCALL IDirect3DTexture2_AddRef_c(void *lpThis)
{
    d3d_log("D3D: IDirect3DTexture2::AddRef\n");
    return IDirectDrawSurface_AddRef_c(((d3d_texture *)lpThis)->surface);
}

uint32_t CCALL IDirect3DTexture2_Release_c(void *lpThis)
{
    d3d_log("D3D: IDirect3DTexture2::Release\n");
    return IDirectDrawSurface_Release_c(((d3d_texture *)lpThis)->surface);
}

uint32_t CCALL IDirect3DTexture2_GetHandle_c(void *lpThis, void */*a1*/, void *a2)
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
    d3d_log("D3D: IDirect3DTexture2::GetHandle -> %u\n", t->handle);
    return D3D_OK;
}

/* ---- IDirect3DTexture (DirectX 3) ---- */
extern "C" {

uint32_t CCALL IDirect3DTexture_QueryInterface_c(void *lpThis, void *riid, void *ppvObj)
{
    d3d_texture1 *v1 = (d3d_texture1 *)lpThis;
    uint32_t *out = (uint32_t *)ppvObj;

    if (out == NULL) return D3DERR_POINTER;
    if (same_guid(riid, IID_IUnknown_) || same_guid(riid, IID_IDirect3DTexture_))
    {
        v1->RefCount++;
        v1->tex->surface->RefCount++;
        *out = GUEST_ADDR(v1);
        return D3D_OK;
    }
    if (same_guid(riid, IID_IDirect3DTexture2_))
    {
        if (!Winapi_D3dSurfaceQuery(v1->tex->surface, riid, out)) { *out = 0; return D3DERR_NOINTERFACE; }
        return D3D_OK;
    }
    *out = 0;
    return D3DERR_NOINTERFACE;
}

uint32_t CCALL IDirect3DTexture_AddRef_c(void *lpThis)
{
    d3d_texture1 *v1 = (d3d_texture1 *)lpThis;
    v1->tex->surface->RefCount++;
    return ++v1->RefCount;
}

uint32_t CCALL IDirect3DTexture_Release_c(void *lpThis)
{
    d3d_texture1 *v1 = (d3d_texture1 *)lpThis;

    if (--v1->RefCount != 0) { v1->tex->surface->RefCount--; return v1->RefCount; }
    if (v1->tex->v1 == v1) v1->tex->v1 = NULL;
    v1->tex->surface->RefCount--;
    x86_free(v1);
    return 0;
}

uint32_t CCALL IDirect3DTexture_Initialize_c(void *lpThis, void */*a1*/, void *a2)
{
    /* the surface the texture is a view of is the one the game asked through */
    (void)lpThis; (void)a2;
    return D3D_OK;
}

uint32_t CCALL IDirect3DTexture_GetHandle_c(void *lpThis, void */*a1*/, void *a2)
{
    d3d_texture *t = ((d3d_texture1 *)lpThis)->tex;
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
    d3d_log("D3D: IDirect3DTexture::GetHandle -> %u\n", t->handle);
    return D3D_OK;
}

uint32_t CCALL IDirect3DTexture_PaletteChanged_c(void */*lpThis*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return D3D_OK;
}

uint32_t CCALL IDirect3DTexture_Load_c(void *lpThis, void *a1)
{
    d3d_log("D3D: IDirect3DTexture::Load(%p)\n", a1);
    (void)lpThis;
    return D3D_OK;
}

uint32_t CCALL IDirect3DTexture_Unload_c(void */*lpThis*/)
{
    d3d_log("D3D: IDirect3DTexture::Unload\n");
    return D3D_OK;
}

}

/* the palette of the surface is read when the texture is used */
uint32_t CCALL IDirect3DTexture2_PaletteChanged_c(void *lpThis, uint32_t /*a1*/, uint32_t /*a2*/)
{
    d3d_log("D3D: IDirect3DTexture2::PaletteChanged\n");
    ((d3d_texture *)lpThis)->surface->texture_gen++;
    return D3D_OK;
}

/* the pixels (and the palette) of another texture of the same size and format */
uint32_t CCALL IDirect3DTexture2_Load_c(void *lpThis, void *a1)
{
    d3d_log("D3D: IDirect3DTexture2::Load\n");
    struct IDirectDrawSurface_c *dst = ((d3d_texture *)lpThis)->surface;
    d3d_texture *from = (d3d_texture *)a1;
    struct IDirectDrawSurface_c *src;
    uint32_t y, row;

    if (from == NULL) return D3DERR_INVALIDARG;
    src = from->surface;
    d3d_log("D3D: Load %ux%u bpp %u -> %ux%u bpp %u\n", (unsigned)src->width, (unsigned)src->height, (unsigned)src->bpp,
            (unsigned)dst->width, (unsigned)dst->height, (unsigned)dst->bpp);
    if ((src->width != dst->width) || (src->height != dst->height) || (src->bpp != dst->bpp)) return D3DERR_INVALIDARG;
    d3d_log("D3D: Load %ux%u masks %x/%x/%x/%x -> %x/%x/%x/%x\n", src->width, src->height, src->rmask, src->gmask, src->bmask, src->amask, dst->rmask, dst->gmask, dst->bmask, dst->amask);
    row = dst->width * (dst->bpp >> 3);
    for (y = 0; y < dst->height; y++)
    {
        memcpy(dst->pixels + (size_t)y * dst->pitch, src->pixels + (size_t)y * src->pitch, row);
    }
    /* Direct3D's Load uploads the whole mip chain, not just level 0 */
    if ((src->lpmip != NULL) && (dst->lpmip != NULL)) Winapi_DDrawCopyLevels(dst, src);
    if (src->palette != NULL && dst->palette != NULL) memcpy(dst->palette->argb, src->palette->argb, sizeof(dst->palette->argb));
    dst->texture_gen++;
    return D3D_OK;
}

int Winapi_Direct3DGL(void)
{
#ifdef GAME_D3D_GL
    const char *e = getenv(GAME_ENV("D3D_GL"));
    if (!Winapi_Direct3DEnabled() || d3d_gl_failed) return 0;
    if (e != NULL) return atoi(e) != 0;
    /* the OpenGL backend is the normal one in a d3d_gl build */
    return (d3d_cfg_gl != 0);
#else
    return 0;
#endif
}

#ifdef GAME_D3D_GL

/* The presentation and the context live in the GL backend
 * (runtime/win32/gl/WinApi-d3dgl.c). */

void Winapi_D3dPresentSurface(struct IDirectDrawSurface_c *prim, struct SDL_Window *window)
{
    Winapi_D3dGLPresent(prim, window);
}

void Winapi_D3dPresentDeinit(void)
{
    Winapi_D3dGLShutdown();
}

#else

void Winapi_D3dPresentSurface(struct IDirectDrawSurface_c *prim, struct SDL_Window *window)
{
    (void)prim; (void)window;
}

void Winapi_D3dPresentDeinit(void)
{
}

#endif
