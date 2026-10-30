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
 * does the gu* colour and alpha combine functions, texturing from the
 * largest LOD of a texture (no mipmapping) and chroma-key; the clip window is
 * the scissor, alpha blending the GL blend function. A linear frame buffer
 * lock gives the guest an RGB565 shadow of a buffer, with the stride of a
 * Voodoo's LFB (2048 bytes) in memory it can address: a lock reads the buffer
 * into it, the unlock of a write lock puts it back. grBufferSwap shows the
 * front buffer scaled to the window, which the DirectDraw layer makes.
 * Left out: the depth buffer, fog, alpha test, dithering, NCC (YIQ) textures,
 * more than one TMU, antialiasing. GLIDE_DEBUG=1 in the environment logs the
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

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#define FXTRUE 1
#define FXFALSE 0

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
};

struct glide_texture {
    int used;
    uint32_t format;
    uint32_t width, height;         /* of the largest LOD */
    uint32_t largest_lod, smallest_lod, aspect;
    uint32_t s_clamp, t_clamp, minfilter, magfilter;
    uint32_t data_size;             /* of all the LODs, in bytes */
    uint8_t *raw;                   /* the LODs as downloaded */
    uint32_t *argb;                 /* the largest LOD, decoded */
    uint32_t decoded_palette;       /* palette_serial at decoding (paletted formats) */
    int downloaded;
    GLuint gltex;                   /* the decoded image on the GPU */
    int gl_dirty;                   /* argb changed since the upload */
};

static int glide_debug = -1;
static int gl_ok;
static void gl_flush(void);
static void shadow_to_gl(int pi);

static struct glide_state st;

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
    uint32_t n = t->width * t->height, i;
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

static struct glide_texture *current_texture(void)
{
    struct glide_texture *t;

    if (st.texture >= num_textures) return NULL;
    t = &textures[st.texture];
    if (!t->used || !t->downloaded) return NULL;
    if (((t->format == GR_TEXFMT_P_8) || (t->format == GR_TEXFMT_AP_88)) && (t->decoded_palette != palette_serial))
    {
        decode_texture(t);
    }
    return t;
}

