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

/* Glide 2.x on OpenGL. The colour buffers are OpenGL frame buffer objects
 * (RGBA8, the size of the screen); triangles are drawn with a shader that
 * does the gu* colour and alpha combine functions, texturing with the
 * mipmaps of a texture (the LODs as the GL levels, grTexMipMapMode and the
 * bias of grTexLodBiasValue) and chroma-key; the clip window is
 * the scissor, alpha blending the GL blend function. A linear frame buffer
 * lock gives the guest an RGB565 shadow of a buffer, with the stride of a
 * Voodoo's LFB (2048 bytes) in memory it can address: a lock reads the buffer
 * into it, the unlock of a write lock puts it back. grBufferSwap shows the
 * front buffer scaled to the window, which the DirectDraw layer makes.
 * The picture is 4:3, as a Voodoo's monitor showed every mode (640x400 has
 * pixels 1.2 times as high as wide); the window opens at the mode's width,
 * 4:3. With Display_3D_Resolution=WxH (or <GAME>_3D_RESOLUTION) the colour
 * buffers are W x H, and the coordinates of the guest stay those of the
 * screen: a lock reads a copy scaled down to the screen, and of what the
 * guest wrote only the pixels it changed go back, so the 3D under its 2D
 * keeps the resolution (with GAME_GLIDE_3D_RESOLUTION_ITEM the port's panel
 * changes it, the buffers made again between two frames). Display_Antialiasing=2|4|8|16 (or <GAME>_ANTIALIASING)
 * draws into multisampled buffers, resolved into the colour buffers before a
 * lock reads or a swap shows them.
 * grAADrawPoint and grAADrawLine add the edges of glide2x's antialiasing
 * (the iterated alpha falls to 0 across them); the grAADrawPolygon* are drawn
 * without. Left out: dithering, NCC (YIQ) textures, more than one TMU (and
 * so the blend of two LODs of lodBlend). GLIDE_DEBUG=1 in the environment logs the
 * calls. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "Game_gl.h"
#include "Glide2x.h"
#include "WinApi-config.h"
#include "Game_memory.h"
#include "WinApi-ddraw.h"
#include "WinApi-d3d.h"
#include "Game_osd.h"
#include "Game_hooks.h"

/* a toggle of the port that hides the fog the program turns on (Game_hooks.h) */
#ifndef GAME_GLIDE_FOG_HIDDEN
#define GAME_GLIDE_FOG_HIDDEN 0
#endif
/* the board reported to the program is a Voodoo2 SLI pair (Game_hooks.h): twice the frame
 * buffer memory, a depth buffer at 1024x768 for a program that sizes its modes by it */
#ifndef GAME_GLIDE_SLI
#define GAME_GLIDE_SLI 0
#endif
/* the item "3D resolution" of the port's panel (Game_hooks.h): Display_3D_Resolution
 * switched while the game runs */
#ifndef GAME_GLIDE_3D_RESOLUTION_ITEM
#define GAME_GLIDE_3D_RESOLUTION_ITEM 0
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#define FXTRUE 1
#define FXFALSE 0

/* of Game_vars.h, which cannot be included next to WinApi-config.h */
extern int Game_3DWidth, Game_3DHeight;

#ifdef __cplusplus
extern "C" {
#endif

#define GR_BUFFER_FRONTBUFFER 0
#define GR_BUFFER_BACKBUFFER 1
#define GR_BUFFER_AUXBUFFER 2

#define GR_LFB_READ_ONLY 0
#define GR_LFB_WRITE_ONLY 1

#define GR_LFBWRITEMODE_565 0
#define GR_LFBWRITEMODE_555 1
#define GR_LFBWRITEMODE_1555 2
#define GR_LFBWRITEMODE_888 4
#define GR_LFBWRITEMODE_8888 5
#define GR_LFBWRITEMODE_ZA16 15
#define GR_LFBWRITEMODE_ANY 0xff

#define GR_ORIGIN_UPPER_LEFT 0
#define GR_ORIGIN_LOWER_LEFT 1

#define GR_COLORFORMAT_ARGB 0
#define GR_COLORFORMAT_ABGR 1
#define GR_COLORFORMAT_RGBA 2
#define GR_COLORFORMAT_BGRA 3

#define GR_CULL_DISABLE 0
#define GR_CULL_NEGATIVE 1
#define GR_CULL_POSITIVE 2

#define GR_DEPTHBUFFER_DISABLE 0
#define GR_DEPTHBUFFER_ZBUFFER 1
#define GR_DEPTHBUFFER_WBUFFER 2
#define GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS 3
#define GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS 4

#define GR_CMP_NEVER 0
#define GR_CMP_LESS 1
#define GR_CMP_EQUAL 2
#define GR_CMP_LEQUAL 3
#define GR_CMP_GREATER 4
#define GR_CMP_NOTEQUAL 5
#define GR_CMP_GEQUAL 6
#define GR_CMP_ALWAYS 7

#define GR_BLEND_ZERO 0x0
#define GR_BLEND_SRC_ALPHA 0x1
#define GR_BLEND_SRC_COLOR 0x2
#define GR_BLEND_DST_COLOR 0x2
#define GR_BLEND_DST_ALPHA 0x3
#define GR_BLEND_ONE 0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA 0x5
#define GR_BLEND_ONE_MINUS_SRC_COLOR 0x6
#define GR_BLEND_ONE_MINUS_DST_COLOR 0x6
#define GR_BLEND_ONE_MINUS_DST_ALPHA 0x7
#define GR_BLEND_ALPHA_SATURATE 0xf

#define GR_COLORCOMBINE_ZERO 0x0
#define GR_COLORCOMBINE_CCRGB 0x1
#define GR_COLORCOMBINE_ITRGB 0x2
#define GR_COLORCOMBINE_ITRGB_DELTA0 0x3
#define GR_COLORCOMBINE_DECAL_TEXTURE 0x4
#define GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB 0x5
#define GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB 0x6
#define GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0 0x7
#define GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA 0x8
#define GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA 0x9
#define GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB 0xa
#define GR_COLORCOMBINE_TEXTURE_ADD_ITRGB 0xb
#define GR_COLORCOMBINE_TEXTURE_SUB_ITRGB 0xc
#define GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA 0xd
#define GR_COLORCOMBINE_DIFF_SPEC_A 0xe
#define GR_COLORCOMBINE_DIFF_SPEC_B 0xf
#define GR_COLORCOMBINE_ONE 0x10

#define GR_ALPHASOURCE_CC_ALPHA 0x0
#define GR_ALPHASOURCE_ITERATED_ALPHA 0x1
#define GR_ALPHASOURCE_TEXTURE_ALPHA 0x2
#define GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA 0x3

#define GR_TEXTURECOMBINE_ZERO 0x0
#define GR_TEXTURECOMBINE_DECAL 0x1
#define GR_TEXTURECOMBINE_ONE 0xa

#define GR_CHROMAKEY_DISABLE 0
#define GR_CHROMAKEY_ENABLE 1

#define GR_TEXTABLE_NCC0 0
#define GR_TEXTABLE_NCC1 1
#define GR_TEXTABLE_PALETTE 2

#define GR_TEXFMT_RGB_332 0x0
#define GR_TEXFMT_YIQ_422 0x1
#define GR_TEXFMT_ALPHA_8 0x2
#define GR_TEXFMT_INTENSITY_8 0x3
#define GR_TEXFMT_ALPHA_INTENSITY_44 0x4
#define GR_TEXFMT_P_8 0x5
#define GR_TEXFMT_ARGB_8332 0x8
#define GR_TEXFMT_AYIQ_8422 0x9
#define GR_TEXFMT_RGB_565 0xa
#define GR_TEXFMT_ARGB_1555 0xb
#define GR_TEXFMT_ARGB_4444 0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88 0xd
#define GR_TEXFMT_AP_88 0xe

#define GR_TEXTURECLAMP_WRAP 0
#define GR_TEXTURECLAMP_CLAMP 1

#define GR_TEXTUREFILTER_POINT_SAMPLED 0
#define GR_TEXTUREFILTER_BILINEAR 1

#define GR_MIPMAP_DISABLE 0
#define GR_MIPMAP_NEAREST 1
#define GR_MIPMAP_NEAREST_DITHER 2

#define GR_NULL_MIPMAP_HANDLE 0xffffffffu

#define GR_SSTTYPE_VOODOO 0

/* the stride of the linear frame buffer of a Voodoo, in pixels */
#define LFB_STRIDE 1024
/* the size of the GrState of glide2x.h, which grGlideGetState fills */
#define GRSTATE_SIZE 312
#define MAX_TEXTURES 4096
/* the part of a refresh period that counts as the vertical retrace */
#define VBLANK_PART 0.05


/* the guest's GrVertex (glide2x.h, GLIDE_NUM_TMU = 3) */
struct gr_tmu_vertex {
    float sow, tow, oow;
};

struct gr_vertex {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    struct gr_tmu_vertex tmuvtx[3];
};

struct gr_lfb_info {
    int32_t size;
    uint32_t lfbPtr;
    uint32_t strideInBytes;
    uint32_t writeMode;
    uint32_t origin;
};

/* the guest's GrHwConfiguration: num_sst, then 4 x { type, the largest board
 * configuration (Voodoo2: fbRam, fbiRev, nTexelfx, sliDetect, 3 x { tmuRev, tmuRam }) } */
struct gr_hw_config {
    int32_t num_sst;
    struct {
        int32_t type;
        int32_t fbRam;
        int32_t fbiRev;
        int32_t nTexelfx;
        int32_t sliDetect;
        struct {
            int32_t tmuRev;
            int32_t tmuRam;
        } tmuConfig[3];
    } SSTs[4];
};

/* everything grGlideGetState/grGlideSetState carry */
struct glide_state {
    uint32_t color_combine, alpha_source, tex_combine;
    uint32_t rgb_sf, rgb_df, alpha_sf, alpha_df;
    uint32_t constant_color;
    uint32_t chromakey_mode, chromakey_value;
    uint32_t cull_mode;
    uint32_t depth_mode, depth_function, depth_mask;
    uint32_t render_buffer;
    int32_t clip_minx, clip_miny, clip_maxx, clip_maxy;
    uint32_t texture;
    uint32_t alpha_test_function, alpha_test_reference;
    int32_t depth_bias;
    /* grColorCombine / grAlphaCombine as the program gave them (cc_raw / ac_raw: instead of the
     * guColorCombineFunction / guAlphaSource value above) */
    uint32_t cc_raw, cc_function, cc_factor, cc_local, cc_other, cc_invert;
    uint32_t ac_raw, ac_function, ac_factor, ac_local, ac_other, ac_invert;
    /* grTexSource: the texture at an address of the texture memory (tex_raw), with the clamp and
     * filter modes of grTexClampMode and grTexFilterMode */
    uint32_t tex_raw, tex_addr, tex_format, tex_small, tex_large, tex_aspect;
    uint32_t tex_sclamp, tex_tclamp, tex_minfilter, tex_magfilter;
    uint32_t fog_mode, fog_color;
    /* grTexMipMapMode, grTexLodBiasValue: of the textures of grTexSource */
    uint32_t mipmap_mode;
    float lod_bias;
};

struct glide_texture {
    int used;
    uint32_t format;
    uint32_t width, height;         /* of the largest LOD */
    uint32_t largest_lod, smallest_lod, aspect;
    uint32_t s_clamp, t_clamp, minfilter, magfilter;
    uint32_t data_size;             /* of all the LODs, in bytes */
    uint8_t *raw;                   /* the LODs as downloaded */
    uint32_t *argb;                 /* the LODs, decoded */
    uint32_t mm_mode;               /* guTexAllocateMemory: GR_MIPMAP_*, the bias */
    float lod_bias;
    GLint gl_min;                   /* the GL minification filter set */
    uint32_t decoded_palette;       /* palette_serial at decoding (paletted formats) */
    int downloaded;
    GLuint gltex;                   /* the decoded image on the GPU */
    int gl_dirty;                   /* argb changed since the upload */
};

static int glide_debug = -1;
static void gl_flush(void);
static void present_front(void);
static void front_drawn(void);
static void shadow_to_gl(int pi);

static struct glide_state st;
static float fog_table[64];         /* grFogTable: the fog of 0..1 at the W of 64 steps */

/* The program may open the window in one thread and draw in another (a driver DLL of the program does): the
 * OpenGL context belongs to the thread that made it current.  grSstWinOpen and grBufferSwap let it go, and a
 * function that uses OpenGL takes it for its thread when it is not its own.  A context current in another
 * thread cannot be taken (EGL_BAD_ACCESS, BadAccess of GLX): the drawing thread gives it up at the end of its
 * frame, so the other thread waits for that (a game may close the window and open another one from its main
 * thread while its driver draws in its own). */
static SDL_Window *gl_window;
static SDL_GLContext gl_context;
static int gl_ok;

static void gl_acquire(void)
{
    int tries;

    if (!gl_ok || SDL_GL_GetCurrentContext() == gl_context) return;
    for (tries = 0; SDL_GL_MakeCurrent(gl_window, gl_context) != 0; tries++)
    {
        if (tries == 500)
        {
            eprintf("Glide: cannot make the OpenGL context current: %s\n", SDL_GetError());
            return;
        }
        SDL_Delay(1);
    }
}

static void gl_release(void)
{
    if (gl_ok && SDL_GL_GetCurrentContext() == gl_context) SDL_GL_MakeCurrent(gl_window, NULL);
}

static int win_open = 0;
static uint32_t scr_width, scr_height, scr_stride;
static uint32_t color_format, buffer_origin, num_color_buffers, num_aux_buffers;
static double refresh_hz = 60.0;

static uint16_t *color_mem[3];
static uint16_t *aux_mem;
/* buffer[0] is the front buffer, buffer[1] the back buffer, buffer[2] the third */
static uint16_t *buffer[3];

static struct glide_texture textures[MAX_TEXTURES];
static uint32_t num_textures;
static uint32_t palette[256];
static uint32_t palette_serial = 1;

/* a lock of the frame buffer in a format other than RGB565 */
static void *lfb_scratch;
static uint32_t lfb_scratch_buffer, lfb_scratch_bpp;
static int lfb_scratch_write;

static uint64_t vr_base;


#define DEBUG(...) do { if (glide_debug > 0) eprintf(__VA_ARGS__); } while (0)

static void init_debug(void)
{
    if (glide_debug < 0)
    {
        const char *env = getenv("GLIDE_DEBUG");
        glide_debug = (env != NULL && env[0] != 0 && env[0] != '0') ? 1 : 0;
    }
}

static void reset_state(void)
{
    memset(&st, 0, sizeof(st));
    st.color_combine = GR_COLORCOMBINE_ITRGB;
    st.alpha_source = GR_ALPHASOURCE_ITERATED_ALPHA;
    st.tex_combine = GR_TEXTURECOMBINE_DECAL;
    st.alpha_test_function = 7;         /* GR_CMP_ALWAYS */
    st.rgb_sf = GR_BLEND_ONE;
    st.rgb_df = GR_BLEND_ZERO;
    st.alpha_sf = GR_BLEND_ONE;
    st.alpha_df = GR_BLEND_ZERO;
    st.cull_mode = GR_CULL_DISABLE;
    st.depth_mode = GR_DEPTHBUFFER_DISABLE;
    st.depth_function = GR_CMP_LESS;
    st.depth_mask = FXFALSE;
    st.render_buffer = GR_BUFFER_BACKBUFFER;
    st.clip_minx = 0;
    st.clip_miny = 0;
    st.clip_maxx = (int32_t)scr_width;
    st.clip_maxy = (int32_t)scr_height;
    st.texture = GR_NULL_MIPMAP_HANDLE;
}

/* a GrColor_t in the colour format of grSstWinOpen -> 0xAARRGGBB */
static uint32_t color_to_argb(uint32_t c)
{
    switch (color_format)
    {
        case GR_COLORFORMAT_ABGR:
            return (c & 0xff00ff00u) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
        case GR_COLORFORMAT_RGBA:
            return (c >> 8) | (c << 24);
        case GR_COLORFORMAT_BGRA:
            return ((c & 0xff) << 24) | ((c >> 24) & 0xff) | ((c >> 8) & 0xff00) | ((c << 8) & 0xff0000);
        default:
            return c;
    }
}

static inline uint16_t argb_to_565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
}

