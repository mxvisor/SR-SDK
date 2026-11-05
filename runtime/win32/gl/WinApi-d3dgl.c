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
 * guest blits into that memory stays under the 3D.  One shader does the rest
 * (OpenGL ES has no fixed pipeline): rhw divides the texture coordinates per
 * fragment, u_mode is the blend mode, u_afunc the alpha test, u_fog the vertex
 * fog (its factor rides in a_pos.w), glBlendFunc the alpha blend.
 *
 * Antialiasing (the game's RS_ANTIALIAS, or Display_Antialiasing of the
 * config file): the scene is drawn into a multisampled FBO of the same size
 * instead, the guest's picture goes into it as a quad of scene_tex, and every
 * readback resolves it into scene_fbo first; the edges come out smoothed at
 * the resolution of the target.
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
#include "Game_hooks.h"
#include "Game_gl.h"
#include "WinApi-config.h"
#include "WinApi-ddraw.h"
#include "WinApi-ddraw-priv.h"
#include "WinApi-d3d.h"
#include "WinApi-d3dgl.h"
#include "Game_osd.h"

#define RS_ANTIALIAS        2
#define RS_TEXTUREADDRESS   3
#define RS_TEXTUREPERSPECTIVE 4
#define RS_ZENABLE          7
#define RS_SHADEMODE        9
#define RS_ZWRITEENABLE     14
#define RS_ALPHATESTENABLE  15
#define RS_TEXTUREMIN       18
#define RS_TEXTUREMAG       17
#define RS_SRCBLEND         19
#define RS_DESTBLEND        20
#define RS_TEXTUREMAPBLEND  21
#define RS_CULLMODE         22
#define RS_ZFUNC            23
#define RS_ALPHAREF         24
#define RS_ALPHAFUNC        25
#define RS_ALPHABLENDENABLE 27
#define RS_FOGENABLE        28
#define RS_SPECULARENABLE   29
#define RS_FOGCOLOR         34
#define RS_FOGTABLEMODE     35
#define RS_COLORKEYENABLE   41

/* a toggle of the port that hides the fog the program turns on (Game_hooks.h) */
#ifndef GAME_D3D_FOG_HIDDEN
#define GAME_D3D_FOG_HIDDEN 0
#endif
/* a fog table of the port by the W of the vertex (Game_hooks.h, WinApi-d3d.c): drawn whatever
 * RS_FOGENABLE says */
#ifndef GAME_D3D_FOG_TABLE_W
#define GAME_D3D_FOG_TABLE_W NULL
#endif

#define MAX_GL_STATES       64
/* a game may load more than a thousand small textures */
#define MAX_GL_TEXTURES     4096

static SDL_GLContext gl_context = NULL;
static SDL_Window *gl_window = NULL;

static GLuint scene_fbo = 0, scene_tex = 0, scene_depth = 0;
static int scene_w = 0, scene_h = 0;
/* the multisampled frame buffer of an antialiased scene (ms_samples 0: none) */
static GLuint ms_fbo = 0, ms_color = 0, ms_depth = 0;
static int ms_samples = 0;
static int scene_ms = 0;                /* the scene draws into ms_fbo */
static struct IDirectDrawSurface_c *scene_target = NULL;
static int scene_active = 0;

/* A program may draw from another thread than the one that opened the window (a driver
 * that makes its own): the context is current in one thread at a time, so a call takes it
 * and the end of a scene or a present gives it back. */
static void gl_acquire(void)
{
    if ((gl_context != NULL) && (SDL_GL_GetCurrentContext() != gl_context)) SDL_GL_MakeCurrent(gl_window, gl_context);
}

static void gl_release(void)
{
    if ((gl_context != NULL) && !scene_active && (SDL_GL_GetCurrentContext() == gl_context)) SDL_GL_MakeCurrent(gl_window, NULL);
}

static int scene_has_z = 0;             /* the target has a Z-buffer attached */
/* the surface read back mid-scene: only it may be uploaded back (the guest
 * wrote it after the readback; a Lock that predates the scene may not) */
