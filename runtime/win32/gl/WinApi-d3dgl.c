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

/* The GL device behind WinApi-d3d.c (a d3d_gl=yes build).  The vertices are
 * pre-transformed (D3D screen space, y down, z 0..1, rhw = 1/w): an identity
 * ortho projection puts screen row 0 in the first row of the FBO's memory, so
 * the colour read back into the guest's surface needs no flipping and 2D the
 * guest blits into that memory stays under the 3D.  The fixed pipeline of the
 * compatibility profile does the rest: rhw is the clip w (perspective texture),
 * the texture environment the blend mode, glBlendFunc the alpha blend.
 *
 * Compromises: the depth attachment is 24 bit, not the guest's 16; the depth
 * of the guest's Z-buffer is neither uploaded nor read back (a scene starts
 * with a clear one, the game clears it every frame); a flat primitive takes
 * the first vertex's colour, a specular add is folded into it. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "Game_gl.h"
#include "WinApi-config.h"
#include "WinApi-ddraw.h"
#include "WinApi-ddraw-priv.h"
#include "WinApi-d3dgl.h"

#define RS_TEXTUREADDRESS   3
#define RS_TEXTUREPERSPECTIVE 4
#define RS_ZENABLE          7
#define RS_SHADEMODE        9
#define RS_ZWRITEENABLE     14
#define RS_SRCBLEND         19
#define RS_DESTBLEND        20
#define RS_TEXTUREMAPBLEND  21
#define RS_CULLMODE         22
#define RS_ZFUNC            23
#define RS_ALPHABLENDENABLE 27
#define RS_SPECULARENABLE   29
#define RS_COLORKEYENABLE   41

#define MAX_GL_STATES       64
#define MAX_GL_TEXTURES     1024

static SDL_GLContext gl_context = NULL;
static SDL_Window *gl_window = NULL;

static GLuint scene_fbo = 0, scene_tex = 0, scene_depth = 0;
static int scene_w = 0, scene_h = 0;
static struct IDirectDrawSurface_c *scene_target = NULL;
static int scene_active = 0;
static int scene_has_z = 0;             /* the target has a Z-buffer attached */
/* the surface read back mid-scene: only it may be uploaded back (the guest
 * wrote it after the readback; a Lock that predates the scene may not) */
static struct IDirectDrawSurface_c *scene_synced = NULL;

static GLuint present_tex = 0;
static uint32_t *present_buf = NULL;
static int present_w = 0, present_h = 0;

static uint32_t gl_state[MAX_GL_STATES];

/* the texture of a surface, and of it with the colour key in the alpha */
typedef struct {
    const struct IDirectDrawSurface_c *surface;
    uint32_t gen;
    GLuint plain, keyed;
} gl_texcache;
static gl_texcache texcache[MAX_GL_TEXTURES];

int Winapi_D3dGLContext(struct SDL_Window *window)
{
    if ((window == NULL) && (gl_window == NULL)) return 0;
    if (window != NULL) gl_window = window;
    if (gl_context == NULL)
    {
        gl_context = Game_GlCreateContext(gl_window, Display_VSync);
        if (gl_context == NULL) return 0;
    }
    return 1;
}

/* ---- the surface's pixels as the FBO's BGRA ---- */

static void gl_channel(uint32_t mask, int *shift, int *max)
{
    int sh = 0;
    if (mask == 0) { *shift = 0; *max = 0; return; }
    while (!(mask & 1)) { mask >>= 1; sh++; }
    *shift = sh;
    *max = (int)mask;
}