static inline uint32_t rgb565_to_argb(uint32_t p)
{
    uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

static inline uint32_t argb1555_to_argb(uint32_t p)
{
    uint32_t r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
    return ((p & 0x8000) ? 0xff000000u : 0) | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

static uint16_t *buffer_of(uint32_t which)
{
    switch (which)
    {
        case GR_BUFFER_FRONTBUFFER: return buffer[0];
        case GR_BUFFER_BACKBUFFER: return buffer[1];
        case GR_BUFFER_AUXBUFFER: return aux_mem;
        default: return NULL;
    }
}


/* ---- the vertical retrace, from a clock of the refresh rate ---- */

static double vr_frame_position(void)
{
    uint64_t now = SDL_GetPerformanceCounter();
    double t = (double)(now - vr_base) / (double)SDL_GetPerformanceFrequency();
    return t * refresh_hz;
}

static int vr_in_retrace(void)
{
    double pos = vr_frame_position();
    return (pos - floor(pos)) < VBLANK_PART;
}

static void vr_wait_retrace(void)
{
    double pos = vr_frame_position();
    double left = (ceil(pos) - pos) / refresh_hz;

    if ((pos - floor(pos)) < VBLANK_PART) return;
    if (left > 0.0)
    {
        SDL_Delay((uint32_t)(left * 1000.0));
    }
    while (!vr_in_retrace())
    {
        SDL_Delay(0);
    }
}


/* ---- textures ---- */

static uint32_t lod_size(uint32_t lod)
{
    /* GR_LOD_256 = 0 ... GR_LOD_1 = 8 */
    return (lod <= 8) ? (256u >> lod) : 1;
}

static int texel_bytes(uint32_t format)
{
    return (format >= GR_TEXFMT_ARGB_8332) ? 2 : 1;
}

static void decode_texture(struct glide_texture *t)
{
    uint32_t n = t->data_size / texel_bytes(t->format), i;
    const uint8_t *s8 = t->raw;
    const uint16_t *s16 = (const uint16_t *)t->raw;
    uint32_t *d = t->argb;

    for (i = 0; i < n; i++)
    {
        uint32_t p, a, r, g, b;
        switch (t->format)
        {
            case GR_TEXFMT_RGB_332:
                p = s8[i];
                r = (p >> 5) & 7; g = (p >> 2) & 7; b = p & 3;
                d[i] = 0xff000000u | (((r << 5) | (r << 2) | (r >> 1)) << 16) | (((g << 5) | (g << 2) | (g >> 1)) << 8) | (b * 0x55);
                break;
            case GR_TEXFMT_ALPHA_8:
                d[i] = ((uint32_t)s8[i] << 24) | 0xffffff;
                break;
            case GR_TEXFMT_INTENSITY_8:
                d[i] = 0xff000000u | (s8[i] * 0x010101u);
                break;
            case GR_TEXFMT_ALPHA_INTENSITY_44:
                a = (s8[i] >> 4) * 0x11; p = (s8[i] & 15) * 0x11;
                d[i] = (a << 24) | (p * 0x010101u);
                break;
            case GR_TEXFMT_P_8:
                d[i] = 0xff000000u | (palette[s8[i]] & 0xffffff);
                break;
            case GR_TEXFMT_ARGB_8332:
                p = s16[i] & 0xff;
                r = (p >> 5) & 7; g = (p >> 2) & 7; b = p & 3;
                d[i] = ((uint32_t)(s16[i] >> 8) << 24) | (((r << 5) | (r << 2) | (r >> 1)) << 16) | (((g << 5) | (g << 2) | (g >> 1)) << 8) | (b * 0x55);
                break;
            case GR_TEXFMT_RGB_565:
                d[i] = rgb565_to_argb(s16[i]);
                break;
            case GR_TEXFMT_ARGB_1555:
                d[i] = argb1555_to_argb(s16[i]);
                break;
            case GR_TEXFMT_ARGB_4444:
                p = s16[i];
                d[i] = (((p >> 12) & 15) * 0x11000000u) | (((p >> 8) & 15) * 0x110000u) | (((p >> 4) & 15) * 0x1100u) | ((p & 15) * 0x11u);
                break;
            case GR_TEXFMT_ALPHA_INTENSITY_88:
                d[i] = ((uint32_t)(s16[i] >> 8) << 24) | ((s16[i] & 0xff) * 0x010101u);
                break;
            case GR_TEXFMT_AP_88:
                d[i] = ((uint32_t)(s16[i] >> 8) << 24) | (palette[s16[i] & 0xff] & 0xffffff);
                break;
            default:
                /* YIQ: no NCC tables */
                d[i] = 0xff808080u;
                break;
        }
    }

    t->decoded_palette = palette_serial;
    t->gl_dirty = 1;
}

/* ---- the texture memory of the board: grTexDownloadMipMap puts the texels in at an address,
 * grTexSource says which texture the pixels take; the texture is decoded when it is first drawn with ---- */

#define TMU_MEM_SIZE 0x400000u
#define MAX_RAW_TEXTURES 1024

/* the guest's GrTexInfo */
struct gr_tex_info {
    int32_t smallLod, largeLod, aspectRatio, format;
    uint32_t data;
};

static uint8_t *tmu_mem;
static struct raw_texture {
    uint32_t addr, size, format, small, large, aspect;
    struct glide_texture tex;
} raw_textures[MAX_RAW_TEXTURES];
static uint32_t num_raw_textures;

/* the size of the largest LOD in texels, and the bytes of all the LODs of a mipmap */
static void tex_dimensions(uint32_t large, uint32_t aspect, uint32_t *w, uint32_t *h)
{
    uint32_t big = lod_size(large);

    *w = (aspect <= 3) ? big : (big >> (aspect - 3));
    *h = (aspect >= 3) ? big : (big >> (3 - aspect));
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

static uint32_t tex_mipmap_bytes(uint32_t small, uint32_t large, uint32_t aspect, uint32_t format)
{
    uint32_t w, h, lod, size = 0;

    tex_dimensions(large, aspect, &w, &h);
    for (lod = large; lod <= small; lod++)
    {
        uint32_t lw = w >> (lod - large), lh = h >> (lod - large);
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        size += lw * lh * texel_bytes(format);
    }
    return size;
}

static void raw_texture_free(struct raw_texture *r)
{
    if (r->tex.gltex != 0 && gl_ok)
    {
        gl_acquire();
        glDeleteTextures(1, &r->tex.gltex);
    }
    free(r->tex.raw);
    free(r->tex.argb);
    memset(r, 0, sizeof(*r));
}

static void free_raw_textures(void)
{
    uint32_t i;
    for (i = 0; i < num_raw_textures; i++) raw_texture_free(&raw_textures[i]);
    num_raw_textures = 0;
    free(tmu_mem);
    tmu_mem = NULL;
}

/* the texture grTexSource named: found by what it is and where, made from the texture memory when it is new */
static struct glide_texture *raw_texture(void)
{
    struct raw_texture *r = NULL;
    uint32_t i;

    if (tmu_mem == NULL) return NULL;
    for (i = 0; i < num_raw_textures; i++)
    {
        struct raw_texture *c = &raw_textures[i];
        if (c->tex.used && c->addr == st.tex_addr && c->format == st.tex_format && c->small == st.tex_small
            && c->large == st.tex_large && c->aspect == st.tex_aspect)
        {
            r = c;
            break;
        }
        if (r == NULL && !c->tex.used) r = c;       /* a free place, if there is no such texture */
    }
    if (r == NULL || !r->tex.used || r->addr != st.tex_addr || r->format != st.tex_format
        || r->small != st.tex_small || r->large != st.tex_large || r->aspect != st.tex_aspect)
    {
        uint32_t w, h, size = tex_mipmap_bytes(st.tex_small, st.tex_large, st.tex_aspect, st.tex_format);

        if (st.tex_addr + size > TMU_MEM_SIZE) return NULL;
        if (r == NULL)
        {
            if (num_raw_textures >= MAX_RAW_TEXTURES)
            {
                /* the oldest goes: the textures of one frame fit */
                raw_texture_free(&raw_textures[0]);
                r = &raw_textures[0];
            }
            else r = &raw_textures[num_raw_textures++];
        }
        else if (r->tex.used) raw_texture_free(r);
        memset(r, 0, sizeof(*r));
        tex_dimensions(st.tex_large, st.tex_aspect, &w, &h);
        r->addr = st.tex_addr;
        r->size = size;
        r->format = st.tex_format;
        r->small = st.tex_small;
        r->large = st.tex_large;
        r->aspect = st.tex_aspect;
        r->tex.width = w;
        r->tex.height = h;
        r->tex.format = st.tex_format;
        r->tex.largest_lod = st.tex_large;
        r->tex.smallest_lod = st.tex_small;
        r->tex.aspect = st.tex_aspect;
        r->tex.data_size = size;
        r->tex.raw = (uint8_t *)malloc(size);
        r->tex.argb = (uint32_t *)malloc(size / texel_bytes(st.tex_format) * 4);
        if (r->tex.raw == NULL || r->tex.argb == NULL)
        {
            raw_texture_free(r);
            return NULL;
        }
        /* the LODs follow each other at the address, the largest first */
        memcpy(r->tex.raw, tmu_mem + st.tex_addr, size);
        r->tex.used = 1;
        r->tex.downloaded = 1;
        decode_texture(&r->tex);
        r->tex.s_clamp = ~st.tex_sclamp;                /* the wrap and filter of the draw: set below */
    }
    if (r->tex.s_clamp != st.tex_sclamp || r->tex.t_clamp != st.tex_tclamp
        || r->tex.minfilter != st.tex_minfilter || r->tex.magfilter != st.tex_magfilter)
    {
        r->tex.s_clamp = st.tex_sclamp;
        r->tex.t_clamp = st.tex_tclamp;
        r->tex.minfilter = st.tex_minfilter;
        r->tex.magfilter = st.tex_magfilter;
        r->tex.gl_dirty = 1;
    }
    if (((r->tex.format == GR_TEXFMT_P_8) || (r->tex.format == GR_TEXFMT_AP_88)) && (r->tex.decoded_palette != palette_serial))
    {
        decode_texture(&r->tex);
    }
    return &r->tex;
}

static struct glide_texture *current_texture(void)
{
    struct glide_texture *t;

    if (st.tex_raw) return raw_texture();

    if (st.texture >= num_textures) return NULL;
    t = &textures[st.texture];
    if (!t->used || !t->downloaded) return NULL;
    if (((t->format == GR_TEXFMT_P_8) || (t->format == GR_TEXFMT_AP_88)) && (t->decoded_palette != palette_serial))
    {
        decode_texture(t);
    }
    return t;
}

static void free_raw_textures(void);

static void free_textures(void)
{
    uint32_t i;

    free_raw_textures();
    gl_acquire();
    for (i = 0; i < num_textures; i++)
    {
        free(textures[i].raw);
        free(textures[i].argb);
        if (gl_ok && textures[i].gltex != 0) glDeleteTextures(1, &textures[i].gltex);
    }
    memset(textures, 0, sizeof(textures));
    num_textures = 0;
}

/* ---- OpenGL ---- */

/* what the shaders get of a vertex: the position in pixels, the colour in 0..1,
 * sow, tow, oow (screen-linear, as a Voodoo interpolates them), and bs0..bt1 the box
 * of the texture coordinates (s/w, t/w) of the vertex's triangle */
struct gl_vertex {
    float x, y, z;
    float r, g, b, a;
    float sow, tow, oow;
    float bs0, bt0, bs1, bt1;
};

#define BATCH_MAX 3072

static GLuint fbo[3], fbo_tex[3];
static GLuint depth_rb, ms_depth_rb;    /* the depth buffer: one for the colour buffers, as the board has */
static GLuint prog_draw;
/* the buffers are fb_width x fb_height, sx x sy pixels of them a pixel of the
 * screen; hires when that is not the screen's size */
static int hires;
static float sx = 1.0f, sy = 1.0f;
static uint32_t fb_width, fb_height;
static GLuint fbo_lo, fbo_lo_tex;   /* hires: a buffer at the size of the screen, for a lock */
static GLuint lfb_tex;              /* hires: the pixels the guest changed */
static uint16_t *base_mem[3];       /* hires: the shadow as GL has it (stride scr_width) */
static int ms_samples;              /* antialiasing: the samples per pixel, 0 without */
static GLuint ms_fbo[3], ms_color[3];
static int ms_dirty[3];             /* the multisampled image is newer than fbo[] */
static GLint u_psize, u_inv_size, u_tex, u_texscale, u_cc, u_asrc, u_usetex, u_ccolor, u_chroma, u_chroma_on;
static GLint u_cc_raw, u_cc_fn, u_cc_fac, u_cc_loc, u_cc_oth, u_cc_inv, u_ac_raw, u_ac_fn, u_ac_fac, u_ac_loc, u_ac_oth, u_ac_inv;
static GLint u_atest, u_atest_ref, u_fog, u_fogcolor, u_fogtab, u_lodbias, u_clampuv;
static GLint a_box = -1;
/* the 565 shadow of a colour buffer is what the guest sees; the GL buffer is what is drawn */
static int shadow_valid[3];
static int shadow_dirty[3];         /* the guest wrote the shadow: the GL image is stale */
static int bidx[3];                 /* logical buffer -> colour_mem index */
static uint32_t *xfer;              /* scr_width * scr_height ARGB, for LFB transfers */

static struct gl_vertex batch[BATCH_MAX];
static uint32_t batch_n;
static GLenum batch_mode;

static const char draw_vs[] =
    "uniform vec2 inv_size;\n"
    "uniform float u_psize;\n"
    "attribute vec3 a_pos;\n"
    "attribute vec4 a_col;\n"
    "attribute vec3 a_stq;\n"
    "attribute vec4 a_box;\n"
    "varying vec4 v_col;\n"
    "varying vec3 v_stq;\n"
    "varying vec4 v_box;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.xy * inv_size - 1.0, a_pos.z * 2.0 - 1.0, 1.0);\n"
    "    gl_PointSize = u_psize;\n"
    "    v_col = a_col;\n"
    "    v_stq = a_stq;\n"
    "    v_box = a_box;\n"
    "}\n";

/* the colour/alpha combine of glide2x.h, the numbers are GR_COLORCOMBINE_*; u_clampuv: the texture
 * coordinates are kept in the box of the triangle's (with antialiasing a pixel on an edge is shaded at
 * its centre, which may lie outside the triangle: the coordinates there would reach the neighbouring
 * picture of a texture page, as in WinApi-d3dgl.c) */
static const char draw_fs[] =
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texscale;\n"
    "uniform int u_cc;\n"
    "uniform int u_asrc;\n"
    "uniform int u_usetex;\n"
    "uniform vec4 u_ccolor;\n"
    "uniform vec3 u_chroma;\n"
    "uniform int u_chroma_on;\n"
    "uniform int u_cc_raw, u_cc_fn, u_cc_fac, u_cc_loc, u_cc_oth, u_cc_inv;\n"
    "uniform int u_ac_raw, u_ac_fn, u_ac_fac, u_ac_loc, u_ac_oth, u_ac_inv;\n"
    "uniform int u_atest;\n"
    "uniform int u_fog;\n"
    "uniform vec3 u_fogcolor;\n"
    "uniform float u_fogtab[64];\n"
    "uniform float u_lodbias;\n"
    "uniform float u_atest_ref;\n"
    "uniform int u_clampuv;\n"
    "varying vec4 v_col;\n"
    "varying vec3 v_stq;\n"
    "varying vec4 v_box;\n"
    "/* grColorCombine / grAlphaCombine of glide2x.h: function (ZERO 0, LOCAL 1, LOCAL_ALPHA 2, SCALE_OTHER 3,\n"
    " * SCALE_OTHER_ADD_LOCAL 4, ..._ADD_LOCAL_ALPHA 5, SCALE_OTHER_MINUS_LOCAL 6, ..._ADD_LOCAL 7, ..._ADD_LOCAL_ALPHA 8,\n"
    " * SCALE_MINUS_LOCAL_ADD_LOCAL 9, ..._ADD_LOCAL_ALPHA 10), factor (ZERO 0, LOCAL 1, OTHER_ALPHA 2, LOCAL_ALPHA 3,\n"
    " * TEXTURE_ALPHA 4, TEXTURE_RGB 5, ONE 8, and 8 + n for one minus n), local (ITERATED 0, CONSTANT 1, DEPTH 2),\n"
    " * other (ITERATED 0, TEXTURE 1, CONSTANT 2), invert */\n"
    "float comb_factor(int fac, vec4 loc, vec4 oth, vec4 tex) {\n"
    "    int n = fac - (fac >= 8 ? 8 : 0);\n"
    "    float f = (n == 1) ? loc.a : (n == 2) ? oth.a : (n == 3) ? loc.a : (n == 4) ? tex.a : 0.0;\n"
    "    return (fac >= 8) ? 1.0 - f : f;\n"
    "}\n"
    "float comb_factor_rgb(int fac, vec4 loc, vec4 oth, vec4 tex, int c) {\n"
    "    if (fac == 1 || fac == 9) { float l = (c == 0) ? loc.r : (c == 1) ? loc.g : loc.b; return (fac == 9) ? 1.0 - l : l; }\n"
    "    if (fac == 5 || fac == 13) { float t = (c == 0) ? tex.r : (c == 1) ? tex.g : tex.b; return (fac == 13) ? 1.0 - t : t; }\n"
    "    return comb_factor(fac, loc, oth, tex);\n"
    "}\n"
    "float comb_apply(int fn, float f, float loc, float oth, float loca) {\n"
    "    if (fn == 0) return 0.0;\n"
    "    if (fn == 1) return loc;\n"
    "    if (fn == 2) return loca;\n"
    "    if (fn == 3) return f * oth;\n"
    "    if (fn == 4) return f * oth + loc;\n"
    "    if (fn == 5) return f * oth + loca;\n"
    "    if (fn == 6) return f * (oth - loc);\n"
    "    if (fn == 7) return f * (oth - loc) + loc;\n"
    "    if (fn == 8) return f * (oth - loc) + loca;\n"
    "    if (fn == 9) return loc - f * loc;\n"
    "    return loca - f * loc;\n"
    "}\n"
    "vec4 comb_local(vec4 col, int loc) { return (loc == 0) ? col : (loc == 1) ? u_ccolor : vec4(0.0); }\n"
    "vec4 comb_other(vec4 col, vec4 tex, int oth) { return (oth == 0) ? col : (oth == 1) ? tex : u_ccolor; }\n"
    "vec3 combine3(vec4 col, vec4 tex, int fn, int fac, int locsel, int othsel, int inv) {\n"
    "    vec4 loc = comb_local(col, locsel);\n"
    "    vec4 oth = comb_other(col, tex, othsel);\n"
    "    vec3 r;\n"
    "    r.r = comb_apply(fn, comb_factor_rgb(fac, loc, oth, tex, 0), loc.r, oth.r, loc.a);\n"
    "    r.g = comb_apply(fn, comb_factor_rgb(fac, loc, oth, tex, 1), loc.g, oth.g, loc.a);\n"
    "    r.b = comb_apply(fn, comb_factor_rgb(fac, loc, oth, tex, 2), loc.b, oth.b, loc.a);\n"
    "    r = clamp(r, 0.0, 1.0);\n"
    "    return (inv != 0) ? 1.0 - r : r;\n"
    "}\n"
    "float combine1(vec4 col, vec4 tex, int fn, int fac, int locsel, int othsel, int inv) {\n"
    "    vec4 loc = comb_local(col, locsel);\n"
    "    vec4 oth = comb_other(col, tex, othsel);\n"
    "    float r = clamp(comb_apply(fn, comb_factor(fac, loc, oth, tex), loc.a, oth.a, loc.a), 0.0, 1.0);\n"
    "    return (inv != 0) ? 1.0 - r : r;\n"
    "}\n"
    "void main() {\n"
    "    vec4 col = clamp(v_col, 0.0, 1.0);\n"
    "    vec4 tex = vec4(1.0);\n"
    "    if (u_usetex == 1) {\n"
    "        float w = (v_stq.z != 0.0) ? 1.0 / v_stq.z : 0.0;\n"
    "        vec2 st = v_stq.xy * w;\n"
    "        if (u_clampuv != 0) st = clamp(st, v_box.xy, v_box.zw);\n"
    "        tex = texture2D(u_tex, st * u_texscale, u_lodbias);\n"
    "    } else if (u_usetex == 2) {\n"
    "        tex = vec4(0.0);\n"
    "    }\n"
    "    vec3 other;\n"
    "    if (u_cc_raw != 0) {\n"
    "        other = (u_cc_oth == 1) ? tex.rgb : ((u_cc_oth == 0) ? col.rgb : u_ccolor.rgb);\n"
    "    } else if (u_cc == 1 || u_cc == 13) other = u_ccolor.rgb;\n"
    "    else if (u_cc == 0 || u_cc == 2 || u_cc == 3 || u_cc == 16) other = col.rgb;\n"
    "    else other = tex.rgb;\n"
    "    if (u_chroma_on != 0 && all(lessThan(abs(other - u_chroma), vec3(0.5 / 255.0)))) discard;\n"
    "    vec3 rgb;\n"
    "    if (u_cc_raw != 0) rgb = combine3(col, tex, u_cc_fn, u_cc_fac, u_cc_loc, u_cc_oth, u_cc_inv);\n"
    "    else if (u_cc == 0) rgb = vec3(0.0);\n"
    "    else if (u_cc == 1) rgb = u_ccolor.rgb;\n"
    "    else if (u_cc == 2 || u_cc == 3) rgb = col.rgb;\n"
    "    else if (u_cc == 4) rgb = tex.rgb;\n"
    "    else if (u_cc == 5) rgb = tex.rgb * u_ccolor.rgb;\n"
    "    else if (u_cc == 6 || u_cc == 7 || u_cc == 15) rgb = tex.rgb * col.rgb;\n"
    "    else if (u_cc == 8 || u_cc == 14) rgb = tex.rgb * col.rgb + col.a;\n"
    "    else if (u_cc == 9) rgb = tex.rgb * col.a;\n"
    "    else if (u_cc == 10) rgb = tex.rgb * col.a + col.rgb;\n"
    "    else if (u_cc == 11) rgb = tex.rgb + col.rgb;\n"
    "    else if (u_cc == 12) rgb = tex.rgb - col.rgb;\n"
    "    else if (u_cc == 13) rgb = col.rgb + (u_ccolor.rgb - col.rgb) * tex.a;\n"
    "    else if (u_cc == 16) rgb = vec3(1.0);\n"
    "    else rgb = col.rgb;\n"
    "    float a;\n"
    "    if (u_ac_raw != 0) a = combine1(col, tex, u_ac_fn, u_ac_fac, u_ac_loc, u_ac_oth, u_ac_inv);\n"
    "    else if (u_asrc == 0) a = u_ccolor.a;\n"
    "    else if (u_asrc == 2) a = tex.a;\n"
    "    else if (u_asrc == 3) a = tex.a * col.a;\n"
    "    else a = col.a;\n"
    "    if (u_fog == 1) {\n"
    "        /* index i of the table is the W of guFogTableIndexToW, 2^(3 + i/4) / (8 - i%4): in the\n"
    "         * octave k (W 2^k up to 2^(k+1)) the index is 4k + 8 - 2^(3+k) / W */\n"
    "        float w = (v_stq.z > 0.0) ? max(1.0 / v_stq.z, 1.0) : 1.0e9;\n"
    "        float k = floor(log2(w));\n"
    "        float x = clamp(4.0 * k + 8.0 - exp2(3.0 + k) / w, 0.0, 63.0);\n"
    "        int i0 = int(floor(x));\n"
    "        float f0 = u_fogtab[i0];\n"
    "        float f1 = u_fogtab[(i0 < 63) ? i0 + 1 : 63];\n"
    "        rgb = mix(rgb, u_fogcolor, mix(f0, f1, x - float(i0)));\n"
    "    } else if (u_fog == 2) {\n"
    "        rgb = mix(rgb, u_fogcolor, col.a);\n"
    "    }\n"
    "    if (u_atest != 7) {\n"
    "        float ref = u_atest_ref;\n"
    "        float av = floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5);\n"
    "        bool pass = (u_atest == 1) ? av < ref : (u_atest == 2) ? av == ref : (u_atest == 3) ? av <= ref\n"
    "                  : (u_atest == 4) ? av > ref : (u_atest == 5) ? av != ref : (u_atest == 6) ? av >= ref : false;\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), a);\n"
    "}\n";

/* Display_3D_Resolution, <GAME>_3D_RESOLUTION over it (WxH); 0 x 0 when not set */
static void resolution_3d(uint32_t *width, uint32_t *height)
{
    const char *e = getenv(GAME_ENV("3D_RESOLUTION"));
    unsigned int w = 0, h = 0;

    if (e != NULL)
    {
        if (sscanf(e, "%ux%u", &w, &h) != 2) w = h = 0;
    }
    else if (Game_3DWidth > 0 && Game_3DHeight > 0)
    {
        w = (unsigned int)Game_3DWidth;
        h = (unsigned int)Game_3DHeight;
    }
    *width = w;
    *height = h;
}

/* the item of the port's panel: off and the 4:3 sizes, the next one at a press
 * (from a size not in the list, the next bigger); grey, with its value, while
 * <GAME>_3D_RESOLUTION holds the size. The buffers change at the next grBufferSwap */
static const uint16_t sizes_3d[][2] = { { 640, 480 }, { 960, 720 }, { 1280, 960 }, { 1600, 1200 }, { 1920, 1440 } };
#define SIZES_3D (int)(sizeof(sizes_3d) / sizeof(sizes_3d[0]))

static int resolution_item_active(void)
{
    return getenv(GAME_ENV("3D_RESOLUTION")) == NULL;
}

static void resolution_item_press(void)
{
    int i;

    for (i = 0; i < SIZES_3D; i++)
    {
        if (sizes_3d[i][0] > Game_3DWidth) break;
    }
    if (Game_3DWidth <= 0 || Game_3DHeight <= 0) i = 0;
    if (i < SIZES_3D)
    {
        Game_3DHeight = sizes_3d[i][1];
        Game_3DWidth = sizes_3d[i][0];
    }
    else
    {
        Game_3DWidth = Game_3DHeight = 0;
    }
}

static const char *resolution_item_state(void)
{
    static char name[24];
    const char *e = getenv(GAME_ENV("3D_RESOLUTION"));

    if (e != NULL) return e;
    if (Game_3DWidth <= 0 || Game_3DHeight <= 0) return "off";
    snprintf(name, sizeof(name), "%dx%d", Game_3DWidth, Game_3DHeight);
    return name;
}

static const Game_OsdItem resolution_item = { "3D resolution", resolution_item_active, resolution_item_press, resolution_item_state, "Display_3D_Resolution" };

void Glide_OsdItems(void)
{
    if (GAME_GLIDE_3D_RESOLUTION_ITEM) Game_OsdAddItem(&resolution_item);
}

static void set_scissor(void)
{
    glEnable(GL_SCISSOR_TEST);
    int x0 = (int)lroundf(st.clip_minx * sx), y0 = (int)lroundf(st.clip_miny * sy);
    int x1 = (int)lroundf(st.clip_maxx * sx), y1 = (int)lroundf(st.clip_maxy * sy);

    glScissor(x0, y0, x1 - x0, y1 - y0);
}

/* the frame buffer a colour buffer is drawn into */
static GLuint draw_fbo(int pi)
{
    if (ms_samples == 0) return fbo[pi];
    ms_dirty[pi] = 1;
    return ms_fbo[pi];
}

/* antialiasing: the multisampled image into fbo[pi], for what reads it */
static void ms_resolve(int pi)
{
    if (ms_samples == 0 || !ms_dirty[pi]) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo[pi]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[pi]);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, fb_width, fb_height, 0, 0, fb_width, fb_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ms_dirty[pi] = 0;
}

/* the multisampled buffers of the colour buffers, up to the driver's samples;
 * none (and ms_samples 0) if it makes none */
static void ms_create(uint32_t nbuf)
{
    GLint max = 0;
    int n = Winapi_Direct3DAntialiasing();
    uint32_t i;

    glGetIntegerv(GL_MAX_SAMPLES, &max);
    if (n > max) n = max;
    if (n < 2) return;
    for (i = 0; i < nbuf; i++)
    {
        glGenFramebuffers(1, &ms_fbo[i]);
        glGenRenderbuffers(1, &ms_color[i]);
        glBindRenderbuffer(GL_RENDERBUFFER, ms_color[i]);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_RGBA8, fb_width, fb_height);
        glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo[i]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ms_color[i]);
        if (ms_depth_rb == 0)
        {
            glGenRenderbuffers(1, &ms_depth_rb);
            glBindRenderbuffer(GL_RENDERBUFFER, ms_depth_rb);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH_COMPONENT24, fb_width, fb_height);
        }
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ms_depth_rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            eprintf("Glide: no multisampled frame buffer (%d samples)\n", n);
            return;
        }
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ms_dirty[i] = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ms_samples = n;
    eprintf("Glide: antialiasing, %d samples\n", n);
}

