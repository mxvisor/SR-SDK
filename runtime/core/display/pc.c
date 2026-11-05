/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
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

/* The default display of a port: a resizable window, 4:3 in shape,
 * showing the game's video mode.
 *
 * The flip thread expands the frame (Render_Width x Render_Height, 8 bits) through
 * the palette into 32-bit pixels (Display_Flip_Procedure); Game_Display_Flip
 * shows that through OpenGL (Game_GlPresentAspect), 4:3 inside the window, by
 * the nearest pixel, or through the 2D filter of Display_Shader (Game_GlFilter).
 * Without OpenGL an SDL renderer shows it the same way, by the nearest pixel.
 * The window opens at the size of mode 13h on a 4:3 monitor, 320x240
 * (Game_WindowCreate); the player makes it bigger.
 * The palette is the game's DAC in 8-bit levels, set by the port 3C9h through
 * Set_Palette_Value (Game_dosvga.c) or by the Win32 palette calls.
 *
 * A Win32 game draws in a window of its own (WinApi-ddraw.c); the one made here
 * stays hidden, because shown it would be a second, black window that covers
 * the game and takes the mouse.
 *
 * What a game adds comes through Game_hooks.h, all optional:
 *   GAME_DISPLAY_INIT()                 after the defaults of Init_Display
 *   GAME_DISPLAY_INIT2()                at the end of Init_Display2: it may change Render_Width and
 *                                       Render_Height and Display_Flip_Procedure
 *   GAME_DISPLAY_CLEANUP()              in Cleanup_Display
 *   GAME_DISPLAY_WINDOW_FLAGS           more SDL_WINDOW_* flags of the window
 *   GAME_DISPLAY_FLIP()                 at the start of Game_Display_Flip: nonzero, the game showed
 *                                       the frame itself (another renderer)
 *   GAME_DISPLAY_OVERLAY(picture)       after the picture is drawn by an SDL renderer, before
 *                                       the touch gamepad (picture: the SDL_Rect it is in)
 *   GAME_DISPLAY_DESTROY()              in Game_Display_Destroy, before the renderer goes
 * A game with a display altogether its own has its own game/display/pc.c with
 * Init_Display, Init_Display2, Config_Display, Cleanup_Display, Set_Palette_Value,
 * Game_Display_Create, Game_Display_Destroy and Game_Display_Flip. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_vars.h"
#include "display.h"
#include "main.h"
#include "Game_ui.h"
#include "Game_osd.h"
#include "Game_thread.h"
#include "palette32bgra.h"
#if defined(GAME_WINAPI)
#include "WinApi-ddraw.h"
#else
#include "Game_gl.h"
#endif
#include "Game_hooks.h"

#if !defined(GAME_WINAPI)
/* The OpenGL of Game_Window: made and used on the main thread, which flips. */
static SDL_GLContext gl_context;
static GLuint gl_texture;
static int gl_width, gl_height;