/* 0xAARRGGBB rows; keyed texels get alpha 0 when key != 0 */
static void surface_to_bgra(const struct IDirectDrawSurface_c *s, uint32_t *dst, int key)
{
    uint32_t x, y;
    int shift[4], max[4];
    uint32_t r = s->rmask, g = s->gmask, b = s->bmask, a = s->amask;

    if ((r | g | b) == 0) { r = 0xf800; g = 0x07e0; b = 0x001f; }
    gl_channel(r, &shift[0], &max[0]);
    gl_channel(g, &shift[1], &max[1]);
    gl_channel(b, &shift[2], &max[2]);
    gl_channel(a, &shift[3], &max[3]);

    for (y = 0; y < s->height; y++, dst += s->width)
    {
        const uint8_t *src = s->pixels + y * s->pitch;

        if (s->bpp == 8)
        {
            const uint32_t *pal = (s->palette != NULL) ? s->palette->argb : NULL;
            for (x = 0; x < s->width; x++)
            {
                uint32_t raw = src[x];
                uint32_t c = (pal != NULL) ? pal[raw] : (0xff000000u | (raw * 0x010101u));
                if (key && s->has_colorkey && (raw >= s->colorkey_low) && (raw <= s->colorkey_high)) c &= 0x00ffffffu;
                dst[x] = c;
            }
        }
        else if (s->bpp == 16)
        {
            const uint16_t *s16 = (const uint16_t *)src;
            for (x = 0; x < s->width; x++)
            {
                uint32_t raw = s16[x];
                uint32_t v[4];
                int i;
                if (key && s->has_colorkey && (raw >= s->colorkey_low) && (raw <= s->colorkey_high)) { dst[x] = 0; continue; }
                for (i = 0; i < 4; i++)
                {
                    v[i] = (max[i] == 0) ? 255 : (uint32_t)((((raw >> shift[i]) & max[i]) * 255u + max[i] / 2) / max[i]);
                }
                dst[x] = 0xff000000u | (v[0] << 16) | (v[1] << 8) | v[2];
                if (max[3] != 0) dst[x] = (v[3] << 24) | (dst[x] & 0x00ffffffu);
            }
        }
        else
        {
            memcpy(dst, src, s->width * 4);
            if (key && s->has_colorkey)
            {
                for (x = 0; x < s->width; x++)
                {
                    uint32_t c = dst[x] & 0x00ffffffu;
                    if ((c >= s->colorkey_low) && (c <= s->colorkey_high)) dst[x] = 0;
                }
            }
        }
    }
}