static void ms_delete(void)
{
    uint32_t i;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (i = 0; i < 3; i++)
    {
        if (ms_fbo[i] != 0) glDeleteFramebuffers(1, &ms_fbo[i]);
        if (ms_color[i] != 0) glDeleteRenderbuffers(1, &ms_color[i]);
        ms_fbo[i] = ms_color[i] = 0;
        ms_dirty[i] = 0;
    }
    if (ms_depth_rb != 0) glDeleteRenderbuffers(1, &ms_depth_rb);
    ms_depth_rb = 0;
    ms_samples = 0;
}

/* the size of the colour buffers: Display_3D_Resolution, else the screen's;
 * the screen's too if OpenGL makes no texture that big */
static uint32_t gl_max_size;

static void fb_target(uint32_t *width, uint32_t *height, int log)
{
    resolution_3d(width, height);
    if (*width == 0 || *height == 0)
    {
        *width = scr_width;
        *height = scr_height;
    }
    if (*width > gl_max_size || *height > gl_max_size)
    {
        if (log) eprintf("Glide: %ux%u is more than OpenGL can make (%u)\n", *width, *height, gl_max_size);
        *width = scr_width;
        *height = scr_height;
    }
}

/* the colour buffers (and their multisampled ones), the depth buffer and, when
 * they are bigger than the screen, the buffers of a lock: fb_width x fb_height */
