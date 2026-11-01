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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL.h>
#include "Game_gl.h"
#include "Game_hooks.h"

#if defined(__ANDROID__) && GAME_WINAPI_TOUCH_GAMEPAD
#include "touchgamepad.h"
#define GL_TOUCH_GAMEPAD 1
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#if defined(GAME_GLES)
#define SHADER_HEADER "#version 100\nprecision highp float;\n"
#else
#define SHADER_HEADER "#version 120\n"
#endif


SDL_GLContext Game_GlCreateContext(SDL_Window *window, int vsync)
{
    SDL_GLContext context;

    if (window == NULL) return NULL;
    context = SDL_GL_CreateContext(window);
    if (context == NULL)
    {
        eprintf("Game_Gl: SDL_GL_CreateContext: %s\n", SDL_GetError());
        return NULL;
    }
#if !defined(GAME_GLES)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) || !GLAD_GL_VERSION_2_0 || !GLAD_GL_ARB_framebuffer_object)
    {
        eprintf("Game_Gl: OpenGL 2.0 with frame buffer objects is needed\n");
        SDL_GL_DeleteContext(context);
        return NULL;
    }
#endif
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    return context;
}

#if defined(GAME_GLES)

/* ES has no BGRA that every device takes, and an FBO's texture cannot swizzle
 * (what is drawn into it is RGBA): the channels are swapped here */
static void swap_red_blue(uint32_t *dst, const uint32_t *src, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        uint32_t c = src[i];
        dst[i] = (c & 0xff00ff00u) | ((c >> 16) & 0xffu) | ((c & 0xffu) << 16);
    }
}

static const void *rgba_of(const void *pixels, int width, int height)
{
    static uint32_t *scratch;
    static size_t scratch_count;
    size_t count = (size_t)width * height;

    if (pixels == NULL) return NULL;
    if (count > scratch_count)
    {
        free(scratch);
        scratch = (uint32_t *)malloc(count * 4);
        scratch_count = (scratch != NULL) ? count : 0;
        if (scratch == NULL) return NULL;
    }
    swap_red_blue(scratch, (const uint32_t *)pixels, count);
    return scratch;
}

void Game_GlTexImageBgra(GLint level, int width, int height, const void *pixels)
{
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_of(pixels, width, height));
}

void Game_GlTexSubImageBgra(int width, int height, const void *pixels)
{
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_of(pixels, width, height));
}

void Game_GlReadBgra(int width, int height, void *pixels)
{
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    swap_red_blue((uint32_t *)pixels, (const uint32_t *)pixels, (size_t)width * height);
}

#else

void Game_GlTexImageBgra(GLint level, int width, int height, const void *pixels)
{
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
}

void Game_GlTexSubImageBgra(int width, int height, const void *pixels)
{
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
}

void Game_GlReadBgra(int width, int height, void *pixels)
{
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
}

#endif

GLuint Game_GlCompileShader(GLenum type, const char *src, const char *tag)
{
    GLuint sh = glCreateShader(type);
    const char *parts[2] = { SHADER_HEADER, src };
    GLint ok = 0;

    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        eprintf("Game_Gl: %s shader: %s\n", tag, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint Game_GlProgram(const char *vs, const char *fs,
                      const char *attr0, const char *attr1, const char *attr2,
                      const char *tag)
{
    GLuint v = Game_GlCompileShader(GL_VERTEX_SHADER, vs, tag);
    GLuint f = Game_GlCompileShader(GL_FRAGMENT_SHADER, fs, tag), p;
    GLint ok = 0;

    if (v == 0 || f == 0) return 0;
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, attr0);
    if (attr1 != NULL) glBindAttribLocation(p, 1, attr1);
    if (attr2 != NULL) glBindAttribLocation(p, 2, attr2);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        eprintf("Game_Gl: %s link: %s\n", tag, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

int Game_GlFboCreate(GLuint *fbo, GLuint *tex, int width, int height,
                     int with_depth, GLuint *depth)
{
    glGenFramebuffers(1, fbo);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    Game_GlTexImageBgra(0, width, height, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    if (with_depth)
    {
        glGenRenderbuffers(1, depth);
        glBindRenderbuffer(GL_RENDERBUFFER, *depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        eprintf("Game_Gl: incomplete frame buffer\n");
        return 0;
    }
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | (with_depth ? GL_DEPTH_BUFFER_BIT : 0));
    return 1;
}

void Game_GlFboDelete(GLuint *fbo, GLuint *tex, int with_depth, GLuint *depth)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, fbo);
    glDeleteTextures(1, tex);
    if (with_depth) glDeleteRenderbuffers(1, depth);
    *fbo = *tex = 0;
    if (with_depth) *depth = 0;
}

/* the image of a texture on the window: the quad of the screen, the texture
 * flipped for the origin of the image; the program is made for the context it
 * is first used in (a new context invalidates the old programs) */

static const char present_vs[] =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char present_fs[] =
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0);\n"
    "}\n";

#if defined(GL_TOUCH_GAMEPAD)

/* The buttons of the touch gamepad over the picture. touchgamepad.c draws with
 * an SDL_Renderer and a GL window has none: it draws into a surface the size
 * of the window, by SDL's software renderer, and the surface is a texture over
 * the window, loaded again only when a button changes. Blending over the
 * surface's clear alpha leaves premultiplied colour. */

static const char overlay_fs[] =
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv);\n"
    "}\n";