static int gl_create(void)
{
    gl_context = Game_GlCreateContext(Game_Window, 1);
    if (gl_context == NULL) return 0;
    glGenTextures(1, &gl_texture);
    glBindTexture(GL_TEXTURE_2D, gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_width = gl_height = 0;
    return 1;
}

static void gl_flip(void)
{
    glBindTexture(GL_TEXTURE_2D, gl_texture);
    if (gl_width != (int)Render_Width || gl_height != (int)Render_Height)
    {
        gl_width = Render_Width;
        gl_height = Render_Height;
        Game_GlTexImageBgra(0, gl_width, gl_height, Game_TextureData);
    }
    else
    {
        Game_GlTexSubImageBgra(gl_width, gl_height, Game_TextureData);
    }
    Game_OsdFrame(gl_width, gl_height, "OpenGL");
    /* mode 13h has non-square pixels made for a 4:3 monitor */
    Game_GlPresentAspect(Game_Window, gl_texture, gl_width, gl_height, 4, 3, 1, 0, Game_GlFilter());
}
#endif

static void Set_Palette_Value2(uint32_t index, uint32_t r, uint32_t g, uint32_t b)
{
    (void)r;
    (void)g;
    (void)b;

    /* opaque: the texture is ARGB8888 */
    Game_Palette[index].s.a = 255;
}

/* The frame is Render_Width x Render_Height, whatever mode the game set. */
static void Flip_8_to_32(const uint8_t *src, uint32_t *dst)
{
    int counter;

    for (counter = Render_Width * Render_Height; counter != 0; counter-=8)
    {
        dst[0] = Game_Palette[src[0]].pix;
        dst[1] = Game_Palette[src[1]].pix;
        dst[2] = Game_Palette[src[2]].pix;
        dst[3] = Game_Palette[src[3]].pix;
        dst[4] = Game_Palette[src[4]].pix;
        dst[5] = Game_Palette[src[5]].pix;
        dst[6] = Game_Palette[src[6]].pix;
        dst[7] = Game_Palette[src[7]].pix;

        src+=8;
        dst+=8;
    }
}

/* the display colours as 32-bit pixels (Game_Palette is static here) */
uint32_t *Game_GetPalette(void)
{
    return &Game_Palette[0].pix;
}

void Init_Display(void)
{
#ifdef GAME_DISPLAY_INIT
    GAME_DISPLAY_INIT();
#endif
}

void Init_Display2(void)
{
    int index;

    Init_Palette();
    for (index = 0; index < 256; index++)
    {
        Game_Palette[index].s.a = 255;
    }

    Display_Bitsperpixel = 32;
    Display_MouseLocked = 0;
    Render_Width = 320;
    Render_Height = 200;
    Display_Flip_Procedure = (Game_Flip_Procedure) &Flip_8_to_32;
#ifdef GAME_DISPLAY_INIT2
    GAME_DISPLAY_INIT2();
#endif
}

int Config_Display(char *str, char *param)
{
    (void)str;
    (void)param;

    return 0;
}

void Cleanup_Display(void)
{
#ifdef GAME_DISPLAY_CLEANUP
    GAME_DISPLAY_CLEANUP();
#endif
#if defined(GAME_WINAPI)
    Winapi_DestroyDirectDrawWindow();
#endif
}
#if defined(GAME_WINAPI)
#define WINDOW_SHOWN_FLAG SDL_WINDOW_HIDDEN
#else
#define WINDOW_SHOWN_FLAG 0
#endif
#ifndef GAME_DISPLAY_WINDOW_FLAGS
#define GAME_DISPLAY_WINDOW_FLAGS 0
#endif

/* The window, mode 13h's 320x200 at 4:3 (a game's bigger frame, as a DOS game's
 * enhanced 3D, is the same picture), and what the flip thread draws into:
 * Game_TextureData, Render_Width x Render_Height 32-bit pixels. */
int Game_Display_Create(void)
{
#if defined(GAME_WINAPI) && defined(__ANDROID__)
    /* Android has one window, and a Win32 game opens its own: nothing here
     * may take it (Game_Display_Flip draws nothing without a renderer). */
    Game_TextureData = calloc(1024 * 768, Display_Bitsperpixel / 8);
    if (Game_TextureData == NULL)
    {
        SDL_OutOfMemory();
        return -1;
    }
    return 0;
#endif

#if !defined(GAME_WINAPI)
#if defined(GAME_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#define WINDOW_GL_FLAG SDL_WINDOW_OPENGL
#else
#define WINDOW_GL_FLAG 0
#endif
#if defined(GAME_WINAPI)
    /* hidden for good: not the window of the picture */
    Game_Window = SDL_CreateWindow(GAME_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   320, 240, WINDOW_SHOWN_FLAG | WINDOW_GL_FLAG | GAME_DISPLAY_WINDOW_FLAGS);
#else
    /* resizable; the picture keeps 4:3 inside whatever size the window gets */
    Game_Window = Game_WindowCreate(320, 240, WINDOW_SHOWN_FLAG | WINDOW_GL_FLAG | GAME_DISPLAY_WINDOW_FLAGS);
#endif
    if (Game_Window == NULL) return -1;

    /* sized for the largest mode, so a mode change never moves it under the flip thread */
    Game_TextureData = calloc((Render_Width * Render_Height > 1024 * 768) ? Render_Width * Render_Height : 1024 * 768,
                              Display_Bitsperpixel / 8);
    if (Game_TextureData == NULL)
    {
        SDL_OutOfMemory();
        return -1;
    }

#if !defined(GAME_WINAPI)
    if (gl_create()) return 0;
    fprintf(stderr, GAME_NAME ": no OpenGL, the picture of an SDL renderer\n");
#endif

    Game_Renderer = SDL_CreateRenderer(Game_Window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (Game_Renderer == NULL) Game_Renderer = SDL_CreateRenderer(Game_Window, -1, SDL_RENDERER_SOFTWARE);
    if (Game_Renderer == NULL) return -1;

    Game_Texture[0] = SDL_CreateTexture(Game_Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        Render_Width, Render_Height);
    if (Game_Texture[0] == NULL) return -1;
    SDL_SetTextureScaleMode(Game_Texture[0], SDL_ScaleModeNearest);

    return 0;
}

void Game_Display_Destroy(void)
{
#ifdef GAME_DISPLAY_DESTROY
    GAME_DISPLAY_DESTROY();
#endif
#if !defined(GAME_WINAPI)
    if (gl_context != NULL)
    {
        if (gl_texture != 0) glDeleteTextures(1, &gl_texture);
        SDL_GL_DeleteContext(gl_context);
        gl_texture = 0;
        gl_context = NULL;
    }
#endif
    if (Game_Texture[0] != NULL) SDL_DestroyTexture(Game_Texture[0]);
    if (Game_Renderer != NULL) SDL_DestroyRenderer(Game_Renderer);
    if (Game_Window != NULL) SDL_DestroyWindow(Game_Window);
    free(Game_TextureData);

    Game_Texture[0] = NULL;
    Game_Renderer = NULL;
    Game_Window = NULL;
    Game_TextureData = NULL;
}

/* Largest 4:3 rectangle centred in the output.  Mode 13h is 320x200 with
 * non-square pixels made for a 4:3 monitor; dosbox-staging's default
 * (aspect = true) shows it the same way. */
static void Display_PictureRect(int output_width, int output_height, SDL_Rect *rect)
{
    rect->w = output_width;
    rect->h = output_width * 3 / 4;
    if (rect->h > output_height)
    {
        rect->h = output_height;
        rect->w = output_height * 4 / 3;
    }
    rect->x = (output_width - rect->w) / 2;
    rect->y = (output_height - rect->h) / 2;
}

/* Main thread only, on EC_DISPLAY_FLIP_FINISH.  The frame goes into the
 * picture by the nearest pixel, as through OpenGL without a filter. */
void Game_Display_Flip(void)
{
    SDL_Rect picture;
    int output_width, output_height;

#ifdef GAME_DISPLAY_FLIP
    if (GAME_DISPLAY_FLIP()) return;
#endif
#if !defined(GAME_WINAPI)
    if (gl_context != NULL)
    {
        gl_flip();
        return;
    }
#endif
    if (Game_Renderer == NULL || Game_Texture[0] == NULL) return;
    /* the game changed video mode: a new texture of the new size */
    int texture_width, texture_height;
    if (SDL_QueryTexture(Game_Texture[0], NULL, NULL, &texture_width, &texture_height) == 0 &&
        (texture_width != (int)Render_Width || texture_height != (int)Render_Height))
    {
        SDL_DestroyTexture(Game_Texture[0]);
        Game_Texture[0] = SDL_CreateTexture(Game_Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            Render_Width, Render_Height);
        if (Game_Texture[0] == NULL) return;
        SDL_SetTextureScaleMode(Game_Texture[0], SDL_ScaleModeNearest);
    }
    SDL_UpdateTexture(Game_Texture[0], NULL, Game_TextureData, Render_Width * Display_Bitsperpixel / 8);
    if (SDL_GetRendererOutputSize(Game_Renderer, &output_width, &output_height) != 0) return;
    Display_PictureRect(output_width, output_height, &picture);
    if (picture.w <= 0 || picture.h <= 0) return;
    Game_OsdFrame(Render_Width, Render_Height, "SDL");

    SDL_SetRenderDrawColor(Game_Renderer, 0, 0, 0, 255);
    SDL_RenderClear(Game_Renderer);
    SDL_RenderCopy(Game_Renderer, Game_Texture[0], NULL, &picture);
#ifdef GAME_DISPLAY_OVERLAY
    GAME_DISPLAY_OVERLAY(&picture);
#endif
    Game_UiDraw(Game_Renderer);
    SDL_RenderPresent(Game_Renderer);
}