static int buffers_create(uint32_t nbuf)
{
    uint32_t i;

    hires = (fb_width != scr_width || fb_height != scr_height);
    sx = (float)fb_width / (float)scr_width;
    sy = (float)fb_height / (float)scr_height;
    if (hires)
    {
        eprintf("Glide: the 3D at %ux%u\n", fb_width, fb_height);
        if (!Game_GlFboCreate(&fbo_lo, &fbo_lo_tex, scr_width, scr_height, 0, NULL)) return 0;
        glGenTextures(1, &lfb_tex);
        glBindTexture(GL_TEXTURE_2D, lfb_tex);
        Game_GlTexImageBgra(0, scr_width, scr_height, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    for (i = 0; i < nbuf; i++)
    {
        if (!Game_GlFboCreate(&fbo[i], &fbo_tex[i], fb_width, fb_height, 0, NULL)) return 0;
        if (hires)
        {
            /* the colour buffers start black, as their shadows */
            base_mem[i] = (uint16_t *)calloc(scr_width * scr_height, 2);
            if (base_mem[i] == NULL) return 0;
        }
        shadow_valid[i] = 1;
        shadow_dirty[i] = 0;
    }
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, fb_width, fb_height);
    for (i = 0; i < nbuf; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    }
    ms_create(nbuf);
    if (ms_samples == 0) ms_delete();
    return 1;
}

/* the names of the buffers, once deleted (or lost with the context) */
static void buffers_forget(void)
{
    uint32_t i;

    memset(fbo, 0, sizeof(fbo));
    memset(fbo_tex, 0, sizeof(fbo_tex));
    fbo_lo = fbo_lo_tex = lfb_tex = 0;
    depth_rb = ms_depth_rb = 0;
    for (i = 0; i < 3; i++)
    {
        free(base_mem[i]);
        base_mem[i] = NULL;
    }
    hires = 0;
    sx = sy = 1.0f;
}

static void buffers_delete(void)
{
    uint32_t i;

    ms_delete();
    for (i = 0; i < 3; i++)
    {
        if (fbo[i] != 0) Game_GlFboDelete(&fbo[i], &fbo_tex[i], 0, NULL);
    }
    if (fbo_lo != 0) Game_GlFboDelete(&fbo_lo, &fbo_lo_tex, 0, NULL);
    if (depth_rb != 0) glDeleteRenderbuffers(1, &depth_rb);
    if (ms_depth_rb != 0) glDeleteRenderbuffers(1, &ms_depth_rb);
    if (lfb_tex != 0) glDeleteTextures(1, &lfb_tex);
    buffers_forget();
}

static int gl_start(uint32_t nbuf)
{
    GLint max_size = 0;

    /* the window 4:3 */
    gl_window = Winapi_ScreenOpenGL(scr_width, scr_width * 3 / 4);
    if (gl_window == NULL) return 0;
    gl_context = Game_GlCreateContext(gl_window, Display_VSync);
    if (gl_context == NULL) goto fail;
    DEBUG("Glide: OpenGL %s, %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    prog_draw = Game_GlProgram(draw_vs, draw_fs, "a_pos", "a_col", "a_stq", "Glide");
    if (prog_draw == 0) goto fail;
    u_inv_size = glGetUniformLocation(prog_draw, "inv_size");
    u_psize = glGetUniformLocation(prog_draw, "u_psize");
    u_tex = glGetUniformLocation(prog_draw, "u_tex");
    u_texscale = glGetUniformLocation(prog_draw, "u_texscale");
    u_cc = glGetUniformLocation(prog_draw, "u_cc");
    u_asrc = glGetUniformLocation(prog_draw, "u_asrc");
    u_usetex = glGetUniformLocation(prog_draw, "u_usetex");
    u_ccolor = glGetUniformLocation(prog_draw, "u_ccolor");
    u_chroma = glGetUniformLocation(prog_draw, "u_chroma");
    u_chroma_on = glGetUniformLocation(prog_draw, "u_chroma_on");
    u_cc_raw = glGetUniformLocation(prog_draw, "u_cc_raw");
    u_cc_fn = glGetUniformLocation(prog_draw, "u_cc_fn");
    u_cc_fac = glGetUniformLocation(prog_draw, "u_cc_fac");
    u_cc_loc = glGetUniformLocation(prog_draw, "u_cc_loc");
    u_cc_oth = glGetUniformLocation(prog_draw, "u_cc_oth");
    u_cc_inv = glGetUniformLocation(prog_draw, "u_cc_inv");
    u_ac_raw = glGetUniformLocation(prog_draw, "u_ac_raw");
    u_ac_fn = glGetUniformLocation(prog_draw, "u_ac_fn");
    u_ac_fac = glGetUniformLocation(prog_draw, "u_ac_fac");
    u_ac_loc = glGetUniformLocation(prog_draw, "u_ac_loc");
    u_ac_oth = glGetUniformLocation(prog_draw, "u_ac_oth");
    u_ac_inv = glGetUniformLocation(prog_draw, "u_ac_inv");
    u_atest = glGetUniformLocation(prog_draw, "u_atest");
    u_atest_ref = glGetUniformLocation(prog_draw, "u_atest_ref");
    u_fog = glGetUniformLocation(prog_draw, "u_fog");
    u_fogcolor = glGetUniformLocation(prog_draw, "u_fogcolor");
    u_fogtab = glGetUniformLocation(prog_draw, "u_fogtab");
    u_lodbias = glGetUniformLocation(prog_draw, "u_lodbias");
    u_clampuv = glGetUniformLocation(prog_draw, "u_clampuv");
    a_box = glGetAttribLocation(prog_draw, "a_box");

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    gl_max_size = (uint32_t)max_size;
    fb_target(&fb_width, &fb_height, 1);
    if (!buffers_create(nbuf)) goto fail;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
#if !defined(GAME_GLES)
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
#endif
    gl_ok = 1;
    return 1;

fail:
    return 0;
}

static void gl_stop(void)
{
    uint32_t i;

    if (gl_context != NULL)
    {
        buffers_delete();
        for (i = 0; i < num_textures; i++)
        {
            if (textures[i].gltex != 0) glDeleteTextures(1, &textures[i].gltex);
            textures[i].gltex = 0;
        }
        if (prog_draw != 0) glDeleteProgram(prog_draw);
        SDL_GL_DeleteContext(gl_context);
    }
    buffers_forget();
    prog_draw = 0;
    gl_context = NULL;
    gl_ok = 0;
    if (gl_window != NULL) Winapi_ScreenCloseGL();
    gl_window = NULL;
    batch_n = 0;
}

static int phys_of(uint32_t which)
{
    return (which == GR_BUFFER_FRONTBUFFER || which == GR_BUFFER_BACKBUFFER) ? bidx[which] : -1;
}

static GLenum blend_gl(uint32_t f, int is_src)
{
    switch (f)
    {
        case GR_BLEND_ZERO: return GL_ZERO;
        case GR_BLEND_SRC_ALPHA: return GL_SRC_ALPHA;
        case GR_BLEND_SRC_COLOR: return is_src ? GL_DST_COLOR : GL_SRC_COLOR;       /* GR_BLEND_DST_COLOR */
        case GR_BLEND_DST_ALPHA: return GL_ONE;                                     /* a 565 buffer has no alpha: 1 */
        case GR_BLEND_ONE: return GL_ONE;
        case GR_BLEND_ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case GR_BLEND_ONE_MINUS_SRC_COLOR: return is_src ? GL_ONE_MINUS_DST_COLOR : GL_ONE_MINUS_SRC_COLOR;
        case GR_BLEND_ONE_MINUS_DST_ALPHA: return GL_ZERO;
        case GR_BLEND_ALPHA_SATURATE: return is_src ? GL_ZERO : GL_ONE;             /* min(a, 1 - dst alpha) = 0; GR_BLEND_PREFOG_COLOR */
        default: return is_src ? GL_ONE : GL_ZERO;
    }
}

/* the mipmap mode of a texture: of grTexMipMapMode for grTexSource, of guTexAllocateMemory for guTexSource */
static uint32_t mipmap_mode_of(const struct glide_texture *t)
{
    return st.tex_raw ? st.mipmap_mode : t->mm_mode;
}

/* the minification of a texture: one LOD, or the nearest LOD; the dither between two LODs as their blend */
static GLint min_filter_gl(const struct glide_texture *t)
{
    int bilinear = (t->minfilter == GR_TEXTUREFILTER_BILINEAR);
    uint32_t mode = mipmap_mode_of(t);

    if (mode == GR_MIPMAP_DISABLE || t->smallest_lod <= t->largest_lod) return bilinear ? GL_LINEAR : GL_NEAREST;
    if (mode == GR_MIPMAP_NEAREST_DITHER) return bilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    return bilinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
}

static void upload_texture(struct glide_texture *t)
{
    uint32_t levels = t->smallest_lod - t->largest_lod + 1, i;
    const uint32_t *p = t->argb;

    if (t->gltex == 0) glGenTextures(1, &t->gltex);
    glBindTexture(GL_TEXTURE_2D, t->gltex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (t->s_clamp == GR_TEXTURECLAMP_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (t->t_clamp == GR_TEXTURECLAMP_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    t->gl_min = min_filter_gl(t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, t->gl_min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (t->magfilter == GR_TEXTUREFILTER_BILINEAR) ? GL_LINEAR : GL_NEAREST);
    /* the LODs of the texture are the levels; the smallest may be larger than 1x1 */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)levels - 1);
    for (i = 0; i < levels; i++)
    {
        uint32_t lw = t->width >> i, lh = t->height >> i;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        Game_GlTexImageBgra((GLint)i, lw, lh, p);
        p += lw * lh;
    }
    t->gl_dirty = 0;
}

/* draw the batch with the state that holds now */
static void gl_flush(void)
{
    int pi;
    uint32_t cc = st.color_combine, ccolor;
    int use_texture;
    struct glide_texture *tex = NULL;

    if (batch_n == 0) return;
    gl_acquire();
    pi = gl_ok ? phys_of(st.render_buffer) : -1;
    if (pi < 0)
    {
        batch_n = 0;
        return;
    }

    /* does the pixel come from the texture: the combine of the colour and that of the alpha, each as the
     * program gave it (grColorCombine, grAlphaCombine) or as guColorCombineFunction/guAlphaSource say */
    use_texture = st.cc_raw ? (st.cc_other == 1 || (st.cc_factor & 7) == 4 || (st.cc_factor & 7) == 5)
                            : (cc >= GR_COLORCOMBINE_DECAL_TEXTURE && cc <= GR_COLORCOMBINE_DIFF_SPEC_B);
    use_texture = use_texture
                  || (st.ac_raw ? (st.ac_other == 1 || (st.ac_factor & 7) == 4)
                                : (st.alpha_source == GR_ALPHASOURCE_TEXTURE_ALPHA
                                   || st.alpha_source == GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA));
    if (use_texture && st.tex_combine != GR_TEXTURECOMBINE_ZERO && st.tex_combine != GR_TEXTURECOMBINE_ONE)
    {
        tex = current_texture();
    }

    if (shadow_dirty[pi]) shadow_to_gl(pi);
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo(pi));
    glViewport(0, 0, fb_width, fb_height);
    set_scissor();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    if (st.rgb_sf == GR_BLEND_ONE && st.rgb_df == GR_BLEND_ZERO)
    {
        glDisable(GL_BLEND);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendFunc(blend_gl(st.rgb_sf, 1), blend_gl(st.rgb_df, 0));
    }

    if (st.depth_mode != GR_DEPTHBUFFER_DISABLE)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_NEVER + (st.depth_function & 7));        /* GR_CMP_* run as GL_NEVER.. */
        glDepthMask(st.depth_mask ? GL_TRUE : GL_FALSE);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    glUseProgram(prog_draw);
    glUniform2f(u_inv_size, 2.0f / (float)scr_width, 2.0f / (float)scr_height);
    glUniform1f(u_psize, sx);
    if (batch_mode == GL_LINES) glLineWidth(sx);
    glUniform1i(u_cc, (int)cc);
    glUniform1i(u_asrc, (int)st.alpha_source);
    glUniform1i(u_cc_raw, (int)st.cc_raw);
    glUniform1i(u_cc_fn, (int)st.cc_function);
    glUniform1i(u_cc_fac, (int)st.cc_factor);
    glUniform1i(u_cc_loc, (int)st.cc_local);
    glUniform1i(u_cc_oth, (int)st.cc_other);
    glUniform1i(u_cc_inv, (int)st.cc_invert);
    glUniform1i(u_ac_raw, (int)st.ac_raw);
    glUniform1i(u_ac_fn, (int)st.ac_function);
    glUniform1i(u_ac_fac, (int)st.ac_factor);
    glUniform1i(u_ac_loc, (int)st.ac_local);
    glUniform1i(u_ac_oth, (int)st.ac_other);
    glUniform1i(u_ac_inv, (int)st.ac_invert);
    glUniform1i(u_atest, (int)st.alpha_test_function);
    glUniform1i(u_fog, GAME_GLIDE_FOG_HIDDEN ? 0 : (int)st.fog_mode);
    if (st.fog_mode != 0)
    {
        uint32_t fc = color_to_argb(st.fog_color);
        glUniform3f(u_fogcolor, ((fc >> 16) & 0xff) / 255.0f, ((fc >> 8) & 0xff) / 255.0f, (fc & 0xff) / 255.0f);
        glUniform1fv(u_fogtab, 64, fog_table);
    }
    glUniform1f(u_atest_ref, (float)st.alpha_test_reference);
    ccolor = color_to_argb(st.constant_color);
    glUniform4f(u_ccolor, ((ccolor >> 16) & 0xff) / 255.0f, ((ccolor >> 8) & 0xff) / 255.0f, (ccolor & 0xff) / 255.0f, (ccolor >> 24) / 255.0f);
    if (st.chromakey_mode == GR_CHROMAKEY_ENABLE)
    {
        uint32_t k = color_to_argb(st.chromakey_value);
        glUniform3f(u_chroma, ((k >> 16) & 0xff) / 255.0f, ((k >> 8) & 0xff) / 255.0f, (k & 0xff) / 255.0f);
        glUniform1i(u_chroma_on, 1);
    }
    else
    {
        glUniform1i(u_chroma_on, 0);
    }
    if (!use_texture)
    {
        glUniform1i(u_usetex, 0);
    }
    else if (tex != NULL)
    {
        uint32_t big = (tex->width > tex->height) ? tex->width : tex->height;
        float scale = (float)big / 256.0f;
        glActiveTexture(GL_TEXTURE0);
        if (tex->gltex == 0 || tex->gl_dirty) upload_texture(tex);
        glBindTexture(GL_TEXTURE_2D, tex->gltex);
        if (tex->gl_min != min_filter_gl(tex))
        {
            /* grTexMipMapMode changed since the upload */
            tex->gl_min = min_filter_gl(tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex->gl_min);
        }
        glUniform1f(u_lodbias, st.tex_raw ? st.lod_bias : tex->lod_bias);
        glUniform1i(u_tex, 0);
        glUniform2f(u_texscale, scale / (float)tex->width, scale / (float)tex->height);
        glUniform1i(u_usetex, 1);
    }
    else
    {
        glUniform1i(u_usetex, (st.tex_combine == GR_TEXTURECOMBINE_ZERO) ? 2 : 0);
    }
    glUniform1i(u_clampuv, ms_samples != 0 && batch_mode == GL_TRIANGLES);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].x);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].r);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].sow);
    if (a_box >= 0)
    {
        glEnableVertexAttribArray((GLuint)a_box);
        glVertexAttribPointer((GLuint)a_box, 4, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].bs0);
    }
    glDrawArrays(batch_mode, 0, batch_n);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    if (a_box >= 0) glDisableVertexAttribArray((GLuint)a_box);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    shadow_valid[pi] = 0;
    batch_n = 0;
    if (st.render_buffer == GR_BUFFER_FRONTBUFFER) front_drawn();
}