static struct IDirectDrawSurface_c *scene_synced = NULL;

static GLuint present_tex = 0;
static uint32_t *present_buf = NULL;
static int present_w = 0, present_h = 0;

static uint32_t gl_state[MAX_GL_STATES];

static GLuint draw_prog = 0;
static GLint u_scale, u_mode, u_alphatest, u_afunc, u_aref, u_fog, u_fogcolor, u_clampuv;
static GLint a_box = -1;

/* what a primitive call gives is one glDrawArrays; f is the vertex fog, 1 no fog;
 * bu0..bv1 the box of the texture coordinates of the vertex's triangle */
typedef struct {
    float x, y, z, f;
    float s, t, q;
    uint8_t r, g, b, a;
    float bu0, bv0, bu1, bv1;
} gl_vertex;
static gl_vertex *batch = NULL;
static uint32_t batch_n = 0, batch_max = 0;

static const char draw_vs[] =
    "uniform vec2 u_scale;\n"
    "attribute vec4 a_pos;\n"
    "attribute vec3 a_stq;\n"
    "attribute vec4 a_col;\n"
    "attribute vec4 a_box;\n"
    "varying vec3 v_stq;\n"
    "varying vec4 v_col;\n"
    "varying float v_fog;\n"
    "varying vec4 v_box;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.xy * u_scale - 1.0, a_pos.z * 2.0 - 1.0, 1.0);\n"
    "    v_stq = a_stq;\n"
    "    v_box = a_box;\n"
    "    v_col = a_col;\n"
    "    v_fog = a_pos.w;\n"
    "}\n";

/* u_mode: 0 no texture, 1 the texel, 2 DECALALPHA, 3 MODULATEALPHA, 4 MODULATE
 * (keeps the texel's alpha, so the colour key still tests as 0); u_afunc: the
 * D3DCMPFUNC of the alpha test against u_aref (0..255), 0 none; u_fog: the
 * colour goes to u_fogcolor as v_fog goes to 0; u_clampuv: the texture
 * coordinates are kept in the box of the triangle's (a multisampled pixel on
 * an edge is shaded at its centre, which may lie outside the triangle: the
 * coordinates there would reach the neighbouring picture of a texture page) */
static const char draw_fs[] =
    "uniform sampler2D u_tex;\n"
    "uniform int u_mode;\n"
    "uniform int u_alphatest;\n"
    "uniform int u_afunc;\n"
    "uniform float u_aref;\n"
    "uniform int u_fog;\n"
    "uniform vec3 u_fogcolor;\n"
    "uniform int u_clampuv;\n"
    "varying vec3 v_stq;\n"
    "varying vec4 v_col;\n"
    "varying float v_fog;\n"
    "varying vec4 v_box;\n"
    "void main() {\n"
    "    vec4 c = v_col;\n"
    "    if (u_mode != 0) {\n"
    "        vec2 uv = v_stq.xy / v_stq.z;\n"
    "        if (u_clampuv != 0) uv = clamp(uv, v_box.xy, v_box.zw);\n"
    "        vec4 t = texture2D(u_tex, uv);\n"
    "        if (u_mode == 1) c = t;\n"
    "        else if (u_mode == 2) c = vec4(mix(v_col.rgb, t.rgb, t.a), t.a);\n"
    "        else if (u_mode == 3) c = t * v_col;\n"
    "        else c = vec4(t.rgb * v_col.rgb, t.a);\n"
    "    }\n"
    "    if (u_alphatest != 0 && c.a <= 0.5) discard;\n"
    "    if (u_afunc != 0) {\n"
    "        float a = floor(c.a * 255.0 + 0.5);\n"
    "        if ((u_afunc == 1) || (u_afunc == 2 && a >= u_aref) || (u_afunc == 3 && a != u_aref) ||\n"
    "            (u_afunc == 4 && a > u_aref) || (u_afunc == 5 && a <= u_aref) ||\n"
    "            (u_afunc == 6 && a == u_aref) || (u_afunc == 7 && a < u_aref)) discard;\n"
    "    }\n"
    "    if (u_fog != 0) c.rgb = mix(u_fogcolor, c.rgb, clamp(v_fog, 0.0, 1.0));\n"
    "    gl_FragColor = c;\n"
    "}\n";