static void bgra_to_surface(const uint32_t *src, struct IDirectDrawSurface_c *s)
{
    uint32_t x, y;

    for (y = 0; y < s->height; y++, src += s->width)
    {
        uint8_t *dst = s->pixels + y * s->pitch;

        if (s->bpp == 16)
        {
            uint16_t *d16 = (uint16_t *)dst;
            for (x = 0; x < s->width; x++)
            {
                uint32_t c = src[x];
                uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                d16[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
        else if (s->bpp == 32)
        {
            memcpy(dst, src, s->width * 4);
        }
    }
}

/* ---- the scene's FBO ---- */

static void scene_fbo_delete(void)
{
    if ((scene_fbo == 0) && (scene_tex == 0)) return;
    Game_GlFboDelete(&scene_fbo, &scene_tex, 1, &scene_depth);
    scene_w = scene_h = 0;
}

static void scene_upload(struct IDirectDrawSurface_c *target)
{
    uint32_t *buf = (uint32_t *)malloc((size_t)scene_w * scene_h * 4);
    if (buf == NULL) return;
    surface_to_bgra(target, buf, 0);
    glBindTexture(GL_TEXTURE_2D, scene_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, scene_w, scene_h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);
    free(buf);
}

int Winapi_D3dGLBeginScene(struct IDirectDrawSurface_c *target)
{
    if ((target == NULL) || (target->pixels == NULL) || (target->dd == NULL)) return 0;
    if ((target->bpp != 16) && (target->bpp != 32)) return 0;
    if (!Winapi_D3dGLContext(target->dd->Window)) return 0;

    if ((scene_fbo == 0) || (scene_w != (int)target->width) || (scene_h != (int)target->height))
    {
        scene_fbo_delete();
        if (!Game_GlFboCreate(&scene_fbo, &scene_tex, (int)target->width, (int)target->height, 1, &scene_depth)) return 0;
        scene_w = (int)target->width;
        scene_h = (int)target->height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);

    /* the guest's picture (its clears and 2D blits) under whatever comes */
    scene_upload(target);

    /* screen row 0 of the D3D target is the first row of the FBO's memory */
    glViewport(0, 0, scene_w, scene_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)scene_w, 0.0, (GLdouble)scene_h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    scene_target = target;
    scene_has_z = (target->zsurf != NULL);
    scene_active = 1;
    return 1;
}

/* the FBO's colour into the target's memory (the scene stays active) */
static void scene_readback(struct IDirectDrawSurface_c *target)
{
    uint8_t *buf = (uint8_t *)malloc((size_t)scene_w * scene_h * 4);
    uint32_t i, n;
    if (buf == NULL) return;
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    /* GL_RGBA + GL_UNSIGNED_BYTE is the one readback every driver has;
     * GL_BGRA + GL_UNSIGNED_INT_8_8_8_8_REV may silently fail on some */
    glReadPixels(0, 0, scene_w, scene_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    {
        static int logged = 0;
        GLenum err = glGetError();
        if (!logged && (err != GL_NO_ERROR))
        {
            fprintf(stderr, "Game_D3dGL: glReadPixels: error 0x%x\n", (unsigned)err);
            logged = 1;
        }
    }
    n = (uint32_t)scene_w * (uint32_t)scene_h;
    for (i = 0; i < n; i++)
    {
        uint8_t r = buf[i * 4], b = buf[i * 4 + 2];
        buf[i * 4] = b;
        buf[i * 4 + 2] = r;
    }
    bgra_to_surface((const uint32_t *)buf, target);
    free(buf);
}

void Winapi_D3dGLEndScene(struct IDirectDrawSurface_c *target)
{
    if (!scene_active || (target == NULL) || (target != scene_target)) { scene_active = 0; return; }
    scene_active = 0;
    scene_synced = NULL;
    scene_readback(target);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    scene_target = NULL;
}

void Winapi_D3dGLSync(struct IDirectDrawSurface_c *target)
{
    /* the guest touches the target's pixels mid-scene (Lock, a blit from it):
     * its memory wants the 3D drawn so far; the scene goes on */
    if (!scene_active || (target == NULL) || (target != scene_target)) return;
    scene_readback(target);
    scene_synced = target;
}

void Winapi_D3dGLUpload(struct IDirectDrawSurface_c *target)
{
    /* the guest wrote the target mid-scene (Unlock or Blt): under the 3D;
     * only after a readback of this scene, else the write predates it */
    if (!scene_active || (target == NULL) || (target != scene_target)) return;
    if (scene_synced != target) return;
    scene_synced = NULL;
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    scene_upload(target);
}

void Winapi_D3dGLClear(struct IDirectDrawSurface_c *target,
                       int x1, int y1, int x2, int y2,
                       int do_color, uint32_t color, int do_z)
{
    if (!scene_active || (target == NULL) || (target != scene_target)) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > scene_w) x2 = scene_w;
    if (y2 > scene_h) y2 = scene_h;
    if ((x1 >= x2) || (y1 >= y2)) return;

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);

    /* the scissor's origin is the FBO's bottom, and the rows here are the
     * screen's top down */
    glEnable(GL_SCISSOR_TEST);
    glScissor(x1, scene_h - y2, x2 - x1, y2 - y1);

    if (do_color)
    {
        glClearColor((float)((color >> 16) & 255) / 255.0f,
                     (float)((color >> 8) & 255) / 255.0f,
                     (float)(color & 255) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (do_z && scene_has_z)
    {
        glClearDepth(1.0);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }
    glDisable(GL_SCISSOR_TEST);
}

/* ---- the render states, applied when a primitive comes ---- */

void Winapi_D3dGLSetState(uint32_t state, uint32_t value)
{
    if (state < MAX_GL_STATES) gl_state[state] = value;
}

static GLenum gl_zfunc(uint32_t func)
{
    /* as z_test() of the software rasteriser: 0 and 1 never, 2..7 per D3D */
    static const GLenum f[8] = { GL_ALWAYS, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL };
    return (func < 8) ? f[func] : GL_ALWAYS;
}

static void gl_blend_factor(uint32_t mode, GLenum *factor)
{
    switch (mode)
    {
        case 1: *factor = GL_ZERO; break;
        case 2: *factor = GL_ONE; break;
        case 3: *factor = GL_SRC_COLOR; break;
        case 4: *factor = GL_ONE_MINUS_SRC_COLOR; break;
        case 5: *factor = GL_SRC_ALPHA; break;
        case 6: *factor = GL_ONE_MINUS_SRC_ALPHA; break;
        case 7: *factor = GL_DST_ALPHA; break;
        case 8: *factor = GL_ONE_MINUS_DST_ALPHA; break;
        case 9: *factor = GL_DST_COLOR; break;
        case 10: *factor = GL_ONE_MINUS_DST_COLOR; break;
        case 11: *factor = GL_SRC_ALPHA_SATURATE; break;
        case 12: *factor = GL_SRC_ALPHA; break;             /* BOTHSRCALPHA */
        case 13: *factor = GL_ONE_MINUS_SRC_ALPHA; break;   /* BOTHINVSRCALPHA */
        default: *factor = GL_ONE; break;
    }
}

/* the texture of a surface: the plain one and the one with the key in alpha */
static GLuint gl_texture_of(const struct IDirectDrawSurface_c *s, int keyed)
{
    gl_texcache *e = NULL;
    uint32_t i;

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        if (texcache[i].surface == s) { e = &texcache[i]; break; }
        if ((e == NULL) && (texcache[i].surface == NULL)) e = &texcache[i];
    }
    if (e == NULL) return 0;

    if (e->surface != s)
    {
        memset(e, 0, sizeof(*e));
        e->surface = s;
        glGenTextures(1, &e->plain);
        glGenTextures(1, &e->keyed);
    }
    if ((e->plain == 0) || (e->gen != s->texture_gen))
    {
        uint32_t *buf = (uint32_t *)malloc((size_t)s->width * s->height * 4);
        if (buf == NULL) return 0;

        surface_to_bgra(s, buf, 0);
        glBindTexture(GL_TEXTURE_2D, e->plain);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);

        if (s->has_colorkey)
        {
            surface_to_bgra(s, buf, 1);
            glBindTexture(GL_TEXTURE_2D, e->keyed);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, e->keyed);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);
        }
        free(buf);

        /* the wrap of RS_TEXTUREADDRESS is set per draw */
        e->gen = s->texture_gen;
    }
    return keyed ? e->keyed : e->plain;
}

