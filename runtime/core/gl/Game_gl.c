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
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <SDL.h>
#include "Game_gl.h"
#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_hooks.h"
#include "Game_ui.h"
#include "Game_osd.h"

/* the touch gamepad on Android: a win32 game's with GAME_WINAPI_TOUCH_GAMEPAD, every DOS game's
 * (core/input/pc.c gives it the touches) */
#if defined(__ANDROID__) && (GAME_WINAPI_TOUCH_GAMEPAD || !defined(GAME_WINAPI))
#define GL_TOUCH_GAMEPAD 1
#endif

#define eprintf(...) fprintf(stderr,__VA_ARGS__)

#if defined(GAME_GLES)
#define SHADER_HEADER "#version 100\nprecision highp float;\n"
#else
#define SHADER_HEADER "#version 120\n"
#endif


void Game_GlUnavailable(const char *why)
{
    char text[1024];

    snprintf(text, sizeof(text),
#if defined(GAME_GLES)
             "This game needs OpenGL ES 3.0, and the device has none.\n\n"
#else
             "This game needs OpenGL 2.0, and the system has none.\n"
             "Install the driver of the video card (in a virtual machine: turn on its 3D acceleration).\n\n"
#endif
             "%s", why);
    eprintf(GAME_NAME ": %s\n", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_NAME, text, NULL);
    /* from whatever thread: the game's threads hold no state to save yet */
    fflush(NULL);
    _exit(1);
}