/* the texture of a surface, and of it with the colour key in the alpha */
typedef struct {
    const struct IDirectDrawSurface_c *surface;
    uint32_t gen;
    int levels;                 /* the levels uploaded, 1 when the surface has no mip chain */
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
        draw_prog = Game_GlProgram(draw_vs, draw_fs, "a_pos", "a_stq", "a_col", "Game_D3dGL");
        if (draw_prog == 0) return 0;
        u_scale = glGetUniformLocation(draw_prog, "u_scale");
        u_mode = glGetUniformLocation(draw_prog, "u_mode");
        u_alphatest = glGetUniformLocation(draw_prog, "u_alphatest");
        u_afunc = glGetUniformLocation(draw_prog, "u_afunc");
        u_aref = glGetUniformLocation(draw_prog, "u_aref");
        u_fog = glGetUniformLocation(draw_prog, "u_fog");
        u_fogcolor = glGetUniformLocation(draw_prog, "u_fogcolor");
        u_clampuv = glGetUniformLocation(draw_prog, "u_clampuv");
        a_box = glGetAttribLocation(draw_prog, "a_box");
    }
    if (draw_prog == 0) return 0;
    gl_acquire();
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

static void ms_fbo_delete(void)
{
    if (ms_fbo == 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &ms_fbo);
    glDeleteRenderbuffers(1, &ms_color);
    glDeleteRenderbuffers(1, &ms_depth);
    ms_fbo = ms_color = ms_depth = 0;
    ms_samples = 0;
}

static void scene_fbo_delete(void)
{
    ms_fbo_delete();
    if ((scene_fbo == 0) && (scene_tex == 0)) return;
    Game_GlFboDelete(&scene_fbo, &scene_tex, 1, &scene_depth);
    scene_w = scene_h = 0;
}

/* the multisampled FBO of the scene's size with up to samples per pixel (the
 * driver's maximum); 0 if the driver makes none */
static int ms_fbo_create(int samples)
{
    GLint max = 0;

    glGetIntegerv(GL_MAX_SAMPLES, &max);
    if (samples > max) samples = max;
    if (samples < 2) return 0;

    glGenFramebuffers(1, &ms_fbo);
    glGenRenderbuffers(1, &ms_color);
    glGenRenderbuffers(1, &ms_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, ms_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, scene_w, scene_h);
    glBindRenderbuffer(GL_RENDERBUFFER, ms_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, scene_w, scene_h);
    glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ms_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ms_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        fprintf(stderr, "Game_D3dGL: no multisampled frame buffer (%d samples)\n", samples);
        ms_fbo_delete();
        return 0;
    }
    ms_samples = samples;
    fprintf(stderr, "Game_D3dGL: antialiasing, %d samples\n", samples);
    return 1;
}

/* the frame buffer the scene draws into */
static void scene_bind(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, scene_ms ? ms_fbo : scene_fbo);
}

/* scene_tex as a quad over the multisampled FBO: rows and texels as they are
 * (row 0 of both is the screen's top), no depth, no blend */
static void ms_put_scene_tex(void)
{
    static const gl_vertex quad_template[4] = {
        { 0, 0, 0, 1, 0, 0, 1, 255, 255, 255, 255, 0, 0, 1, 1 },
        { 1, 0, 0, 1, 1, 0, 1, 255, 255, 255, 255, 0, 0, 1, 1 },
        { 0, 1, 0, 1, 0, 1, 1, 255, 255, 255, 255, 0, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 255, 255, 255, 255, 0, 0, 1, 1 },
    };
    gl_vertex quad[4];
    int i;

    for (i = 0; i < 4; i++)
    {
        quad[i] = quad_template[i];
        quad[i].x *= (float)scene_w;
        quad[i].y *= (float)scene_h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    glViewport(0, 0, scene_w, scene_h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(draw_prog);
    glUniform2f(u_scale, 2.0f / (float)scene_w, 2.0f / (float)scene_h);
    glUniform1i(u_mode, 1);
    glUniform1i(u_alphatest, 0);
    glUniform1i(u_afunc, 0);
    glUniform1i(u_fog, 0);
    glUniform1i(u_clampuv, 0);
    glBindTexture(GL_TEXTURE_2D, scene_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &quad[0].x);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &quad[0].s);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_vertex), &quad[0].r);
    if (a_box >= 0)
    {
        glEnableVertexAttribArray((GLuint)a_box);
        glVertexAttribPointer((GLuint)a_box, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &quad[0].bu0);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    if (a_box >= 0) glDisableVertexAttribArray((GLuint)a_box);
    glUseProgram(0);
}