/* the depth of a vertex in 0..1 (0 the nearest): ooz of the vertex in the Z buffer's mode, 1/oow in the W buffer's,
 * the bias alone when the depth is compared to it */
static float vertex_depth(const struct gr_vertex *v)
{
    float d;

    switch (st.depth_mode)
    {
        case GR_DEPTHBUFFER_ZBUFFER: d = v->ooz / 65535.0f + (float)st.depth_bias / 65535.0f; break;
        case GR_DEPTHBUFFER_WBUFFER: d = (v->oow > 0.0f ? 1.0f / v->oow : 65535.0f) / 65535.0f + (float)st.depth_bias / 65535.0f; break;
        case 3: case 4: d = (float)st.depth_bias / 65535.0f; break;
        default: d = 0.0f; break;
    }
    return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
}

static void batch_vertex(GLenum mode, const struct gr_vertex *v, int pixel_centre)
{
    struct gl_vertex *o;

    if (batch_n > 0 && (batch_mode != mode || batch_n + 3 > BATCH_MAX)) gl_flush();
    batch_mode = mode;
    o = &batch[batch_n++];
    o->x = pixel_centre ? floorf(v->x) + 0.5f : v->x;
    o->y = pixel_centre ? floorf(v->y) + 0.5f : v->y;
    o->z = vertex_depth(v);
    o->r = v->r / 255.0f;
    o->g = v->g / 255.0f;
    o->b = v->b / 255.0f;
    o->a = v->a / 255.0f;
    o->sow = v->tmuvtx[0].sow;
    o->tow = v->tmuvtx[0].tow;
    o->oow = v->oow;
    o->bs0 = o->bt0 = -1e30f;
    o->bs1 = o->bt1 = 1e30f;
}

/* the box of the last three vertices' texture coordinates (a triangle); none if a w is not positive */
static void batch_box_last3(void)
{
    struct gl_vertex *v = &batch[batch_n - 3];
    float s[3], t[3];
    int i;

    for (i = 0; i < 3; i++)
    {
        if (!(v[i].oow > 0.0f)) return;
        s[i] = v[i].sow / v[i].oow;
        t[i] = v[i].tow / v[i].oow;
    }
    for (i = 0; i < 3; i++)
    {
        v[i].bs0 = fminf(s[0], fminf(s[1], s[2]));
        v[i].bt0 = fminf(t[0], fminf(t[1], t[2]));
        v[i].bs1 = fmaxf(s[0], fmaxf(s[1], s[2]));
        v[i].bt1 = fmaxf(t[0], fmaxf(t[1], t[2]));
    }
}

static void draw_triangle(const struct gr_vertex *a, const struct gr_vertex *b, const struct gr_vertex *c)
{
    float area;

    if (!win_open || !gl_ok) return;

    /* the culling of glide2x: the sign of (a - b) x (b - c) */
    area = (a->x - b->x) * (b->y - c->y) - (b->x - c->x) * (a->y - b->y);
    if (st.cull_mode == GR_CULL_NEGATIVE && area < 0.0f) return;
    if (st.cull_mode == GR_CULL_POSITIVE && area > 0.0f) return;
    if (area == 0.0f || area != area) return;

    batch_vertex(GL_TRIANGLES, a, 0);
    batch_vertex(GL_TRIANGLES, b, 0);
    batch_vertex(GL_TRIANGLES, c, 0);
    batch_box_last3();
}

static void draw_point(const struct gr_vertex *v)
{
    if (!win_open || !gl_ok) return;
    batch_vertex(GL_POINTS, v, 1);
}

static void draw_line(const struct gr_vertex *v1, const struct gr_vertex *v2)
{
    if (!win_open || !gl_ok) return;
    batch_vertex(GL_LINES, v1, 1);
    batch_vertex(GL_LINES, v2, 1);
}

/* v moved by (dx, dy), its iterated alpha 0 */
static struct gr_vertex aa_fringe(const struct gr_vertex *v, float dx, float dy)
{
    struct gr_vertex o = *v;

    o.x += dx;
    o.y += dy;
    o.a = 0.0f;
    return o;
}

/* the antialiasing of glide2x: a primitive gets edges one pixel wide, across
 * which the iterated alpha falls to 0; how it shows is up to the alpha combine
 * and the blend function the program set (no culling) */
static void aa_draw_line(const struct gr_vertex *v1, const struct gr_vertex *v2)
{
    struct gr_vertex u1, u2, d1, d2;
    float ox, oy;

    if (!win_open || !gl_ok) return;
    if (v1->x == v2->x && v1->y == v2->y) return;

    /* the fringes lie across the minor axis */
    if (fabsf(v2->x - v1->x) >= fabsf(v2->y - v1->y)) { ox = 0.0f; oy = 1.0f; }
    else { ox = 1.0f; oy = 0.0f; }
    u1 = aa_fringe(v1, -ox, -oy);
    u2 = aa_fringe(v2, -ox, -oy);
    d1 = aa_fringe(v1, ox, oy);
    d2 = aa_fringe(v2, ox, oy);

    batch_vertex(GL_TRIANGLES, v1, 0); batch_vertex(GL_TRIANGLES, v2, 0); batch_vertex(GL_TRIANGLES, &u2, 0);
    batch_vertex(GL_TRIANGLES, v1, 0); batch_vertex(GL_TRIANGLES, &u2, 0); batch_vertex(GL_TRIANGLES, &u1, 0);
    batch_vertex(GL_TRIANGLES, v1, 0); batch_vertex(GL_TRIANGLES, v2, 0); batch_vertex(GL_TRIANGLES, &d2, 0);
    batch_vertex(GL_TRIANGLES, v1, 0); batch_vertex(GL_TRIANGLES, &d2, 0); batch_vertex(GL_TRIANGLES, &d1, 0);
}

static void aa_draw_point(const struct gr_vertex *v)
{
    static const float dirs[5][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f }, { 0.0f, -1.0f }, { 1.0f, 0.0f } };
    struct gr_vertex c = *v, e0, e1;
    int i;

    if (!win_open || !gl_ok) return;
    c.x = floorf(v->x) + 0.5f;
    c.y = floorf(v->y) + 0.5f;
    for (i = 0; i < 4; i++)
    {
        e0 = aa_fringe(&c, dirs[i][0], dirs[i][1]);
        e1 = aa_fringe(&c, dirs[i + 1][0], dirs[i + 1][1]);
        batch_vertex(GL_TRIANGLES, &c, 0);
        batch_vertex(GL_TRIANGLES, &e0, 0);
        batch_vertex(GL_TRIANGLES, &e1, 0);
    }
}

/* a colour buffer's GL image -> its 565 shadow */
static void shadow_from_gl(int pi)
{
    uint32_t x, y;

    if (shadow_valid[pi]) return;
    ms_resolve(pi);
    if (hires)
    {
        /* scaled down into the buffer of the screen's size */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[pi]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_lo);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, fb_width, fb_height, 0, 0, scr_width, scr_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_lo);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[pi]);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    Game_GlReadBgra(scr_width, scr_height, xfer);
    for (y = 0; y < scr_height; y++)
    {
        uint16_t *d = color_mem[pi] + y * scr_stride;
        const uint32_t *s = xfer + y * scr_width;
        for (x = 0; x < scr_width; x++) d[x] = argb_to_565(s[x]);
        if (hires) memcpy(base_mem[pi] + y * scr_width, d, scr_width * 2);
    }
    shadow_valid[pi] = 1;
}

/* hires: the pixels of the shadow the guest changed, drawn over the GL image */
static void put_texture(GLuint target, GLuint tex, int blend);