static void present_overlay(int ww, int wh, const float *pos, SDL_GLContext context)
{
    static const float uv[8] = { 0.0f, 1.0f,  1.0f, 1.0f,  0.0f, 0.0f,  1.0f, 0.0f };
    static SDL_Surface *surface;
    static SDL_Renderer *renderer;
    static GLuint prog, tex;
    static GLint u_tex;
    static SDL_GLContext prog_context;
    static int drawn_stamp;
    int new_texture = 0;

    if (prog_context != context)
    {
        prog = Game_GlProgram(present_vs, overlay_fs, "a_pos", "a_uv", NULL, "Game_Gl overlay");
        u_tex = (prog != 0) ? glGetUniformLocation(prog, "u_tex") : 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        prog_context = context;
        new_texture = 1;
    }
    if (prog == 0) return;

    if ((surface == NULL) || (surface->w != ww) || (surface->h != wh))
    {
        TouchGamepad_ForgetRenderer();
        if (renderer != NULL) SDL_DestroyRenderer(renderer);
        if (surface != NULL) SDL_FreeSurface(surface);
        renderer = NULL;
        surface = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surface != NULL) renderer = SDL_CreateSoftwareRenderer(surface);
        if (renderer == NULL)
        {
            eprintf("Game_Gl: the touch gamepad's surface: %s\n", SDL_GetError());
            if (surface != NULL) SDL_FreeSurface(surface);
            surface = NULL;
            return;
        }
        new_texture = 1;
    }

    if (new_texture || (drawn_stamp != TouchGamepad_Stamp()))
    {
        drawn_stamp = TouchGamepad_Stamp();
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        TouchGamepad_Draw(renderer);
        SDL_RenderFlush(renderer);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (new_texture) Game_GlTexImageBgra(0, ww, wh, surface->pixels);
        else Game_GlTexSubImageBgra(ww, wh, surface->pixels);
    }

    glViewport(0, 0, ww, wh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_tex, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

#endif

void Game_GlPresentTexture(SDL_Window *window, GLuint tex,
                           int width, int height, int origin_upper_left,
                           int linear_filter)
{
    static const float pos[8] = { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    static GLuint prog;
    static GLint u_tex;
    static SDL_GLContext prog_context;
    float uv[8], top, bottom;
    int ww, wh, vx, vy, vw, vh;

    if (window == NULL || tex == 0) return;

    if (prog == 0 || prog_context != SDL_GL_GetCurrentContext())
    {
        if (prog != 0) glDeleteProgram(prog);
        prog = Game_GlProgram(present_vs, present_fs, "a_pos", "a_uv", NULL, "Game_Gl present");
        u_tex = (prog != 0) ? glGetUniformLocation(prog, "u_tex") : 0;
        prog_context = SDL_GL_GetCurrentContext();
        if (prog == 0) return;
    }

    SDL_GL_GetDrawableSize(window, &ww, &wh);
    if (ww < 1 || wh < 1) return;
    if ((int64_t)ww * height > (int64_t)wh * width)
    {
        vh = wh;
        vw = (int)((int64_t)wh * width / height);
    }
    else
    {
        vw = ww;
        vh = (int)((int64_t)ww * height / width);
    }
    vx = (ww - vw) / 2;
    vy = (wh - vh) / 2;

    /* row 0 of the image is the top of the screen (the bottom for a lower
     * left origin): v grows down the image, up the window */
    top = origin_upper_left ? 0.0f : 1.0f;
    bottom = 1.0f - top;
    uv[0] = 0.0f; uv[1] = bottom;
    uv[2] = 1.0f; uv[3] = bottom;
    uv[4] = 0.0f; uv[5] = top;
    uv[6] = 1.0f; uv[7] = top;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, ww, wh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vx, vy, vw, vh);

    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_filter ? GL_LINEAR : GL_NEAREST);
    glUniform1i(u_tex, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glUseProgram(0);

#if defined(GL_TOUCH_GAMEPAD)
    present_overlay(ww, wh, pos, prog_context);
#endif
    SDL_GL_SwapWindow(window);
}