static void free_textures(void)
{
    uint32_t i;
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
 * and sow, tow, oow (screen-linear, as a Voodoo interpolates them) */
struct gl_vertex {
    float x, y;
    float r, g, b, a;
    float sow, tow, oow;
};

#define BATCH_MAX 3072

static SDL_Window *gl_window;
static SDL_GLContext gl_context;
static GLuint fbo[3], fbo_tex[3];
static GLuint prog_draw;
static GLint u_inv_size, u_tex, u_texscale, u_cc, u_asrc, u_usetex, u_ccolor, u_chroma, u_chroma_on;
/* the 565 shadow of a colour buffer is what the guest sees; the GL buffer is what is drawn */
static int shadow_valid[3];
static int shadow_dirty[3];         /* the guest wrote the shadow: the GL image is stale */
static int bidx[3];                 /* logical buffer -> colour_mem index */
static uint32_t *xfer;              /* scr_width * scr_height ARGB, for LFB transfers */

static struct gl_vertex batch[BATCH_MAX];
static uint32_t batch_n;
static GLenum batch_mode;

static const char draw_vs[] =
    "#version 120\n"
    "uniform vec2 inv_size;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_col;\n"
    "attribute vec3 a_stq;\n"
    "varying vec4 v_col;\n"
    "varying vec3 v_stq;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos * inv_size - 1.0, 0.0, 1.0);\n"
    "    v_col = a_col;\n"
    "    v_stq = a_stq;\n"
    "}\n";

/* the colour/alpha combine of glide2x.h, the numbers are GR_COLORCOMBINE_* */
static const char draw_fs[] =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texscale;\n"
    "uniform int u_cc;\n"
    "uniform int u_asrc;\n"
    "uniform int u_usetex;\n"
    "uniform vec4 u_ccolor;\n"
    "uniform vec3 u_chroma;\n"
    "uniform int u_chroma_on;\n"
    "varying vec4 v_col;\n"
    "varying vec3 v_stq;\n"
    "void main() {\n"
    "    vec4 col = clamp(v_col, 0.0, 1.0);\n"
    "    vec4 tex = vec4(1.0);\n"
    "    if (u_usetex == 1) {\n"
    "        float w = (v_stq.z != 0.0) ? 1.0 / v_stq.z : 0.0;\n"
    "        tex = texture2D(u_tex, v_stq.xy * w * u_texscale);\n"
    "    } else if (u_usetex == 2) {\n"
    "        tex = vec4(0.0);\n"
    "    }\n"
    "    vec3 other;\n"
    "    if (u_cc == 1 || u_cc == 13) other = u_ccolor.rgb;\n"
    "    else if (u_cc == 0 || u_cc == 2 || u_cc == 3 || u_cc == 16) other = col.rgb;\n"
    "    else other = tex.rgb;\n"
    "    if (u_chroma_on != 0 && all(lessThan(abs(other - u_chroma), vec3(0.5 / 255.0)))) discard;\n"
    "    vec3 rgb;\n"
    "    if (u_cc == 0) rgb = vec3(0.0);\n"
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
    "    if (u_asrc == 0) a = u_ccolor.a;\n"
    "    else if (u_asrc == 2) a = tex.a;\n"
    "    else if (u_asrc == 3) a = tex.a * col.a;\n"
    "    else a = col.a;\n"
    "    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), a);\n"
    "}\n";

static int gl_start(uint32_t nbuf)
{
    uint32_t i;

    gl_window = Winapi_ScreenOpenGL(scr_width, scr_height);
    if (gl_window == NULL) return 0;
    gl_context = Game_GlCreateContext(gl_window, Display_VSync);
    if (gl_context == NULL) goto fail;
    DEBUG("Glide: OpenGL %s, %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    prog_draw = Game_GlProgram(draw_vs, draw_fs, "a_pos", "a_col", "a_stq", "Glide");
    if (prog_draw == 0) goto fail;
    u_inv_size = glGetUniformLocation(prog_draw, "inv_size");
    u_tex = glGetUniformLocation(prog_draw, "u_tex");
    u_texscale = glGetUniformLocation(prog_draw, "u_texscale");
    u_cc = glGetUniformLocation(prog_draw, "u_cc");
    u_asrc = glGetUniformLocation(prog_draw, "u_asrc");
    u_usetex = glGetUniformLocation(prog_draw, "u_usetex");
    u_ccolor = glGetUniformLocation(prog_draw, "u_ccolor");
    u_chroma = glGetUniformLocation(prog_draw, "u_chroma");
    u_chroma_on = glGetUniformLocation(prog_draw, "u_chroma_on");

    for (i = 0; i < nbuf; i++)
    {
        if (!Game_GlFboCreate(&fbo[i], &fbo_tex[i], scr_width, scr_height, 0, NULL)) goto fail;
        shadow_valid[i] = 1;
        shadow_dirty[i] = 0;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
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
        for (i = 0; i < 3; i++)
        {
            Game_GlFboDelete(&fbo[i], &fbo_tex[i], 0, NULL);
        }
        for (i = 0; i < num_textures; i++)
        {
            if (textures[i].gltex != 0) glDeleteTextures(1, &textures[i].gltex);
            textures[i].gltex = 0;
        }
        if (prog_draw != 0) glDeleteProgram(prog_draw);
        SDL_GL_DeleteContext(gl_context);
    }
    memset(fbo, 0, sizeof(fbo));
    memset(fbo_tex, 0, sizeof(fbo_tex));
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

static void upload_texture(struct glide_texture *t)
{
    if (t->gltex == 0) glGenTextures(1, &t->gltex);
    glBindTexture(GL_TEXTURE_2D, t->gltex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (t->s_clamp == GR_TEXTURECLAMP_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (t->t_clamp == GR_TEXTURECLAMP_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (t->minfilter == GR_TEXTUREFILTER_BILINEAR) ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (t->magfilter == GR_TEXTUREFILTER_BILINEAR) ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->width, t->height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, t->argb);
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
    pi = gl_ok ? phys_of(st.render_buffer) : -1;
    if (pi < 0)
    {
        batch_n = 0;
        return;
    }

    use_texture = (cc >= GR_COLORCOMBINE_DECAL_TEXTURE && cc <= GR_COLORCOMBINE_DIFF_SPEC_B)
                  || (st.alpha_source == GR_ALPHASOURCE_TEXTURE_ALPHA)
                  || (st.alpha_source == GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA);
    if (use_texture && st.tex_combine != GR_TEXTURECOMBINE_ZERO && st.tex_combine != GR_TEXTURECOMBINE_ONE)
    {
        tex = current_texture();
    }

    if (shadow_dirty[pi]) shadow_to_gl(pi);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[pi]);
    glViewport(0, 0, scr_width, scr_height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(st.clip_minx, st.clip_miny, st.clip_maxx - st.clip_minx, st.clip_maxy - st.clip_miny);
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

    glUseProgram(prog_draw);
    glUniform2f(u_inv_size, 2.0f / (float)scr_width, 2.0f / (float)scr_height);
    glUniform1i(u_cc, (int)cc);
    glUniform1i(u_asrc, (int)st.alpha_source);
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
        if (tex->gltex == 0 || tex->gl_dirty) upload_texture(tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex->gltex);
        glUniform1i(u_tex, 0);
        glUniform2f(u_texscale, scale / (float)tex->width, scale / (float)tex->height);
        glUniform1i(u_usetex, 1);
    }
    else
    {
        glUniform1i(u_usetex, (st.tex_combine == GR_TEXTURECOMBINE_ZERO) ? 2 : 0);
    }

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].x);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].r);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(struct gl_vertex), &batch[0].sow);
    glDrawArrays(batch_mode, 0, batch_n);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glUseProgram(0);

    shadow_valid[pi] = 0;
    batch_n = 0;
}