void Winapi_D3dGLPaletteChanged(const struct IDirectDrawPalette_c *palette)
{
    uint32_t i;

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        struct IDirectDrawSurface_c *s = (struct IDirectDrawSurface_c *)texcache[i].surface;
        if ((s != NULL) && (s->palette == palette)) s->texture_gen++;
    }
}

void Winapi_D3dGLTextureFree(const struct IDirectDrawSurface_c *s)
{
    uint32_t i;

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        if (texcache[i].surface == s)
        {
            if (gl_context != NULL)
            {
                glDeleteTextures(1, &texcache[i].plain);
                glDeleteTextures(1, &texcache[i].keyed);
            }
            memset(&texcache[i], 0, sizeof(texcache[i]));
        }
    }
}

/* ---- the primitives ---- */

static void gl_apply_draw_state(int textured)
{
    /* the depth test only with a Z-buffer, as in the software rasteriser */
    if (scene_has_z && (gl_state[RS_ZENABLE] != 0))
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(gl_zfunc(gl_state[RS_ZFUNC]));
        glDepthMask((gl_state[RS_ZWRITEENABLE] != 0) ? GL_TRUE : GL_FALSE);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    if (gl_state[RS_ALPHABLENDENABLE] != 0)
    {
        GLenum sfac, dfac;
        gl_blend_factor(gl_state[RS_SRCBLEND], &sfac);
        gl_blend_factor(gl_state[RS_DESTBLEND], &dfac);
        if (gl_state[RS_SRCBLEND] == 12) dfac = GL_ONE_MINUS_SRC_ALPHA;
        if (gl_state[RS_SRCBLEND] == 13) dfac = GL_SRC_ALPHA;
        glEnable(GL_BLEND);
        glBlendFunc(sfac, dfac);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    /* the culling of the software rasteriser is kept on the CPU, y down */
    glDisable(GL_CULL_FACE);

    glShadeModel((gl_state[RS_SHADEMODE] == 1) ? GL_FLAT : GL_SMOOTH);

    if (textured)
    {
        glEnable(GL_TEXTURE_2D);
        /* the software rasteriser's blend of the texel and the vertex:
         * MODULATE keeps the texel's alpha (not the vertex's, so the colour
         * key of the keyed texture still tests as 0), MODULATEALPHA multiplies
         * it, DECALALPHA mixes the colour by the texel's alpha */
        switch (gl_state[RS_TEXTUREMAPBLEND])
        {
            case 1: case 7: glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); break;
            case 3:
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
                break;
            case 4:
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
                break;
            default:
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
                break;
        }
        if (gl_state[RS_COLORKEYENABLE] != 0)
        {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
        }
        else
        {
            glDisable(GL_ALPHA_TEST);
        }
    }
    else
    {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
    }
}