static void shadow_changes_to_gl(int pi)
{
    uint32_t x, y, changed = 0;

    for (y = 0; y < scr_height; y++)
    {
        const uint16_t *s = color_mem[pi] + y * scr_stride;
        uint16_t *b = base_mem[pi] + y * scr_width;
        uint32_t *d = xfer + y * scr_width;
        for (x = 0; x < scr_width; x++)
        {
            if (s[x] != b[x])
            {
                d[x] = rgb565_to_argb(s[x]);
                b[x] = s[x];
                changed++;
            }
            else
            {
                d[x] = 0;
            }
        }
    }
    shadow_valid[pi] = 1;
    shadow_dirty[pi] = 0;
    if (changed == 0) return;

    glBindTexture(GL_TEXTURE_2D, lfb_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    Game_GlTexSubImageBgra(scr_width, scr_height, xfer);

    /* the texture's alpha picks the pixels */
    put_texture(draw_fbo(pi), lfb_tex, 1);
}

/* tex (the screen's rows, as xfer) over the whole of frame buffer target,
 * blended by its alpha or not */
static void put_texture(GLuint target, GLuint tex, int blend)
{
    struct gl_vertex q[6];
    int i;

    memset(q, 0, sizeof(q));
    for (i = 0; i < 6; i++)
    {
        static const uint8_t corner[6] = { 0, 1, 2, 2, 1, 3 };
        q[i].x = (corner[i] & 1) ? (float)scr_width : 0.0f;
        q[i].y = (corner[i] & 2) ? (float)scr_height : 0.0f;
        q[i].r = q[i].g = q[i].b = q[i].a = 1.0f;
        q[i].sow = q[i].x;
        q[i].tow = q[i].y;
        q[i].oow = 1.0f;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, fb_width, fb_height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    if (blend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glUseProgram(prog_draw);
    glUniform2f(u_inv_size, 2.0f / (float)scr_width, 2.0f / (float)scr_height);
    glUniform1f(u_psize, 1.0f);
    /* the picture as it is: not the combine, alpha test and fog of the last triangles (gl_flush sets
     * them again), nor the alpha test NEVER of uniforms never set (a program that only writes the LFB) */
    glUniform1i(u_cc, GR_COLORCOMBINE_DECAL_TEXTURE);
    glUniform1i(u_asrc, GR_ALPHASOURCE_TEXTURE_ALPHA);
    glUniform1i(u_cc_raw, 0);
    glUniform1i(u_ac_raw, 0);
    glUniform1i(u_atest, 7);            /* GR_CMP_ALWAYS */
    glUniform1i(u_fog, 0);
    glUniform1f(u_lodbias, 0.0f);
    glUniform1i(u_chroma_on, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_tex, 0);
    glUniform2f(u_texscale, 1.0f / (float)scr_width, 1.0f / (float)scr_height);
    glUniform1i(u_usetex, 1);
    glUniform1i(u_clampuv, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &q[0].x);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &q[0].r);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &q[0].sow);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glUseProgram(0);
}

/* the 565 shadow -> the GL image */
static void shadow_to_gl(int pi)
{
    uint32_t x, y;

    if (hires)
    {
        shadow_changes_to_gl(pi);
        return;
    }
    for (y = 0; y < scr_height; y++)
    {
        const uint16_t *s = color_mem[pi] + y * scr_stride;
        uint32_t *d = xfer + y * scr_width;
        for (x = 0; x < scr_width; x++) d[x] = rgb565_to_argb(s[x]);
    }
    glBindTexture(GL_TEXTURE_2D, fbo_tex[pi]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    Game_GlTexSubImageBgra(scr_width, scr_height, xfer);
    /* antialiasing: the image drawn over the multisampled one too */
    if (ms_samples != 0) put_texture(ms_fbo[pi], fbo_tex[pi], 0);
    ms_dirty[pi] = 0;
    shadow_valid[pi] = 1;
    shadow_dirty[pi] = 0;
}

/* the colour buffers at width x height, between two frames (the item "3D
 * resolution" of the port's panel): their images scaled into the new ones, the
 * shadows read again at the next lock; the screen's size if the new ones fail */
static void buffers_resize(uint32_t width, uint32_t height)
{
    GLuint old_fbo[3], old_tex[3];
    uint32_t old_width = fb_width, old_height = fb_height, i;

    gl_flush();
    for (i = 0; i < num_color_buffers; i++)
    {
        if (shadow_dirty[i]) shadow_to_gl((int)i);
        ms_resolve((int)i);
    }
    memcpy(old_fbo, fbo, sizeof(old_fbo));
    memcpy(old_tex, fbo_tex, sizeof(old_tex));
    memset(fbo, 0, sizeof(fbo));
    memset(fbo_tex, 0, sizeof(fbo_tex));
    buffers_delete();
    fb_width = width;
    fb_height = height;
    if (!buffers_create(num_color_buffers))
    {
        eprintf("Glide: no buffers of %ux%u\n", width, height);
        buffers_delete();
        fb_width = scr_width;
        fb_height = scr_height;
        if (!buffers_create(num_color_buffers)) gl_ok = 0;
    }
    if (!hires) eprintf("Glide: the 3D at the screen's %ux%u\n", fb_width, fb_height);
    for (i = 0; i < 3; i++)
    {
        if (gl_ok && fbo[i] != 0 && old_fbo[i] != 0)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, old_fbo[i]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[i]);
            glDisable(GL_SCISSOR_TEST);
            glBlitFramebuffer(0, 0, old_width, old_height, 0, 0, fb_width, fb_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (ms_samples != 0) put_texture(ms_fbo[i], fbo_tex[i], 0);
            ms_dirty[i] = 0;
            shadow_valid[i] = 0;
        }
        if (old_fbo[i] != 0) Game_GlFboDelete(&old_fbo[i], &old_tex[i], 0, NULL);
    }
}

/* the front buffer, scaled to the window with the aspect of the screen */
static void present_front(void)
{
    int pi = bidx[0];

    if (!gl_ok) return;
    gl_acquire();
    gl_flush();
    if (shadow_dirty[pi]) shadow_to_gl(pi);
    ms_resolve(pi);
    {
        char output[48];

        /* the game's mode, and the 3D drawn at Display_3D_Resolution */
        if (fb_width == scr_width && fb_height == scr_height) snprintf(output, sizeof(output), "Glide, OpenGL");
        else snprintf(output, sizeof(output), "Glide, OpenGL, 3D %ux%u", (unsigned)fb_width, (unsigned)fb_height);
        Game_OsdFrame((int)scr_width, (int)scr_height, output);
    }
    /* by the nearest pixel; the 3D of Display_3D_Resolution, bigger than the
     * mode, smoothed down to the window */
    Game_GlPresentTexture(gl_window, fbo_tex[pi], 4, 3,
                          (buffer_origin == GR_ORIGIN_UPPER_LEFT), (fb_width != scr_width) || (fb_height != scr_height));
}


/* what is drawn into the front buffer is on the screen: not after every triangle, but when a frame's
 * time has gone since the last time, and at grSstIdle and the like */
static int front_dirty;
static uint64_t front_time;

static void front_drawn(void)
{
    uint64_t now = SDL_GetPerformanceCounter();

    front_dirty = 1;
    if ((double)(now - front_time) >= (double)SDL_GetPerformanceFrequency() / 60.0)
    {
        front_dirty = 0;
        front_time = now;
        present_front();
    }
}

static void front_flush(void)
{
    gl_flush();
    if (front_dirty)
    {
        front_dirty = 0;
        front_time = SDL_GetPerformanceCounter();
        present_front();
    }
}

/* ---- the API ---- */

uint32_t CCALL grGlideInit_c(void)
{
    init_debug();
    DEBUG("grGlideInit\n");
    reset_state();
    free_textures();
    return 0;
}

uint32_t CCALL grGlideShutdown_c(void)
{
    DEBUG("grGlideShutdown\n");
    grSstWinClose_c();
    free_textures();
    return 0;
}

uint32_t CCALL grGlideGetState_c(void *state)
{
    static_assert(sizeof(struct glide_state) <= GRSTATE_SIZE, "GrState");
    DEBUG("grGlideGetState\n");
    if (state != NULL) memcpy(state, &st, sizeof(st));
    return 0;
}

uint32_t CCALL grGlideSetState_c(void *state)
{
    gl_flush();
    DEBUG("grGlideSetState\n");
    if (state != NULL) memcpy(&st, state, sizeof(st));
    return 0;
}

uint32_t CCALL grSstQueryHardware_c(void *hwconfig)
{
    struct gr_hw_config *hw = (struct gr_hw_config *)hwconfig;

    DEBUG("grSstQueryHardware\n");
    if (hw == NULL) return FXFALSE;

    /* only the boards found: a program may have room for fewer than 4 */
    memset(hw, 0, sizeof(int32_t) + sizeof(hw->SSTs[0]));
    hw->num_sst = 1;
    hw->SSTs[0].type = GR_SSTTYPE_VOODOO;
    hw->SSTs[0].fbRam = 4;
    hw->SSTs[0].fbiRev = 2;
    hw->SSTs[0].nTexelfx = 1;
    hw->SSTs[0].sliDetect = GAME_GLIDE_SLI ? 1 : 0;
    hw->SSTs[0].tmuConfig[0].tmuRev = 1;
    hw->SSTs[0].tmuConfig[0].tmuRam = 4;
    return FXTRUE;
}

uint32_t CCALL grSstSelect_c(uint32_t which_sst)
{
    DEBUG("grSstSelect: %u\n", which_sst);
    return 0;
}

uint32_t CCALL grSstWinOpen_c(uint32_t hWnd, uint32_t resolution, uint32_t refresh, uint32_t color_fmt, uint32_t origin, uint32_t nColBuffers, uint32_t nAuxBuffers)
{
    static const uint16_t resolutions[][2] = {
        { 320, 200 }, { 320, 240 }, { 400, 256 }, { 512, 384 }, { 640, 200 }, { 640, 350 },
        { 640, 400 }, { 640, 480 }, { 800, 600 }, { 960, 720 }, { 856, 480 }, { 512, 256 },
        { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 }, { 400, 300 }
    };
    static const uint8_t refresh_rates[] = { 60, 70, 72, 75, 80, 90, 100, 85, 120 };
    uint32_t i, size;

    init_debug();
    DEBUG("grSstWinOpen: 0x%x, %u, %u, %u, %u, %u, %u\n", hWnd, resolution, refresh, color_fmt, origin, nColBuffers, nAuxBuffers);

    if (resolution >= sizeof(resolutions) / sizeof(resolutions[0])) return FXFALSE;
    if (win_open) grSstWinClose_c();

    scr_width = resolutions[resolution][0];
    scr_height = resolutions[resolution][1];
    scr_stride = (scr_width > LFB_STRIDE) ? scr_width : LFB_STRIDE;
    refresh_hz = (refresh < sizeof(refresh_rates)) ? refresh_rates[refresh] : 60;
    color_format = color_fmt;
    buffer_origin = origin;
    num_color_buffers = (nColBuffers < 1) ? 1 : ((nColBuffers > 3) ? 3 : nColBuffers);
    num_aux_buffers = nAuxBuffers;

    size = scr_stride * scr_height * 2;
    for (i = 0; i < 3; i++)
    {
        color_mem[i] = NULL;
        buffer[i] = NULL;
        bidx[i] = 0;
    }
    for (i = 0; i < num_color_buffers; i++)
    {
        color_mem[i] = (uint16_t *)x86_malloc(size);
        if (color_mem[i] == NULL) goto fail;
        memset(color_mem[i], 0, size);
        buffer[i] = color_mem[i];
        bidx[i] = (int)i;
    }
    /* a single buffer is both the front and the back buffer */
    if (num_color_buffers == 1)
    {
        buffer[1] = buffer[0];
        bidx[1] = 0;
    }
    aux_mem = (uint16_t *)x86_malloc(size);
    if (aux_mem == NULL) goto fail;
    memset(aux_mem, 0xff, size);

    xfer = (uint32_t *)malloc(scr_width * scr_height * 4);
    if (xfer == NULL) goto fail;

    if (!gl_start(num_color_buffers)) goto fail;

    vr_base = SDL_GetPerformanceCounter();
    win_open = 1;
    reset_state();
    gl_release();
    return FXTRUE;

fail:
    eprintf("grSstWinOpen: cannot open %ux%u\n", scr_width, scr_height);
    gl_stop();
    for (i = 0; i < 3; i++)
    {
        x86_free(color_mem[i]);
        color_mem[i] = NULL;
        buffer[i] = NULL;
    }
    x86_free(aux_mem);
    aux_mem = NULL;
    free(xfer);
    xfer = NULL;
    return FXFALSE;
}

uint32_t CCALL grSstWinClose_c(void)
{
    uint32_t i;

    DEBUG("grSstWinClose\n");
    if (!win_open) return 0;
    win_open = 0;

    gl_acquire();
    gl_stop();
    for (i = 0; i < 3; i++)
    {
        x86_free(color_mem[i]);
        color_mem[i] = NULL;
        buffer[i] = NULL;
    }
    x86_free(aux_mem);
    aux_mem = NULL;
    free(xfer);
    xfer = NULL;
    x86_free(lfb_scratch);
    lfb_scratch = NULL;
    return 0;
}

uint32_t CCALL grSstIdle_c(void)
{
    DEBUG("grSstIdle\n");
    if (win_open) front_flush();
    return 0;
}

uint32_t CCALL grSstVRetraceOn_c(void)
{
    if (vr_in_retrace()) return FXTRUE;
    /* a program that polls for the retrace sleeps here instead of spinning */
    vr_wait_retrace();
    return FXFALSE;
}

uint32_t CCALL grBufferClear_c(uint32_t color, uint32_t alpha, uint32_t depth)
{
    uint32_t argb = color_to_argb(color);
    int pi = phys_of(st.render_buffer);

    (void)alpha;
    DEBUG("grBufferClear: 0x%x, 0x%x, 0x%x\n", color, alpha, depth);
    if (!win_open || pi < 0) return 0;
    gl_acquire();

    gl_flush();
    if (shadow_dirty[pi]) shadow_to_gl(pi);
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo(pi));
    set_scissor();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(((argb >> 16) & 0xff) / 255.0f, ((argb >> 8) & 0xff) / 255.0f, (argb & 0xff) / 255.0f, 1.0f);
    if (st.depth_mode != GR_DEPTHBUFFER_DISABLE)
    {
        glDepthMask(GL_TRUE);
        glClearDepth((double)(depth & 0xffff) / 65535.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (st.clip_minx == 0 && st.clip_miny == 0 && st.clip_maxx == (int32_t)scr_width && st.clip_maxy == (int32_t)scr_height)
    {
        /* the whole buffer is one colour: the shadow need not be read back */
        uint16_t c = argb_to_565(argb);
        uint32_t x, y;
        for (y = 0; y < scr_height; y++)
        {
            uint16_t *row = color_mem[pi] + y * scr_stride;
            for (x = 0; x < scr_width; x++) row[x] = c;
            if (hires)
            {
                for (x = 0; x < scr_width; x++) base_mem[pi][y * scr_width + x] = c;
            }
        }
        shadow_valid[pi] = 1;
        shadow_dirty[pi] = 0;
    }
    else
    {
        shadow_valid[pi] = 0;
    }
    return 0;
}

uint32_t CCALL grBufferSwap_c(uint32_t swap_interval)
{
    uint16_t *tmp;
    int ti;

    DEBUG("grBufferSwap: %u\n", swap_interval);
    if (!win_open) return 0;

    gl_flush();
    if (gl_ok)
    {
        uint32_t width, height;

        /* Display_3D_Resolution changed from the port's panel */
        fb_target(&width, &height, 0);
        if (width != fb_width || height != fb_height)
        {
            gl_acquire();
            buffers_resize(width, height);
        }
    }
    if (swap_interval > 0) vr_wait_retrace();

    if (num_color_buffers > 1)
    {
        tmp = buffer[0];
        ti = bidx[0];
        buffer[0] = buffer[1];
        bidx[0] = bidx[1];
        if (num_color_buffers == 3)
        {
            buffer[1] = buffer[2];
            bidx[1] = bidx[2];
            buffer[2] = tmp;
            bidx[2] = ti;
        }
        else
        {
            buffer[1] = tmp;
            bidx[1] = ti;
        }
    }
    present_front();
    gl_release();
    return 0;
}

uint32_t CCALL grRenderBuffer_c(uint32_t buf)
{
    gl_flush();
    DEBUG("grRenderBuffer: %u\n", buf);
    st.render_buffer = buf;
    return 0;
}

uint32_t CCALL grClipWindow_c(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy)
{
    gl_flush();
    DEBUG("grClipWindow: %u, %u, %u, %u\n", minx, miny, maxx, maxy);
    if (maxx > scr_width) maxx = scr_width;
    if (maxy > scr_height) maxy = scr_height;
    if (minx > maxx) minx = maxx;
    if (miny > maxy) miny = maxy;
    st.clip_minx = (int32_t)minx;
    st.clip_miny = (int32_t)miny;
    st.clip_maxx = (int32_t)maxx;
    st.clip_maxy = (int32_t)maxy;
    return 0;
}

uint32_t CCALL grCullMode_c(uint32_t mode)
{
    gl_flush();
    DEBUG("grCullMode: %u\n", mode);
    st.cull_mode = mode;
    return 0;
}

uint32_t CCALL grDepthBufferMode_c(uint32_t mode)
{
    gl_flush();
    DEBUG("grDepthBufferMode: %u\n", mode);
    st.depth_mode = mode;
    return 0;
}

uint32_t CCALL grDepthBufferFunction_c(uint32_t function)
{
    gl_flush();
    DEBUG("grDepthBufferFunction: %u\n", function);
    st.depth_function = function;
    return 0;
}

uint32_t CCALL grDepthMask_c(uint32_t mask)
{
    gl_flush();
    DEBUG("grDepthMask: %u\n", mask);
    st.depth_mask = mask;
    return 0;
}

uint32_t CCALL grConstantColorValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grConstantColorValue: 0x%x\n", value);
    st.constant_color = value;
    return 0;
}

uint32_t CCALL grChromakeyMode_c(uint32_t mode)
{
    gl_flush();
    DEBUG("grChromakeyMode: %u\n", mode);
    st.chromakey_mode = mode;
    return 0;
}

uint32_t CCALL grChromakeyValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grChromakeyValue: 0x%x\n", value);
    st.chromakey_value = value;
    return 0;
}