static void batch_vertex(GLenum mode, const struct gr_vertex *v, int pixel_centre)
{
    struct gl_vertex *o;

    if (batch_n > 0 && (batch_mode != mode || batch_n + 3 > BATCH_MAX)) gl_flush();
    batch_mode = mode;
    o = &batch[batch_n++];
    o->x = pixel_centre ? floorf(v->x) + 0.5f : v->x;
    o->y = pixel_centre ? floorf(v->y) + 0.5f : v->y;
    o->r = v->r / 255.0f;
    o->g = v->g / 255.0f;
    o->b = v->b / 255.0f;
    o->a = v->a / 255.0f;
    o->sow = v->tmuvtx[0].sow;
    o->tow = v->tmuvtx[0].tow;
    o->oow = v->oow;
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

/* a colour buffer's GL image -> its 565 shadow */
static void shadow_from_gl(int pi)
{
    uint32_t x, y;

    if (shadow_valid[pi]) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[pi]);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, scr_width, scr_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, xfer);
    for (y = 0; y < scr_height; y++)
    {
        uint16_t *d = color_mem[pi] + y * scr_stride;
        const uint32_t *s = xfer + y * scr_width;
        for (x = 0; x < scr_width; x++) d[x] = argb_to_565(s[x]);
    }
    shadow_valid[pi] = 1;
}

/* the 565 shadow -> the GL image */
static void shadow_to_gl(int pi)
{
    uint32_t x, y;

    for (y = 0; y < scr_height; y++)
    {
        const uint16_t *s = color_mem[pi] + y * scr_stride;
        uint32_t *d = xfer + y * scr_width;
        for (x = 0; x < scr_width; x++) d[x] = rgb565_to_argb(s[x]);
    }
    glBindTexture(GL_TEXTURE_2D, fbo_tex[pi]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, scr_width, scr_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, xfer);
    shadow_valid[pi] = 1;
    shadow_dirty[pi] = 0;
}

