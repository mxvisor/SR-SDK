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
 * texture scaled to the window. A game enables this module with opengl=yes,
 * glide=yes or d3d_gl=yes in game.cfg. The window is not made here: a win32
 * game gets one from Winapi_ScreenOpenGL, others draw in Game_Window. */

#ifndef GAME_GL_H
#define GAME_GL_H

#include <SDL.h>
#include <glad/glad.h>

/* SDL_GL_CreateContext, then glad; GL 2.0 with frame buffer objects is
 * needed. NULL and a message if any of it is missing. */
SDL_GLContext Game_GlCreateContext(SDL_Window *window, int vsync);

/* glCreateShader/glCompileShader; tag names the shader in the error message */
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

#endif