uint32_t CCALL grAlphaBlendFunction_c(uint32_t rgb_sf, uint32_t rgb_df, uint32_t alpha_sf, uint32_t alpha_df)
{
    gl_flush();
    DEBUG("grAlphaBlendFunction: %u, %u, %u, %u\n", rgb_sf, rgb_df, alpha_sf, alpha_df);
    st.rgb_sf = rgb_sf;
    st.rgb_df = rgb_df;
    st.alpha_sf = alpha_sf;
    st.alpha_df = alpha_df;
    return 0;
}

static void lfb_convert(int to_scratch, uint16_t *buf)
{
    uint32_t x, y;

    for (y = 0; y < scr_height; y++)
    {
        uint16_t *row = buf + y * scr_stride;
        if (lfb_scratch_bpp == 32)
        {
            uint32_t *s = (uint32_t *)lfb_scratch + y * scr_stride;
            for (x = 0; x < scr_width; x++)
            {
                if (to_scratch) s[x] = rgb565_to_argb(row[x]);
                else row[x] = argb_to_565(s[x]);
            }
        }
        else
        {
            uint16_t *s = (uint16_t *)lfb_scratch + y * scr_stride;
            for (x = 0; x < scr_width; x++)
            {
                if (to_scratch)
                {
                    uint32_t c = rgb565_to_argb(row[x]);
                    s[x] = (uint16_t)(0x8000 | ((c >> 9) & 0x7c00) | ((c >> 6) & 0x03e0) | ((c >> 3) & 0x001f));
                }
                else
                {
                    row[x] = argb_to_565(argb1555_to_argb(s[x]));
                }
            }
        }
    }
}

uint32_t CCALL grLfbLock_c(uint32_t type, uint32_t buf, uint32_t writeMode, uint32_t origin, uint32_t pixelPipeline, void *info)
{
    struct gr_lfb_info *li = (struct gr_lfb_info *)info;
    uint16_t *mem;
    int write = (type & 1) == GR_LFB_WRITE_ONLY;

    DEBUG("grLfbLock: %u, %u, %u, %u, %u\n", type, buf, writeMode, origin, pixelPipeline);
    if (!win_open || li == NULL) return FXFALSE;
    gl_acquire();
    mem = buffer_of(buf);
    if (mem == NULL) return FXFALSE;
    if (buf != GR_BUFFER_AUXBUFFER)
    {
        /* what was drawn since the last lock is in the shadow */
        gl_flush();
        shadow_from_gl(phys_of(buf));
    }
    if (origin != buffer_origin)
    {
        DEBUG("grLfbLock: origin %u of a buffer of origin %u\n", origin, buffer_origin);
    }

    if (!write || writeMode == GR_LFBWRITEMODE_565 || writeMode == GR_LFBWRITEMODE_ANY
        || writeMode == GR_LFBWRITEMODE_ZA16 || buf == GR_BUFFER_AUXBUFFER)
    {
        li->lfbPtr = GAME_PTR_TO_GUEST32(mem);
        li->strideInBytes = scr_stride * 2;
        li->writeMode = GR_LFBWRITEMODE_565;
    }
    else
    {
        lfb_scratch_bpp = (writeMode == GR_LFBWRITEMODE_888 || writeMode == GR_LFBWRITEMODE_8888) ? 32 : 16;
        if (lfb_scratch == NULL)
        {
            /* room for 32 bits per pixel */
            lfb_scratch = x86_malloc(scr_stride * scr_height * 4);
            if (lfb_scratch == NULL) return FXFALSE;
        }
        lfb_scratch_buffer = buf;
        lfb_scratch_write = 1;
        lfb_convert(1, mem);
        li->lfbPtr = GAME_PTR_TO_GUEST32(lfb_scratch);
        li->strideInBytes = scr_stride * (lfb_scratch_bpp / 8);
        li->writeMode = writeMode;
    }
    li->origin = buffer_origin;
    return FXTRUE;
}

uint32_t CCALL grLfbUnlock_c(uint32_t type, uint32_t buf)
{
    int write = (type & 1) == GR_LFB_WRITE_ONLY;

    DEBUG("grLfbUnlock: %u, %u\n", type, buf);
    if (!win_open) return FXFALSE;
    gl_acquire();

    if (write && lfb_scratch_write && lfb_scratch_buffer == buf)
    {
        uint16_t *mem = buffer_of(buf);
        if (mem != NULL) lfb_convert(0, mem);
        lfb_scratch_write = 0;
    }
    if (write && buf != GR_BUFFER_AUXBUFFER && phys_of(buf) >= 0)
    {
        /* put into the GL image when it is next used: a program locks for every sprite */
        shadow_dirty[phys_of(buf)] = 1;
        shadow_valid[phys_of(buf)] = 1;
        /* what is written into the front buffer is on the screen at once */
        if (buf == GR_BUFFER_FRONTBUFFER) present_front();
    }
    return FXTRUE;
}

uint32_t CCALL grTexDownloadTable_c(uint32_t tmu, uint32_t type, void *data)
{
    gl_flush();
    DEBUG("grTexDownloadTable: %u, %u\n", tmu, type);
    if (type == GR_TEXTABLE_PALETTE && data != NULL)
    {
        memcpy(palette, data, sizeof(palette));
        palette_serial++;
    }
    return 0;
}

uint32_t CCALL grAADrawPoint_c(void *p)
{
    if (p != NULL) aa_draw_point((const struct gr_vertex *)p);
    return 0;
}

uint32_t CCALL grAADrawLine_c(void *v1, void *v2)
{
    if (v1 != NULL && v2 != NULL) aa_draw_line((const struct gr_vertex *)v1, (const struct gr_vertex *)v2);
    return 0;
}

uint32_t CCALL grDrawTriangle_c(void *a, void *b, void *c)
{
    if (a != NULL && b != NULL && c != NULL)
    {
        draw_triangle((const struct gr_vertex *)a, (const struct gr_vertex *)b, (const struct gr_vertex *)c);
    }
    return 0;
}

uint32_t CCALL grDrawLine_c(void *v1, void *v2)
{
    if (v1 != NULL && v2 != NULL) draw_line((const struct gr_vertex *)v1, (const struct gr_vertex *)v2);
    return 0;
}

uint32_t CCALL grDrawPoint_c(void *p)
{
    if (p != NULL) draw_point((const struct gr_vertex *)p);
    return 0;
}

/* a convex polygon as a fan of triangles; ilist, when not NULL, indexes vlist */
static void draw_polygon(uint32_t count, const struct gr_vertex *v, const int *ilist)
{
    uint32_t i;

    if (v == NULL || count < 3) return;
    for (i = 1; i + 1 < count; i++)
    {
        const struct gr_vertex *a = ilist ? &v[ilist[0]] : &v[0];
        const struct gr_vertex *b = ilist ? &v[ilist[i]] : &v[i];
        const struct gr_vertex *c = ilist ? &v[ilist[i + 1]] : &v[i + 1];

        draw_triangle(a, b, c);
    }
}

uint32_t CCALL grDrawPolygon_c(uint32_t count, void *vertices, uint32_t flags)
{
    draw_polygon(count, (const struct gr_vertex *)vertices,
                 (const int *)(void *)(uintptr_t)flags);
    return 0;
}

uint32_t CCALL grDrawPlanarPolygon_c(uint32_t count, void *vertices, uint32_t flags)
{
    draw_polygon(count, (const struct gr_vertex *)vertices,
                 (const int *)(void *)(uintptr_t)flags);
    return 0;
}

uint32_t CCALL grDrawPolygonVertexList_c(uint32_t count, void *vertices)
{
    draw_polygon(count, (const struct gr_vertex *)vertices, NULL);
    return 0;
}

uint32_t CCALL grAADrawPolygon_c(uint32_t count, void *vertices, void *ilist)
{
    draw_polygon(count, (const struct gr_vertex *)vertices,
                 (const int *)ilist);
    return 0;
}

uint32_t CCALL grAADrawPolygonVertexList_c(uint32_t count, void *vertices)
{
    draw_polygon(count, (const struct gr_vertex *)vertices, NULL);
    return 0;
}

uint32_t CCALL grColorCombine_c(uint32_t function, uint32_t factor, uint32_t local, uint32_t other, uint32_t invert)
{
    gl_flush();
    DEBUG("grColorCombine: %u, %u, 0x%x, 0x%x, %u\n", function, factor, local, other, invert);
    st.cc_raw = 1;
    st.cc_function = function;
    st.cc_factor = factor;
    st.cc_local = local;
    st.cc_other = other;
    st.cc_invert = invert;
    return 0;
}

uint32_t CCALL guColorCombineFunction_c(uint32_t function)
{
    gl_flush();
    DEBUG("guColorCombineFunction: %u\n", function);
    st.cc_raw = 0;
    st.color_combine = function;
    return 0;
}

uint32_t CCALL grAlphaCombine_c(uint32_t function, uint32_t factor, uint32_t local, uint32_t other, uint32_t invert)
{
    gl_flush();
    DEBUG("grAlphaCombine: %u, %u, %u, %u, %u\n", function, factor, local, other, invert);
    st.ac_raw = 1;
    st.ac_function = function;
    st.ac_factor = factor;
    st.ac_local = local;
    st.ac_other = other;
    st.ac_invert = invert;
    return 0;
}

/* GR_FOG_DISABLE 0, GR_FOG_WITH_ITERATED_ALPHA 1, GR_FOG_WITH_TABLE 2 (the shader's 2 and 1) */
uint32_t CCALL grFogMode_c(uint32_t mode)
{
    gl_flush();
    DEBUG("grFogMode: %u\n", mode);
    mode &= 0xff;       /* without GR_FOG_MULT2 0x100 and GR_FOG_ADD2 0x200 */
    st.fog_mode = (mode == 1) ? 2 : (mode == 2) ? 1 : 0;
    return 0;
}

uint32_t CCALL grFogColorValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grFogColorValue: 0x%x\n", value);
    st.fog_color = value;
    return 0;
}

uint32_t CCALL grFogTable_c(void *table)
{
    const uint8_t *t = (const uint8_t *)table;
    int i;

    gl_flush();
    DEBUG("grFogTable\n");
    if (t != NULL) for (i = 0; i < 64; i++) fog_table[i] = t[i] / 255.0f;
    return 0;
}

/* guFogGenerateExp makes no table yet (no program of ours calls it) */
uint32_t CCALL guFogGenerateExp_c(void *table, uint32_t mode)
{
    (void)table; (void)mode;
    gl_flush();
    DEBUG("guFogGenerateExp: %u\n", mode);
    return 0;
}

uint32_t CCALL grGammaCorrectionValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grGammaCorrectionValue: 0x%x\n", value);
    return 0;
}

uint32_t CCALL grLfbConstantAlpha_c(uint32_t value)
{
    gl_flush();
    DEBUG("grLfbConstantAlpha: 0x%x\n", value);
    return 0;
}

uint32_t CCALL grLfbConstantDepth_c(uint32_t value)
{
    gl_flush();
    DEBUG("grLfbConstantDepth: 0x%x\n", value);
    return 0;
}

uint32_t CCALL grTexSource_c(uint32_t tmu, uint32_t startAddress, uint32_t evenOdd, void *info)
{
    const struct gr_tex_info *ti = (const struct gr_tex_info *)info;

    (void)evenOdd;
    gl_flush();
    DEBUG("grTexSource: tmu=%u address 0x%x\n", tmu, startAddress);
    if (ti == NULL) return 0;
    st.tex_raw = 1;
    st.tex_addr = startAddress;
    st.tex_small = (uint32_t)ti->smallLod;
    st.tex_large = (uint32_t)ti->largeLod;
    st.tex_aspect = (uint32_t)ti->aspectRatio;
    st.tex_format = (uint32_t)ti->format;
    return 0;
}

uint32_t CCALL grTexDownloadMipMap_c(uint32_t tmu, uint32_t startAddress, uint32_t evenOdd, void *info)
{
    const struct gr_tex_info *ti = (const struct gr_tex_info *)info;
    uint32_t size, i;

    (void)evenOdd;
    gl_flush();
    DEBUG("grTexDownloadMipMap: tmu=%u address 0x%x\n", tmu, startAddress);
    if (ti == NULL || ti->data == 0 || ti->largeLod < 0 || ti->smallLod < ti->largeLod || ti->smallLod > 8
        || ti->aspectRatio < 0 || ti->aspectRatio > 6 || ti->format < 0 || ti->format > 0xe) return 0;
    size = tex_mipmap_bytes((uint32_t)ti->smallLod, (uint32_t)ti->largeLod, (uint32_t)ti->aspectRatio, (uint32_t)ti->format);
    if (startAddress + size > TMU_MEM_SIZE) return 0;
    if (tmu_mem == NULL)
    {
        tmu_mem = (uint8_t *)calloc(1, TMU_MEM_SIZE);
        if (tmu_mem == NULL) return 0;
    }
    memcpy(tmu_mem + startAddress, GUEST_MEM(ti->data), size);
    /* the textures that were made of what was there are old */
    for (i = 0; i < num_raw_textures; i++)
    {
        struct raw_texture *r = &raw_textures[i];
        if (r->tex.used && r->addr < startAddress + size && startAddress < r->addr + r->size) raw_texture_free(r);
    }
    return 0;
}