SDL_GLContext Game_GlCreateContext(SDL_Window *window, int vsync)
{
    SDL_GLContext context;

    if (window == NULL) return NULL;
    context = SDL_GL_CreateContext(window);
    if (context == NULL) Game_GlUnavailable(SDL_GetError());
#if !defined(GAME_GLES)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) || !GLAD_GL_VERSION_2_0 || !GLAD_GL_ARB_framebuffer_object)
    {
        /* glad's glGetString is NULL when it could load nothing */
        const char *version = (glGetString != NULL) ? (const char *)glGetString(GL_VERSION) : NULL;
        const char *renderer = (glGetString != NULL) ? (const char *)glGetString(GL_RENDERER) : NULL;
        char why[256];

        snprintf(why, sizeof(why), "The OpenGL of the system: %s, %s", (version != NULL) ? version : "?",
                 (renderer != NULL) ? renderer : "?");
        Game_GlUnavailable(why);
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

/* The 2D filters of Display_Shader (a mod): the fragment shaders of the picture of a
 * game that draws a small bitmap (u_size texels) to a window (u_out pixels). 0 is the
 * plain present above (nearest or linear, as the game asks). The texture is sampled
 * with GL_LINEAR where the shader does its own sharpening. */

static const char filter_head[] =
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_size;\n"
    "uniform vec2 u_out;\n"
    "varying vec2 v_uv;\n";

/* bilinear only on the edges of the texels: crisp at any scale, no shimmering */
static const char filter_smooth_fs[] =
    "void main() {\n"
    "    vec2 p = v_uv * u_size;\n"
    "    vec2 i = floor(p);\n"
    "    vec2 s = u_out / u_size;\n"
    "    vec2 r = clamp((p - i - 0.5) * s + 0.5, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(texture2D(u_tex, (i + r) / u_size).rgb, 1.0);\n"
    "}\n";

/* Scale2x (EPX): the corners of a texel are rounded off where the neighbours say a diagonal line is */
static const char filter_pixelart_fs[] =
    "vec3 px(vec2 c) { return texture2D(u_tex, (c + 0.5) / u_size).rgb; }\n"
    "bool eq(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.004))); }\n"
    "void main() {\n"
    "    vec2 p = v_uv * u_size;\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = p - i;\n"
    "    vec3 P = px(i);\n"
    "    vec3 A = px(i + vec2(0.0, -1.0));\n"
    "    vec3 C = px(i + vec2(-1.0, 0.0));\n"
    "    vec3 B = px(i + vec2(1.0, 0.0));\n"
    "    vec3 D = px(i + vec2(0.0, 1.0));\n"
    "    vec3 o = P;\n"
    "    bool left = f.x < 0.5;\n"
    "    bool up = f.y < 0.5;\n"
    "    if (up && left) { if (eq(C, A) && !eq(C, D) && !eq(A, B)) o = A; }\n"
    "    else if (up) { if (eq(A, B) && !eq(A, C) && !eq(B, D)) o = B; }\n"
    "    else if (left) { if (eq(D, C) && !eq(D, B) && !eq(C, A)) o = C; }\n"
    "    else { if (eq(B, D) && !eq(B, A) && !eq(D, C)) o = D; }\n"
    "    gl_FragColor = vec4(o, 1.0);\n"
    "}\n";

/* a row of the picture is a lit line and a dark gap, as on a tube */
static const char filter_scanlines_fs[] =
    "void main() {\n"
    "    vec2 p = v_uv * u_size;\n"
    "    vec2 i = floor(p);\n"
    "    vec2 s = u_out / u_size;\n"
    "    float rx = clamp((p.x - i.x - 0.5) * s.x + 0.5, 0.0, 1.0);\n"
    "    vec3 c = texture2D(u_tex, vec2(i.x + rx, i.y + 0.5) / u_size).rgb;\n"
    "    float d = (fract(p.y) - 0.5) * 2.0;\n"
    "    float beam = exp(-d * d * 2.2);\n"
    "    gl_FragColor = vec4(c * (0.25 + 0.95 * beam), 1.0);\n"
    "}\n";

/* scanlines, the mask of the three colours, a darker edge of the screen */
static const char filter_crt_fs[] =
    "void main() {\n"
    "    vec2 p = v_uv * u_size;\n"
    "    vec2 i = floor(p);\n"
    "    vec2 s = u_out / u_size;\n"
    "    float rx = clamp((p.x - i.x - 0.5) * s.x + 0.5, 0.0, 1.0);\n"
    "    vec3 c = texture2D(u_tex, vec2(i.x + rx, i.y + 0.5) / u_size).rgb;\n"
    "    float d = (fract(p.y) - 0.5) * 2.0;\n"
    "    float beam = exp(-d * d * 2.2);\n"
    "    float m = mod(floor(gl_FragCoord.x), 3.0);\n"
    "    vec3 mask = vec3(m < 1.0 ? 1.0 : 0.72, (m >= 1.0 && m < 2.0) ? 1.0 : 0.72, m >= 2.0 ? 1.0 : 0.72);\n"
    "    vec2 q = v_uv - 0.5;\n"
    "    float vig = 1.0 - 0.55 * dot(q, q);\n"
    "    gl_FragColor = vec4(c * (0.25 + 0.95 * beam) * mask * 1.2 * vig, 1.0);\n"
    "}\n";

static const char *const filter_names[GAME_GL_SHADERS] = { "none", "smooth", "pixelart", "scanlines", "crt" };
static const char *const filter_body[GAME_GL_SHADERS] = { NULL, filter_smooth_fs, filter_pixelart_fs, filter_scanlines_fs, filter_crt_fs };
/* the filters sample by themselves; EPX needs the nearest texel */
static const int filter_linear[GAME_GL_SHADERS] = { 0, 1, 0, 0, 0 };

int Game_GlShaderParse(const char *name)
{
    int i;

    if (name == NULL) return -1;
    if (strcasecmp(name, "off") == 0) return 0;
    for (i = 0; i < GAME_GL_SHADERS; i++)
    {
        if (strcasecmp(name, filter_names[i]) == 0) return i;
    }
    return -1;
}

const char *Game_GlShaderName(int shader)
{
    return ((shader >= 0) && (shader < GAME_GL_SHADERS)) ? filter_names[shader] : "none";
}

/* the filter of the process: of the settings file at first, then the panel's */
static volatile int filter_current = -1;
/* a presenter that has the filters showed a picture */
static volatile int filter_used;

int Game_GlFilter(void)
{
    if (filter_current < 0)
    {
        const char *name = getenv(GAME_ENV("SHADER"));
        int shader = 0;

        if (name == NULL || name[0] == 0) name = Game_Shader;
        if (name[0] != 0)
        {
            shader = Game_GlShaderParse(name);
            if (shader < 0)
            {
                eprintf(GAME_NAME ": unknown Display_Shader: %s\n", name);
                shader = 0;
            }
            else if (shader != 0) eprintf(GAME_NAME ": the picture filter: %s\n", Game_GlShaderName(shader));
        }
        filter_current = shader;
    }
    return filter_current;
}

void Game_GlSetFilter(int shader)
{
    if (shader < 0 || shader >= GAME_GL_SHADERS) shader = 0;
    filter_current = shader;
}

/* the filter in the port's panel: where a presenter has the filters, saved as
 * Display_Shader */
static int filter_item_active(void)
{
    return filter_used;
}

static void filter_item_press(void)
{
    Game_GlSetFilter((Game_GlFilter() + 1) % GAME_GL_SHADERS);
}

static void filter_item_step(int dir)
{
    int shader = Game_GlFilter() + dir;
    if (shader >= 0 && shader < GAME_GL_SHADERS) Game_GlSetFilter(shader);
}

static const char *filter_item_state(void)
{
    return Game_GlShaderName(Game_GlFilter());
}

static const Game_OsdItem filter_item = { "Filter", filter_item_active, filter_item_press, filter_item_state, "Display_Shader", filter_item_step };

void Game_GlOsdItems(void)
{
    Game_OsdAddItem(&filter_item);
}

/* The screen interface over the picture (Game_ui.h: the touch gamepad, the
 * window menu). It draws with an SDL_Renderer and a GL window has none: it
 * draws into a surface the size of the window, by SDL's software renderer, and
 * the surface is a texture over the window, loaded again only when what it
 * shows changes. Blending over the surface's clear alpha leaves premultiplied
 * colour. Without the touch gamepad the pass runs only while there is
 * something to show (a menu, the on-screen display). */

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
        Game_UiForgetRenderer();
        if (renderer != NULL) SDL_DestroyRenderer(renderer);
        if (surface != NULL) SDL_FreeSurface(surface);
        renderer = NULL;
        surface = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surface != NULL) renderer = SDL_CreateSoftwareRenderer(surface);
        if (renderer == NULL)
        {
            eprintf("Game_Gl: the surface of the screen interface: %s\n", SDL_GetError());
            if (surface != NULL) SDL_FreeSurface(surface);
            surface = NULL;
            return;
        }
        new_texture = 1;
    }

    if (new_texture || (drawn_stamp != Game_UiStamp()))
    {
        drawn_stamp = Game_UiStamp();
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        Game_UiDraw(renderer);
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

static void present_texture(SDL_Window *window, GLuint tex,
                            int width, int height, int aspect_w, int aspect_h,
                            int origin_upper_left, int linear_filter, int shader)
{
    static const float pos[8] = { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    static GLuint progs[GAME_GL_SHADERS];
    static GLint u_tex[GAME_GL_SHADERS], u_size[GAME_GL_SHADERS], u_out[GAME_GL_SHADERS];
    static SDL_GLContext prog_context;
    GLuint prog;
    float uv[8], top, bottom;
    int ww, wh, vx, vy, vw, vh, i;
    SDL_Rect picture;

    if (window == NULL || tex == 0) return;
    if (shader < 0 || shader >= GAME_GL_SHADERS) shader = 0;

    if (prog_context != SDL_GL_GetCurrentContext())
    {
        for (i = 0; i < GAME_GL_SHADERS; i++)
        {
            if (progs[i] != 0) glDeleteProgram(progs[i]);
            progs[i] = 0;
        }
        prog_context = SDL_GL_GetCurrentContext();
    }
    if (progs[shader] == 0)
    {
        if (shader == 0)
        {
            progs[0] = Game_GlProgram(present_vs, present_fs, "a_pos", "a_uv", NULL, "Game_Gl present");
        }
        else
        {
            char *fs = (char *)malloc(strlen(filter_head) + strlen(filter_body[shader]) + 1);
            if (fs == NULL) return;
            strcpy(fs, filter_head);
            strcat(fs, filter_body[shader]);
            progs[shader] = Game_GlProgram(present_vs, fs, "a_pos", "a_uv", NULL, filter_names[shader]);
            free(fs);
            /* a filter that does not compile: the plain picture */
            if (progs[shader] == 0) shader = 0;
        }
        if (progs[shader] == 0)
        {
            progs[0] = Game_GlProgram(present_vs, present_fs, "a_pos", "a_uv", NULL, "Game_Gl present");
            shader = 0;
            if (progs[0] == 0) return;
        }
        u_tex[shader] = glGetUniformLocation(progs[shader], "u_tex");
        u_size[shader] = glGetUniformLocation(progs[shader], "u_size");
        u_out[shader] = glGetUniformLocation(progs[shader], "u_out");
    }
    prog = progs[shader];
    if (shader != 0) linear_filter = filter_linear[shader];

    SDL_GL_GetDrawableSize(window, &ww, &wh);
    if (ww < 1 || wh < 1) return;
    Game_UiPictureRect(ww, wh, aspect_w, aspect_h, &picture);
    vx = picture.x;
    vy = picture.y;
    vw = picture.w;
    vh = picture.h;

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
    glUniform1i(u_tex[shader], 0);
    if (u_size[shader] >= 0) glUniform2f(u_size[shader], (float)width, (float)height);
    if (u_out[shader] >= 0) glUniform2f(u_out[shader], (float)vw, (float)vh);
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
#else
    if (Game_UiActive()) present_overlay(ww, wh, pos, prog_context);
#endif
    SDL_GL_SwapWindow(window);
}

void Game_GlPresentTexture(SDL_Window *window, GLuint tex,
                           int width, int height, int origin_upper_left,
                           int linear_filter)
{
    present_texture(window, tex, width, height, width, height, origin_upper_left, linear_filter, 0);
}

void Game_GlPresentFiltered(SDL_Window *window, GLuint tex,
                            int width, int height, int origin_upper_left,
                            int linear_filter, int shader)
{
    filter_used = 1;
    present_texture(window, tex, width, height, width, height, origin_upper_left, linear_filter, shader);
}

void Game_GlPresentAspect(SDL_Window *window, GLuint tex, int width, int height,
                          int aspect_w, int aspect_h, int origin_upper_left,
                          int linear_filter, int shader)
{
    filter_used = 1;
    present_texture(window, tex, width, height, aspect_w, aspect_h, origin_upper_left, linear_filter, shader);
}