static void gl_vertex(const d3dgl_vert *v, int persp, const d3dgl_vert *flatfrom)
{
    /* glOrtho(0,W,0,H,-1,1) maps eye z -1..1 to the depth 0..1: eye z =
     * 2*guest_z - 1 keeps the guest's 0 (near) at depth 0 and 1 (far) at the
     * cleared depth 1, as the software rasteriser's z_test() */
    if (flatfrom != NULL) glColor4ub((GLubyte)flatfrom->r, (GLubyte)flatfrom->g, (GLubyte)flatfrom->b, (GLubyte)flatfrom->a);
    else glColor4ub((GLubyte)v->r, (GLubyte)v->g, (GLubyte)v->b, (GLubyte)v->a);
    if (persp) glTexCoord4f(v->u * v->rhw, v->v * v->rhw, 0.0f, v->rhw);
    else glTexCoord2f(v->u, v->v);
    glVertex3f(v->x, v->y, v->z * 2.0f - 1.0f);
}

static void gl_triangle(const d3dgl_vert *v0, const d3dgl_vert *v1, const d3dgl_vert *v2, int textured, int flat)
{
    float area;
    int persp;

    area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
    if (area == 0.0f) return;
    if (((gl_state[RS_CULLMODE] == 2) && (area > 0)) || ((gl_state[RS_CULLMODE] == 3) && (area < 0))) return;

    persp = textured && (gl_state[RS_TEXTUREPERSPECTIVE] != 0) &&
            (v0->rhw > 0.0f) && (v1->rhw > 0.0f) && (v2->rhw > 0.0f);

    glBegin(GL_TRIANGLES);
    gl_vertex(v0, persp, flat ? v0 : NULL);
    gl_vertex(v1, persp, flat ? v0 : NULL);
    gl_vertex(v2, persp, flat ? v0 : NULL);
    glEnd();
}

static const d3dgl_vert *gl_ith(const d3dgl_vert *verts, uint32_t nverts, const uint16_t *index, uint32_t i)
{
    uint32_t k = (index != NULL) ? index[i] : i;
    if (index != NULL && k >= nverts) k = 0;
    return &verts[k];
}