/* the front buffer, scaled to the window with the aspect of the screen */
static void present_front(void)
{
    int pi = bidx[0];

    if (!gl_ok) return;
    gl_flush();
    if (shadow_dirty[pi]) shadow_to_gl(pi);
    Game_GlPresentTexture(gl_window, fbo_tex[pi], scr_width, scr_height,
                          (buffer_origin == GR_ORIGIN_UPPER_LEFT), (Display_ScalingQuality != 0));
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
    hw->SSTs[0].sliDetect = 0;
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
    (void)depth;
    DEBUG("grBufferClear: 0x%x, 0x%x, 0x%x\n", color, alpha, depth);
    if (!win_open || pi < 0) return 0;

    gl_flush();
    if (shadow_dirty[pi]) shadow_to_gl(pi);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[pi]);
    glEnable(GL_SCISSOR_TEST);
    glScissor(st.clip_minx, st.clip_miny, st.clip_maxx - st.clip_minx, st.clip_maxy - st.clip_miny);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(((argb >> 16) & 0xff) / 255.0f, ((argb >> 8) & 0xff) / 255.0f, (argb & 0xff) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (st.clip_minx == 0 && st.clip_miny == 0 && st.clip_maxx == (int32_t)scr_width && st.clip_maxy == (int32_t)scr_height)
    {
        /* the whole buffer is one colour: the shadow need not be read back */
        uint16_t c = argb_to_565(argb);
        uint32_t x, y;
        for (y = 0; y < scr_height; y++)
        {
            uint16_t *row = color_mem[pi] + y * scr_stride;
            for (x = 0; x < scr_width; x++) row[x] = c;
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
    if (p != NULL) draw_point((const struct gr_vertex *)p);
    return 0;
}

uint32_t CCALL grAADrawLine_c(void *v1, void *v2)
{
    if (v1 != NULL && v2 != NULL) draw_line((const struct gr_vertex *)v1, (const struct gr_vertex *)v2);
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
    (void)factor; (void)local; (void)other; (void)invert;
    st.color_combine = function;
    return 0;
}

uint32_t CCALL guColorCombineFunction_c(uint32_t function)
{
    gl_flush();
    DEBUG("guColorCombineFunction: %u\n", function);
    st.color_combine = function;
    return 0;
}

uint32_t CCALL grAlphaCombine_c(uint32_t function)
{
    gl_flush();
    DEBUG("grAlphaCombine: %u\n", function);
    st.alpha_source = function;
    return 0;
}

uint32_t CCALL grFogMode_c(uint32_t mode)
{
    gl_flush();
    DEBUG("grFogMode: %u\n", mode);
    // Note: This is a stub; real fog handling needs depth buffer
    return 0;
}

uint32_t CCALL grFogColorValue_c(uint32_t value)
{
    gl_flush();
    DEBUG("grFogColorValue: 0x%x\n", value);
    return 0;
}

uint32_t CCALL grFogTable_c(void *table)
{
    (void)table;
    gl_flush();
    DEBUG("grFogTable\n");
    return 0;
}

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
    (void)startAddress; (void)evenOdd; (void)info;
    gl_flush();
    DEBUG("grTexSource: tmu=%u\n", tmu);
    st.texture = tmu;
    return 0;
}

uint32_t CCALL grTexDownloadMipMap_c(uint32_t tmu, uint32_t startAddress, uint32_t evenOdd, void *info)
{
    (void)tmu; (void)startAddress; (void)evenOdd; (void)info;
    gl_flush();
    DEBUG("grTexDownloadMipMap\n");
    /* mmid is passed as info->largeLod.baseAddress in real Voodoo2; ignored here */
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
    (void)tmu; (void)minFilter; (void)magFilter;
    gl_flush();
    DEBUG("grTexFilterMode: tmu=%u min=%u mag=%u\n", tmu, minFilter, magFilter);
    return 0;
}

uint32_t CCALL grTexClampMode_c(uint32_t tmu, uint32_t sClamp, uint32_t tClamp)
{
    (void)tmu; (void)sClamp; (void)tClamp;
    gl_flush();
    DEBUG("grTexClampMode: tmu=%u sClamp=%u tClamp=%u\n", tmu, sClamp, tClamp);
    return 0;
}

uint32_t CCALL grTexCombine_c(uint32_t tmu, uint32_t rgb_function, uint32_t alpha_function, uint32_t rgb_invert, uint32_t alpha_invert, uint32_t rgb_factor, uint32_t alpha_factor)
{
    (void)tmu; (void)rgb_function; (void)alpha_function; (void)rgb_invert; (void)alpha_invert; (void)rgb_factor; (void)alpha_factor;
    gl_flush();
    DEBUG("grTexCombine: tmu=%u\n", tmu);
    if (tmu == 0) st.tex_combine = rgb_function;
    return 0;
}

uint32_t CCALL grTexMinAddress_c(uint32_t address)
{
    (void)address;
    gl_flush();
    DEBUG("grTexMinAddress\n");
    return 0;
}

uint32_t CCALL grTexMaxAddress_c(uint32_t address)
{
    (void)address;
    gl_flush();
    DEBUG("grTexMaxAddress\n");
    return 0;
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

    (void)tmu; (void)odd_even_mask; (void)mm_mode; (void)lod_bias; (void)trilinear;
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
    t->data_size = size;
    t->raw = (uint8_t *)malloc(size);
    t->argb = (uint32_t *)malloc(w * h * 4);
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

#ifdef __cplusplus
}
#endif