uint32_t CCALL grTexCalcMemRequired_c(uint32_t smallest_lod, uint32_t largest_lod, uint32_t aspect, uint32_t format)
{
    uint32_t big, w, h, lod, size;

    (void)format;
    DEBUG("grTexCalcMemRequired: lod %u-%u, aspect %u\n", smallest_lod, largest_lod, aspect);
    if (largest_lod > 8 || smallest_lod > 8 || smallest_lod < largest_lod || aspect > 6) return 0;

    big = lod_size(largest_lod);
    w = (aspect <= 3) ? big : (big >> (aspect - 3));
    h = (aspect >= 3) ? big : (big >> (3 - aspect));
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    size = 0;
    for (lod = largest_lod; lod <= smallest_lod; lod++)
    {
        uint32_t lw = (lod == largest_lod) ? w : (w >> (lod - largest_lod));
        uint32_t lh = (lod == largest_lod) ? h : (h >> (lod - largest_lod));
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        size += lw * lh * texel_bytes(format);
    }
    return size;
}

uint32_t CCALL grTexFilterMode_c(uint32_t tmu, uint32_t minFilter, uint32_t magFilter)
{
    gl_flush();
    DEBUG("grTexFilterMode: tmu=%u min=%u mag=%u\n", tmu, minFilter, magFilter);
    st.tex_minfilter = minFilter;
    st.tex_magfilter = magFilter;
    return 0;
}

uint32_t CCALL grTexClampMode_c(uint32_t tmu, uint32_t sClamp, uint32_t tClamp)
{
    gl_flush();
    DEBUG("grTexClampMode: tmu=%u sClamp=%u tClamp=%u\n", tmu, sClamp, tClamp);
    st.tex_sclamp = sClamp;
    st.tex_tclamp = tClamp;
    return 0;
}

uint32_t CCALL grTexCombine_c(uint32_t tmu, uint32_t rgb_function, uint32_t rgb_factor, uint32_t alpha_function, uint32_t alpha_factor, uint32_t rgb_invert, uint32_t alpha_invert)
{
    (void)tmu; (void)rgb_function; (void)alpha_function; (void)rgb_invert; (void)alpha_invert; (void)rgb_factor; (void)alpha_factor;
    gl_flush();
    DEBUG("grTexCombine: tmu=%u\n", tmu);
    if (tmu == 0) st.tex_combine = rgb_function;
    return 0;
}

uint32_t CCALL grTexMinAddress_c(uint32_t tmu)
{
    gl_flush();
    DEBUG("grTexMinAddress: %u\n", tmu);
    return 0;
}

uint32_t CCALL grTexMaxAddress_c(uint32_t tmu)
{
    gl_flush();
    DEBUG("grTexMaxAddress: %u\n", tmu);
    return TMU_MEM_SIZE;
}

uint32_t CCALL grTexCombineFunction_c(uint32_t tmu, uint32_t function)
{
    gl_flush();
    DEBUG("grTexCombineFunction: %u, %u\n", tmu, function);
    if (tmu == 0) st.tex_combine = function;
    return 0;
}

uint32_t CCALL guAlphaSource_c(uint32_t mode)
{
    gl_flush();
    DEBUG("guAlphaSource: %u\n", mode);
    st.ac_raw = 0;
    st.alpha_source = mode;
    return 0;
}

uint32_t CCALL guTexCombineFunction_c(uint32_t tmu, uint32_t function)
{
    gl_flush();
    DEBUG("guTexCombineFunction: %u, %u\n", tmu, function);
    if (tmu == 0) st.tex_combine = function;
    return 0;
}

uint32_t CCALL guTexMemReset_c(void)
{
    gl_flush();
    DEBUG("guTexMemReset\n");
    free_textures();
    st.texture = GR_NULL_MIPMAP_HANDLE;
    return 0;
}

uint32_t CCALL guTexAllocateMemory_c(uint32_t tmu, uint32_t odd_even_mask, uint32_t width, uint32_t height, uint32_t format, uint32_t mm_mode, uint32_t smallest_lod, uint32_t largest_lod, uint32_t aspect, uint32_t s_clamp_mode, uint32_t t_clamp_mode, uint32_t minfilter_mode, uint32_t magfilter_mode, uint32_t lod_bias, uint32_t trilinear)
{
    struct glide_texture *t;
    uint32_t big, w, h, lod, size;

    (void)tmu; (void)odd_even_mask; (void)trilinear;
    DEBUG("guTexAllocateMemory: %u, %u, %ux%u, fmt %u, mm %u, lod %u-%u, aspect %u, clamp %u/%u, filter %u/%u\n",
          tmu, odd_even_mask, width, height, format, mm_mode, smallest_lod, largest_lod, aspect,
          s_clamp_mode, t_clamp_mode, minfilter_mode, magfilter_mode);

    if (num_textures >= MAX_TEXTURES || largest_lod > 8 || smallest_lod > 8 || smallest_lod < largest_lod || aspect > 6)
    {
        return GR_NULL_MIPMAP_HANDLE;
    }

    t = &textures[num_textures];
    memset(t, 0, sizeof(*t));

    /* GR_ASPECT_8x1 = 0 ... GR_ASPECT_1x1 = 3 ... GR_ASPECT_1x8 = 6 */
    big = lod_size(largest_lod);
    w = (aspect <= 3) ? big : (big >> (aspect - 3));
    h = (aspect >= 3) ? big : (big >> (3 - aspect));
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    t->width = w;
    t->height = h;

    size = 0;
    for (lod = largest_lod; lod <= smallest_lod; lod++)
    {
        uint32_t lw = (lod == largest_lod) ? w : (w >> (lod - largest_lod));
        uint32_t lh = (lod == largest_lod) ? h : (h >> (lod - largest_lod));
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        size += lw * lh * texel_bytes(format);
    }

    t->format = format;
    t->largest_lod = largest_lod;
    t->smallest_lod = smallest_lod;
    t->aspect = aspect;
    t->s_clamp = s_clamp_mode;
    t->t_clamp = t_clamp_mode;
    t->minfilter = minfilter_mode;
    t->magfilter = magfilter_mode;
    t->mm_mode = mm_mode;
    memcpy(&t->lod_bias, &lod_bias, sizeof(t->lod_bias));
    t->data_size = size;
    t->raw = (uint8_t *)malloc(size);
    t->argb = (uint32_t *)malloc(size / texel_bytes(format) * 4);
    if (t->raw == NULL || t->argb == NULL)
    {
        free(t->raw);
        free(t->argb);
        memset(t, 0, sizeof(*t));
        return GR_NULL_MIPMAP_HANDLE;
    }
    t->used = 1;
    return num_textures++;
}

uint32_t CCALL guTexDownloadMipMap_c(uint32_t mmid, void *src, void *nccTable)
{
    struct glide_texture *t;

    (void)nccTable;
    gl_flush();
    DEBUG("guTexDownloadMipMap: %u\n", mmid);
    if (mmid >= num_textures || src == NULL) return 0;
    t = &textures[mmid];
    if (!t->used) return 0;

    memcpy(t->raw, src, t->data_size);
    decode_texture(t);
    t->downloaded = 1;
    return 0;
}

uint32_t CCALL guTexSource_c(uint32_t mmid)
{
    gl_flush();
    DEBUG("guTexSource: %u\n", mmid);
    st.tex_raw = 0;
    st.texture = mmid;
    return 0;
}

uint32_t CCALL guDrawTriangleWithClip_c(void *a, void *b, void *c)
{
    DEBUG("guDrawTriangleWithClip\n");
    if (a != NULL && b != NULL && c != NULL)
    {
        draw_triangle((const struct gr_vertex *)a, (const struct gr_vertex *)b, (const struct gr_vertex *)c);
    }
    return 0;
}

/* ---- the rest of glide2x.dll, as a driver DLL of a program uses it ---- */

uint32_t CCALL grAADrawTriangle_c(void *a, void *b, void *c, uint32_t ab_antialias, uint32_t bc_antialias, uint32_t ca_antialias)
{
    (void)ab_antialias; (void)bc_antialias; (void)ca_antialias;
    return grDrawTriangle_c(a, b, c);
}

uint32_t CCALL grAlphaTestFunction_c(uint32_t function)
{
    gl_flush();
    DEBUG("grAlphaTestFunction: %u\n", function);
    st.alpha_test_function = function;
    return 0;
}

uint32_t CCALL grAlphaTestReferenceValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grAlphaTestReferenceValue: %u\n", value);
    st.alpha_test_reference = value & 0xff;
    return 0;
}

uint32_t CCALL grBufferNumPending_c(void)
{
    return 0;
}

uint32_t CCALL grDepthBiasLevel_c(uint32_t level)
{
    gl_flush();
    DEBUG("grDepthBiasLevel: %d\n", (int)level);
    st.depth_bias = (int16_t)level;
    return 0;
}

uint32_t CCALL grDitherMode_c(uint32_t mode)
{
    DEBUG("grDitherMode: %u\n", mode);
    return 0;
}

uint32_t CCALL grGlideGetVersion_c(void *version)
{
    if (version != NULL) strcpy((char *)version, "2.46");
    return 0;
}

uint32_t CCALL grHints_c(uint32_t hintType, uint32_t hintMask)
{
    DEBUG("grHints: %u, 0x%x\n", hintType, hintMask);
    return 0;
}

/* the rectangle of a buffer, in the 16 bits of its pixels, to and from the program's memory */
uint32_t CCALL grLfbReadRegion_c(uint32_t src_buffer, uint32_t src_x, uint32_t src_y, uint32_t src_width, uint32_t src_height, uint32_t dst_stride, void *dst_data)
{
    uint16_t *mem = buffer_of(src_buffer);
    uint32_t y;

    DEBUG("grLfbReadRegion: %u, %u,%u %ux%u\n", src_buffer, src_x, src_y, src_width, src_height);
    if (!win_open || mem == NULL || dst_data == NULL) return FXFALSE;
    gl_acquire();
    if (src_buffer != GR_BUFFER_AUXBUFFER)
    {
        gl_flush();
        shadow_from_gl(phys_of(src_buffer));
    }
    for (y = 0; y < src_height && src_y + y < scr_height; y++)
    {
        uint32_t w = src_width;
        if (src_x + w > scr_width) w = scr_width > src_x ? scr_width - src_x : 0;
        memcpy((uint8_t *)dst_data + y * dst_stride, mem + (src_y + y) * scr_stride + src_x, w * 2);
    }
    return FXTRUE;
}

uint32_t CCALL grLfbWriteRegion_c(uint32_t dst_buffer, uint32_t dst_x, uint32_t dst_y, uint32_t src_format, uint32_t src_width, uint32_t src_height, uint32_t src_stride, void *src_data)
{
    uint16_t *mem = buffer_of(dst_buffer);
    uint32_t x, y;

    DEBUG("grLfbWriteRegion: %u, %u,%u format %u %ux%u\n", dst_buffer, dst_x, dst_y, src_format, src_width, src_height);
    if (!win_open || mem == NULL || src_data == NULL) return FXFALSE;
    gl_acquire();
    if (src_format != GR_LFBWRITEMODE_565 && src_format != GR_LFBWRITEMODE_555 && src_format != GR_LFBWRITEMODE_1555
        && src_format != GR_LFBWRITEMODE_888 && src_format != GR_LFBWRITEMODE_8888)
    {
        DEBUG("grLfbWriteRegion: format %u not supported\n", src_format);
        return FXFALSE;
    }
    if (dst_buffer != GR_BUFFER_AUXBUFFER)
    {
        gl_flush();
        shadow_from_gl(phys_of(dst_buffer));
    }
    for (y = 0; y < src_height && dst_y + y < scr_height; y++)
    {
        const uint8_t *row = (const uint8_t *)src_data + y * src_stride;
        uint16_t *out = mem + (dst_y + y) * scr_stride + dst_x;
        for (x = 0; x < src_width && dst_x + x < scr_width; x++)
        {
            switch (src_format)
            {
            case GR_LFBWRITEMODE_565: out[x] = ((const uint16_t *)row)[x]; break;
            case GR_LFBWRITEMODE_555:
            case GR_LFBWRITEMODE_1555: out[x] = argb_to_565(argb1555_to_argb(((const uint16_t *)row)[x])); break;
            default: out[x] = argb_to_565(((const uint32_t *)row)[x]); break;
            }
        }
    }
    if (dst_buffer != GR_BUFFER_AUXBUFFER && phys_of(dst_buffer) >= 0)
    {
        shadow_dirty[phys_of(dst_buffer)] = 1;
        shadow_valid[phys_of(dst_buffer)] = 1;
        if (dst_buffer == GR_BUFFER_FRONTBUFFER) present_front();
    }
    return FXTRUE;
}

uint32_t CCALL grSstControl_c(uint32_t code)
{
    DEBUG("grSstControl: %u\n", code);
    return FXTRUE;
}

uint32_t CCALL grSstIsBusy_c(void)
{
    return FXFALSE;
}

uint32_t CCALL grSstQueryBoards_c(void *hwconfig)
{
    if (hwconfig == NULL) return FXFALSE;
    ((struct gr_hw_config *)hwconfig)->num_sst = 1;
    return FXTRUE;
}

uint32_t CCALL grSstScreenHeight_c(void)
{
    return scr_height;
}

uint32_t CCALL grSstScreenWidth_c(void)
{
    return scr_width;
}

uint32_t CCALL grSstStatus_c(void)
{
    /* bit 7 is the bus' busy bit of the status register: never busy */
    return 0;
}

/* the bias is a float, -8 to 7.75 in the LODs of the board */
uint32_t CCALL grTexLodBiasValue_c(uint32_t tmu, uint32_t bias)
{
    float b;

    gl_flush();
    DEBUG("grTexLodBiasValue: %u, 0x%x\n", tmu, bias);
    memcpy(&b, &bias, sizeof(b));
    if (!(b >= -8.0f)) b = -8.0f;
    if (b > 7.75f) b = 7.75f;
    st.lod_bias = b;
    return 0;
}

uint32_t CCALL grTexMipMapMode_c(uint32_t tmu, uint32_t mode, uint32_t lodBlend)
{
    gl_flush();
    DEBUG("grTexMipMapMode: %u, %u, %u\n", tmu, mode, lodBlend);
    st.mipmap_mode = mode;
    return 0;
}

#ifdef __cplusplus
}
#endif