void Winapi_D3dGLDraw(uint32_t type, const d3dgl_vert *verts, uint32_t nverts,
                      const uint16_t *index, uint32_t count,
                      const struct IDirectDrawSurface_c *texture,
                      int clip_x, int clip_y, int clip_w, int clip_h)
{
    GLuint tex = 0;
    int textured, flat, i;
    uint32_t n;

    if (!scene_active || (verts == NULL)) return;
    n = (index != NULL) ? count : nverts;
    if (n == 0) return;
    if ((texture != NULL) && (texture->pixels != NULL) && (texture->width != 0))
    {
        tex = gl_texture_of(texture, gl_state[RS_COLORKEYENABLE] != 0);
        textured = (tex != 0);
    }
    else textured = 0;
    flat = (gl_state[RS_SHADEMODE] == 1);

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);

    if ((clip_w > 0) && (clip_h > 0))
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(clip_x, scene_h - (clip_y + clip_h), clip_w, clip_h);
    }

    gl_apply_draw_state(textured);
    if (textured)
    {
        uint32_t addr = gl_state[RS_TEXTUREADDRESS];
        GLint wrap = (addr == 3) ? GL_CLAMP_TO_EDGE : ((addr == 2) ? GL_MIRRORED_REPEAT : GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    }

    if (type >= 4)
    {
        switch (type)
        {
            case 4:
                for (i = 0; i + 2 < (int)n; i += 3)
                {
                    gl_triangle(gl_ith(verts, nverts, index, (uint32_t)i),
                                gl_ith(verts, nverts, index, (uint32_t)i + 1),
                                gl_ith(verts, nverts, index, (uint32_t)i + 2), textured, flat);
                }
                break;
            case 5:
                for (i = 0; i + 2 < (int)n; i++)
                {
                    const d3dgl_vert *a = gl_ith(verts, nverts, index, (uint32_t)i);
                    const d3dgl_vert *b = gl_ith(verts, nverts, index, (uint32_t)i + 1);
                    const d3dgl_vert *c = gl_ith(verts, nverts, index, (uint32_t)i + 2);
                    if (i & 1) gl_triangle(b, a, c, textured, flat);
                    else gl_triangle(a, b, c, textured, flat);
                }
                break;
            case 6:
                for (i = 1; i + 1 < (int)n; i++)
                {
                    gl_triangle(gl_ith(verts, nverts, index, 0),
                                gl_ith(verts, nverts, index, (uint32_t)i),
                                gl_ith(verts, nverts, index, (uint32_t)i + 1), textured, flat);
                }
                break;
            default: break;
        }
    }
    else
    {
        /* lines: the colour of each vertex, untextured, as in software */
        glBegin((type == 3) ? GL_LINE_STRIP : GL_LINES);
        for (i = 0; i < (int)n; i++)
        {
            const d3dgl_vert *v = gl_ith(verts, nverts, index, (uint32_t)i);
            glColor4ub((GLubyte)v->r, (GLubyte)v->g, (GLubyte)v->b, 255);
            glVertex3f(v->x, v->y, v->z * 2.0f - 1.0f);
        }
        glEnd();
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_TEXTURE_2D);
}

/* ---- the image of the target's memory on the window ---- */

void Winapi_D3dGLPresent(struct IDirectDrawSurface_c *target, struct SDL_Window *window)
{
    if ((target == NULL) || (window == NULL)) return;
    if (!Winapi_D3dGLContext(window)) return;

    if ((present_tex == 0) || (present_w != (int)target->width) || (present_h != (int)target->height))
    {
        if (present_tex != 0) glDeleteTextures(1, &present_tex);
        free(present_buf);
        present_w = (int)target->width;
        present_h = (int)target->height;
        present_buf = (uint32_t *)malloc((size_t)present_w * present_h * 4);
        if (present_buf == NULL) { present_w = present_h = 0; return; }
        glGenTextures(1, &present_tex);
        glBindTexture(GL_TEXTURE_2D, present_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, present_w, present_h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    }

    Winapi_SurfaceToBgra(target, present_buf, present_w * 4);
    glBindTexture(GL_TEXTURE_2D, present_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, present_w, present_h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, present_buf);
    Game_GlPresentTexture(window, present_tex, present_w, present_h, 1, Display_ScalingQuality != 0);
}

void Winapi_D3dGLShutdown(void)
{
    uint32_t i;

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        if ((gl_context != NULL) && (texcache[i].surface != NULL))
        {
            glDeleteTextures(1, &texcache[i].plain);
            glDeleteTextures(1, &texcache[i].keyed);
        }
        memset(&texcache[i], 0, sizeof(texcache[i]));
    }
    if (present_tex != 0)
    {
        if (gl_context != NULL) glDeleteTextures(1, &present_tex);
        present_tex = 0;
    }
    free(present_buf);
    present_buf = NULL;
    present_w = present_h = 0;
    scene_fbo_delete();
    scene_active = 0;
    scene_has_z = 0;
    scene_target = NULL;
    if (gl_context != NULL)
    {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }
    gl_window = NULL;
}
