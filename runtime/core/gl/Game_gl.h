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

/* The OpenGL that does not belong to one game: the glad loader, a context on
 * an SDL window, shader programs, frame buffer objects, and the image of a
 * texture scaled to the window. Every game has it (scons/game.py, GAME_GL).
 * The window is not made here: a win32 game gets one from
 * Winapi_ScreenOpenGL, others draw in Game_Window.
 *
 * GAME_GLES is the Android build (OpenGL ES 3.0, no glad, its header); gles=yes
 * of scons makes the same on a desktop. What differs is kept here: the pixels
 * go through Game_GlTexImageBgra and its kin, and a shader has no #version. */

#ifndef GAME_GL_H
#define GAME_GL_H

#include <SDL.h>
#if defined(GAME_GLES)
#include <GLES3/gl3.h>
#define glClearDepth glClearDepthf
#else
#include <glad/glad.h>
#endif

/* SDL_GL_CreateContext, then glad; GL 2.0 with frame buffer objects is
 * needed. NULL and a message if any of it is missing. */
SDL_GLContext Game_GlCreateContext(SDL_Window *window, int vsync);

/* The bound texture, RGBA8, from rows of 0xAARRGGBB (pixels may be NULL). */
void Game_GlTexImageBgra(GLint level, int width, int height, const void *pixels);
void Game_GlTexSubImageBgra(int width, int height, const void *pixels);
/* the bound frame buffer into rows of 0xAARRGGBB */
void Game_GlReadBgra(int width, int height, void *pixels);

/* glCreateShader/glCompileShader; the source has no #version: GLSL 1.20 or
 * GLSL ES 1.00, the common part of the two (attribute, varying, texture2D,
 * gl_FragColor). tag names the shader in the error message */
GLuint Game_GlCompileShader(GLenum type, const char *src, const char *tag);

/* a program of a vertex and a fragment shader; the attributes are bound to
 * locations 0, 1, 2 (attr1 and attr2 may be NULL); 0 and a message on error */
GLuint Game_GlProgram(const char *vs, const char *fs,
                      const char *attr0, const char *attr1, const char *attr2,
                      const char *tag);

/* an FBO with an RGBA8 colour texture; with_depth adds a 24-bit depth render
 * buffer (depth != NULL). 0 and a message if the FBO is incomplete. */
int Game_GlFboCreate(GLuint *fbo, GLuint *tex, int width, int height,
                     int with_depth, GLuint *depth);
void Game_GlFboDelete(GLuint *fbo, GLuint *tex, int with_depth, GLuint *depth);

/* the image of a texture on the screen: scaled to the window with the aspect
 * of the image, letterboxed, then SDL_GL_SwapWindow. Row 0 of the image is
 * the top of the picture when origin_upper_left, the bottom otherwise. */
void Game_GlPresentTexture(SDL_Window *window, GLuint tex,
                           int width, int height, int origin_upper_left,
                           int linear_filter);

/* The 2D filters (Display_Shader of a game's settings file, a mod): 0 none, 1 smooth
 * (sharp bilinear), 2 pixelart (Scale2x), 3 scanlines, 4 crt. For a picture of a
 * few hundred texels on a bigger window. */
#define GAME_GL_SHADERS 5
/* the number of a name ("off" is none), -1 for an unknown one */
int Game_GlShaderParse(const char *name);
const char *Game_GlShaderName(int shader);
/* the filter of the process: at first that of the settings file (Display_Shader,
 * <GAME>_SHADER over it; an unknown name is none, with a message), then what the
 * port's panel switched to; the presenters read it at every frame */
int Game_GlFilter(void);
void Game_GlSetFilter(int shader);
/* Game_OsdInit: the filter's item of the port's panel (Game_osd.h), active once a
 * picture went through Game_GlPresentFiltered or Game_GlPresentAspect */
void Game_GlOsdItems(void);
/* Game_GlPresentTexture with a filter; for the filters the sampling is theirs */
void Game_GlPresentFiltered(SDL_Window *window, GLuint tex,
                            int width, int height, int origin_upper_left,
                            int linear_filter, int shader);
/* Game_GlPresentFiltered of a picture shown at aspect_w:aspect_h (4:3 for a 320x200
 * mode of a 4:3 monitor), not at the aspect of its pixels */
void Game_GlPresentAspect(SDL_Window *window, GLuint tex, int width, int height,
                          int aspect_w, int aspect_h, int origin_upper_left,
                          int linear_filter, int shader);

#endif