static void scene_upload(struct IDirectDrawSurface_c *target)
{
    uint32_t *buf = (uint32_t *)malloc((size_t)scene_w * scene_h * 4);
    if (buf == NULL) return;
    surface_to_bgra(target, buf, 0);
    glBindTexture(GL_TEXTURE_2D, scene_tex);
    Game_GlTexSubImageBgra(scene_w, scene_h, buf);
    free(buf);
    if (scene_ms) ms_put_scene_tex();
    scene_bind();
}

/* the samples the scene wants: the setting's, else 4 when the game asks */
static int scene_samples(void)
{
    int n = Winapi_Direct3DAntialiasing();
    if (n == 0 && gl_state[RS_ANTIALIAS] != 0) n = 4;
    return n;
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

    /* the multisampled FBO when the scene wants one (made again when the
     * number changes); without it the plain one */
    {
        int want = scene_samples();
        static int failed_for = -1;

        if ((want < 2) || (want != ms_samples)) ms_fbo_delete();
        if ((want >= 2) && (ms_fbo == 0) && (failed_for != want))
        {
            if (!ms_fbo_create(want)) failed_for = want;
        }
        scene_ms = (ms_fbo != 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    /* the guest's picture (its clears and 2D blits) under whatever comes;
     * the scene's frame buffer is bound after it */
    scene_upload(target);

    /* screen row 0 of the D3D target is the first row of the FBO's memory */
    glViewport(0, 0, scene_w, scene_h);

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
    if (scene_ms)
    {
        /* the samples resolved into scene_fbo, whose colour is read */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene_fbo);
        glBlitFramebuffer(0, 0, scene_w, scene_h, 0, 0, scene_w, scene_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
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
    scene_bind();
}

void Winapi_D3dGLEndScene(struct IDirectDrawSurface_c *target)
{
    if (!scene_active || (target == NULL) || (target != scene_target)) { scene_active = 0; return; }
    scene_active = 0;
    scene_synced = NULL;
    scene_readback(target);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    scene_target = NULL;
    gl_release();
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

    scene_bind();

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

/* RS_TEXTUREMIN: D3DFILTER (DirectX 5) 1 NEAREST, 2 LINEAR, 3 MIPNEAREST,
 * 4 MIPLINEAR, 5 LINEARMIPNEAREST, 6 LINEARMIPLINEAR (trilinear) */
static GLint gl_min_filter(uint32_t mn, int levels)
{
    int texel_linear = (mn == 2) || (mn == 4) || (mn == 6);

    if (levels < 2) return texel_linear ? GL_LINEAR : GL_NEAREST;   /* a mip filter would sample black */
    switch (mn)
    {
        case 3: return GL_NEAREST_MIPMAP_NEAREST;
        case 4: return GL_LINEAR_MIPMAP_NEAREST;
        case 5: return GL_NEAREST_MIPMAP_LINEAR;
        case 6: return GL_LINEAR_MIPMAP_LINEAR;
        default: return texel_linear ? GL_LINEAR : GL_NEAREST;
    }
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
        const struct IDirectDrawSurface_c *lv;
        uint32_t *buf = (uint32_t *)malloc((size_t)s->width * s->height * 4);
        int level = 0;

        if (buf == NULL) return 0;

        /* the texture of the game is a chain: every level is its own surface
         * (the game filled it), and the mip filter picks one of them */
        for (lv = s; lv != NULL; lv = lv->lpmip, level++)
        {
            surface_to_bgra(lv, buf, 0);
            glBindTexture(GL_TEXTURE_2D, e->plain);
            if (level == 0)
            {
                /* the default MIN_FILTER is a mipmap one; the levels alone make
                 * the texture complete, the filter itself is set per draw */
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            Game_GlTexImageBgra(level, (int)lv->width, (int)lv->height, buf);

            /* the keyed texture: with the key in the alpha when the level has one,
             * the plain pixels otherwise (a keyed draw of a keyless level) */
            surface_to_bgra(lv, buf, 1);
            glBindTexture(GL_TEXTURE_2D, e->keyed);
            if (level == 0)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            Game_GlTexImageBgra(level, (int)lv->width, (int)lv->height, buf);
        }
        glBindTexture(GL_TEXTURE_2D, e->plain);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level - 1);
        glBindTexture(GL_TEXTURE_2D, e->keyed);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level - 1);
        free(buf);

        /* the wrap of RS_TEXTUREADDRESS and the filters are set per draw */
        e->levels = level;
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
                gl_acquire();
                glDeleteTextures(1, &texcache[i].plain);
                glDeleteTextures(1, &texcache[i].keyed);
                gl_release();
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

    glUseProgram(draw_prog);
    glUniform2f(u_scale, 2.0f / (float)scene_w, 2.0f / (float)scene_h);
    /* the alpha test (D3DCMP_ALWAYS, 8, and unknown functions draw everything) */
    glUniform1i(u_afunc, ((gl_state[RS_ALPHATESTENABLE] != 0) && (gl_state[RS_ALPHAFUNC] >= 1) && (gl_state[RS_ALPHAFUNC] <= 7))
                         ? (GLint)gl_state[RS_ALPHAFUNC] : 0);
    glUniform1f(u_aref, (float)(gl_state[RS_ALPHAREF] & 255));
    /* the vertex fog, as the software rasteriser: a fog table is not done */
    glUniform1i(u_fog, (((gl_state[RS_FOGENABLE] != 0) && (gl_state[RS_FOGTABLEMODE] == 0)) || (GAME_D3D_FOG_TABLE_W != NULL)) && !GAME_D3D_FOG_HIDDEN);
    glUniform3f(u_fogcolor, (float)((gl_state[RS_FOGCOLOR] >> 16) & 255) / 255.0f,
                (float)((gl_state[RS_FOGCOLOR] >> 8) & 255) / 255.0f, (float)(gl_state[RS_FOGCOLOR] & 255) / 255.0f);
    if (textured)
    {
        int mode;

        switch (gl_state[RS_TEXTUREMAPBLEND])
        {
            case 1: case 7: mode = 1; break;
            case 3: mode = 2; break;
            case 4: mode = 3; break;
            default: mode = 4; break;
        }
        glUniform1i(u_mode, mode);
        glUniform1i(u_alphatest, gl_state[RS_COLORKEYENABLE] != 0);
    }
    else
    {
        glUniform1i(u_mode, 0);
        glUniform1i(u_alphatest, 0);
    }
}

static void gl_put(const d3dgl_vert *v, float s_coord, float t_coord, float q, const d3dgl_vert *colour, int alpha)
{
    gl_vertex *o;

    if (batch_n == batch_max)
    {
        uint32_t max = batch_max ? batch_max * 2 : 4096;
        gl_vertex *grown = (gl_vertex *)realloc(batch, max * sizeof(*batch));
        if (grown == NULL) return;
        batch = grown;
        batch_max = max;
    }
    o = &batch[batch_n++];
    /* the guest's z 0 (near) at depth 0 and 1 (far) at the cleared depth 1, as
     * the software rasteriser's z_test() */
    o->x = v->x;
    o->y = v->y;
    o->z = v->z;
    o->f = (float)v->f / 255.0f;
    o->s = s_coord;
    o->t = t_coord;
    o->q = q;
    o->r = (uint8_t)colour->r;
    o->g = (uint8_t)colour->g;
    o->b = (uint8_t)colour->b;
    o->a = (uint8_t)alpha;
    o->bu0 = o->bv0 = o->bu1 = o->bv1 = 0.0f;
}

/* the box of the last three vertices' texture coordinates (a triangle) */
static void gl_box_last3(const d3dgl_vert *v0, const d3dgl_vert *v1, const d3dgl_vert *v2)
{
    float u0 = fminf(v0->u, fminf(v1->u, v2->u)), u1 = fmaxf(v0->u, fmaxf(v1->u, v2->u));
    float w0 = fminf(v0->v, fminf(v1->v, v2->v)), w1 = fmaxf(v0->v, fmaxf(v1->v, v2->v));
    uint32_t i;

    if (batch_n < 3) return;
    for (i = batch_n - 3; i < batch_n; i++)
    {
        batch[i].bu0 = u0;
        batch[i].bv0 = w0;
        batch[i].bu1 = u1;
        batch[i].bv1 = w1;
    }
}

static void gl_vertex_of(const d3dgl_vert *v, int persp, const d3dgl_vert *flatfrom)
{
    const d3dgl_vert *colour = (flatfrom != NULL) ? flatfrom : v;

    if (persp) gl_put(v, v->u * v->rhw, v->v * v->rhw, v->rhw, colour, colour->a);
    else gl_put(v, v->u, v->v, 1.0f, colour, colour->a);
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

    gl_vertex_of(v0, persp, flat ? v0 : NULL);
    gl_vertex_of(v1, persp, flat ? v0 : NULL);
    gl_vertex_of(v2, persp, flat ? v0 : NULL);
    gl_box_last3(v0, v1, v2);
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

    scene_bind();

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
        int levels = 1;
        const struct IDirectDrawSurface_c *lv;

        for (lv = texture->lpmip; lv != NULL; lv = lv->lpmip) levels++;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_min_filter(gl_state[RS_TEXTUREMIN], levels));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ((gl_state[RS_TEXTUREMAG] >= 2) && ((gl_state[RS_TEXTUREMAG] & 1) == 0)) ? GL_LINEAR : GL_NEAREST);
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
        for (i = 0; i < (int)n; i++)
        {
            const d3dgl_vert *v = gl_ith(verts, nverts, index, (uint32_t)i);
            gl_put(v, 0.0f, 0.0f, 1.0f, v, 255);
        }
    }

    if (batch_n != 0)
    {
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &batch[0].x);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &batch[0].s);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_vertex), &batch[0].r);
        if (a_box >= 0)
        {
            glEnableVertexAttribArray((GLuint)a_box);
            glVertexAttribPointer((GLuint)a_box, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), &batch[0].bu0);
        }
        /* only a multisampled scene shades outside the triangles */
        glUniform1i(u_clampuv, scene_ms && (type >= 4));
        glDrawArrays((type >= 4) ? GL_TRIANGLES : ((type == 3) ? GL_LINE_STRIP : GL_LINES), 0, (GLsizei)batch_n);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        if (a_box >= 0) glDisableVertexAttribArray((GLuint)a_box);
        batch_n = 0;
    }

    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
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
        Game_GlTexImageBgra(0, present_w, present_h, NULL);
    }

    Winapi_SurfaceToBgra(target, present_buf, present_w * 4);
    glBindTexture(GL_TEXTURE_2D, present_tex);
    Game_GlTexSubImageBgra(present_w, present_h, present_buf);
    Game_OsdFrame(present_w, present_h, Winapi_Direct3DGL() ? "Direct3D, OpenGL" : "DirectDraw, OpenGL");
    Game_GlPresentFiltered(window, present_tex, present_w, present_h, 1, 0, Game_GlFilter());
    gl_release();
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
    scene_ms = 0;
    scene_has_z = 0;
    scene_target = NULL;
    if (gl_context != NULL)
    {
        if (draw_prog != 0) glDeleteProgram(draw_prog);
        draw_prog = 0;
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }
    gl_window = NULL;
}
