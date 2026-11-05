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

/* The screen interface of the port (Game_ui.h): the touch gamepad and one
 * client over the picture, and the text they write. */

#include <stdint.h>
#include <string.h>
#include "Game_ui.h"
#include "Game_osd.h"
#include "touchgamepad.h"
#include "VKfont2.h"

static const Game_UiClient *ui_client;
static int ui_stamp;
static int ui_width, ui_height;
static void (*ui_repaint)(void);

static SDL_Renderer *glyph_renderer;
static SDL_Texture *glyph[128];

void Game_UiSetClient(const Game_UiClient *client)
{
    ui_client = client;
    ui_stamp++;
}

void Game_UiChanged(void)
{
    ui_stamp++;
}

int Game_UiStamp(void)
{
    return ui_stamp + TouchGamepad_Stamp() + Game_OsdStamp();
}

int Game_UiVisible(void)
{
    return (ui_client != NULL && ui_client->visible != NULL && ui_client->visible()) ? 1 : 0;
}

int Game_UiActive(void)
{
    return Game_UiVisible() || Game_OsdVisible();
}

void Game_UiForgetRenderer(void)
{
    TouchGamepad_ForgetRenderer();
    memset(glyph, 0, sizeof(glyph));
    glyph_renderer = NULL;
}

void Game_UiSize(int *width, int *height)
{
    *width = ui_width;
    *height = ui_height;
}

void Game_UiDraw(SDL_Renderer *renderer)
{
    int w, h;

    int logical_w, logical_h, client, osd;

    if (renderer == NULL) return;
    TouchGamepad_Draw(renderer);
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0) return;
    ui_width = w;
    ui_height = h;
    client = Game_UiVisible() && ui_client->draw != NULL;
    osd = Game_OsdVisible();
    if (!client && !osd) return;
    /* in the pixels of the window, also when the picture has a logical size (DirectDraw) */
    SDL_RenderGetLogicalSize(renderer, &logical_w, &logical_h);
    if (logical_w != 0) SDL_RenderSetLogicalSize(renderer, 0, 0);
    if (osd) Game_OsdDraw(renderer, w, h);
    if (client) ui_client->draw(renderer, w, h);
    if (logical_w != 0) SDL_RenderSetLogicalSize(renderer, logical_w, logical_h);
}

void Game_UiSetRepaint(void (*repaint)(void))
{
    ui_repaint = repaint;
}

void Game_UiRepaint(void)
{
    if (ui_repaint != NULL) ui_repaint();
}

/* ---- text ---- */

/* Latin-1 (and the quotes and dashes of Windows-1252) without the accents */
static unsigned char plain_char(unsigned char c)
{
    static const char latin1[] =
        "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"      /* 0xC0 - 0xDF */
        "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";     /* 0xE0 - 0xFF */

    if (c < 0x80) return c;
    if (c >= 0xC0) return (unsigned char)latin1[c - 0xC0];
    switch (c)
    {
        case 0x91: case 0x92: case 0xB4: return '\'';
        case 0x93: case 0x94: case 0xAB: case 0xBB: return '"';
        case 0x96: case 0x97: case 0xAD: return '-';
        case 0xA9: return 'c';
        case 0xAE: return 'R';
        case 0xA0: return ' ';
    }
    return '?';
}

static SDL_Texture *glyph_texture(SDL_Renderer *renderer, unsigned char chr)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    const uint8_t *src;
    int index, x, y;

    if (renderer != glyph_renderer)
    {
        /* the textures of another renderer: that renderer destroys them */
        memset(glyph, 0, sizeof(glyph));
        glyph_renderer = renderer;
    }
    if (chr >= 128) return NULL;
    if (glyph[chr] != NULL) return glyph[chr];
    index = vkfont_chr_index[chr];
    if (index < 0) return NULL;

    surface = SDL_CreateRGBSurface(0, vkfont_chr_width_pixels, vkfont_chr_height, 32,
                                   0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (surface == NULL) return NULL;
    src = vkfont_chr_data + vkfont_chr_size * index;
    for (y = 0; y < vkfont_chr_height; y++)
    {
        uint32_t *dst = (uint32_t *)((uint8_t *)surface->pixels + y * surface->pitch);

        for (x = 0; x < vkfont_chr_width_pixels; x++)
        {
            dst[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 0xFFFFFFFFu : 0;
        }
        src += vkfont_chr_width_bytes;
    }
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture != NULL)
    {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    }
    glyph[chr] = texture;
    return texture;
}

/* the glyphs are narrower than their cell: the text runs at 3/4 of it */
static int char_advance(int h)
{
    int advance = vkfont_chr_width_pixels * h * 3 / (vkfont_chr_height * 4);

    return (advance < 1) ? 1 : advance;
}

int Game_UiTextWidth(const char *text, int h)
{
    return (int)strlen(text) * char_advance(h);
}

void Game_UiText(SDL_Renderer *renderer, const char *text, int x, int y, int h)
{
    int advance = char_advance(h), cell = vkfont_chr_width_pixels * h / vkfont_chr_height;
    Uint8 r, g, b, a;
    SDL_Rect dst;

    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    dst.y = y;
    dst.w = cell;
    dst.h = h;
    for (; *text != 0; text++, x += advance)
    {
        unsigned char chr = plain_char((unsigned char)*text);
        SDL_Texture *texture;

        /* the keyboard's font shows its space key */
        if (chr == ' ') continue;
        texture = glyph_texture(renderer, chr);
        if (texture == NULL) continue;
        SDL_SetTextureColorMod(texture, r, g, b);
        SDL_SetTextureAlphaMod(texture, a);
        dst.x = x - (cell - advance) / 2;
        SDL_RenderCopy(renderer, texture, NULL, &dst);
    }
}

void Game_UiFillRect(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    SDL_Rect rect;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(renderer, &rect);
}
