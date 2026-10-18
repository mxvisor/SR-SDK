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
#include <SDL.h>
#include <glad/glad.h>
#include "Game_gl.h"

#define eprintf(...) fprintf(stderr,__VA_ARGS__)


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
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) || !GLAD_GL_VERSION_2_0 || !GLAD_GL_ARB_framebuffer_object)
    {
        eprintf("Game_Gl: OpenGL 2.0 with frame buffer objects is needed\n");
        SDL_GL_DeleteContext(context);
        return NULL;
    }
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    return context;
}

GLuint Game_GlCompileShader(GLenum type, const char *src, const char *tag)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(sh, 1, &src, NULL);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
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
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char present_fs[] =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0);\n"
    "}\n";

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

    SDL_GL_SwapWindow(window);
}
