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
#include "Game_hooks.h"
#include "Game_ui.h"
#include "Game_osd.h"
#include "touchgamepad.h"
#include "VKfont2.h"
#include "Game_font_cyr.h"

static const Game_UiClient *ui_client;
static int ui_stamp;
static int ui_width, ui_height;
static void (*ui_repaint)(void);

/* Game_ui.h counts the glyphs */
typedef char glyph_count_check[(GAME_UI_GLYPHS == 128 + FONT_CYR_COUNT) ? 1 : -1];

static SDL_Renderer *glyph_renderer;
static SDL_Texture *glyph[GAME_UI_GLYPHS];

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

void Game_UiWindowPoint(Uint32 window_id, int *x, int *y)
{
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    int ww, wh;

    if (window == NULL) return;
    SDL_GetWindowSize(window, &ww, &wh);
    if (ww > 0 && wh > 0 && ui_width > 0 && ui_height > 0)
    {
        *x = (int)((int64_t)*x * ui_width / ww);
        *y = (int)((int64_t)*y * ui_height / wh);
    }
}

void Game_UiPictureRect(int window_w, int window_h, int width, int height, SDL_Rect *rect)
{
#if defined(__ANDROID__) && (GAME_WINAPI_TOUCH_GAMEPAD || !defined(GAME_WINAPI))
    /* as DirectDraw's CopyToDisplay: every mode fills a 4:3 display (640x400 is
     * not 4:3 in pixels) */
    width = 4;
    height = 3;
#endif
    if (width < 1 || height < 1)
    {
        width = window_w;
        height = window_h;
    }
    if ((int64_t)window_w * height > (int64_t)window_h * width)
    {
        rect->h = window_h;
        rect->w = (int)((int64_t)window_h * width / height);
    }
    else
    {
        rect->w = window_w;
        rect->h = (int)((int64_t)window_w * height / width);
    }
    rect->x = (window_w - rect->w) / 2;
    rect->y = (window_h - rect->h) / 2;
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

/* the ASCII character for a Latin-1 letter (without its accent) and for the
 * quotes and dashes, 0 for none */
static int plain_char(uint32_t code)
{
    static const char latin1[] =
        "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"      /* 0xC0 - 0xDF */
        "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";     /* 0xE0 - 0xFF */

    if (code >= 0xC0 && code <= 0xFF) return latin1[code - 0xC0];
    switch (code)
    {
        case 0x2018: case 0x2019: case 0xB4: return '\'';
        case 0x201C: case 0x201D: case 0xAB: case 0xBB: return '"';
        case 0x2013: case 0x2014: case 0xAD: return '-';
        case 0xA9: return 'c';
        case 0xAE: return 'R';
        case 0xA0: return ' ';
    }
    return 0;
}

uint32_t Game_UiNextChar(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t code = 0;
    int extra, i;

    if (s[0] == 0) return 0;
    if (s[0] < 0x80) { *text += 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0) { code = s[0] & 0x1F; extra = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { code = s[0] & 0x0F; extra = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { code = s[0] & 0x07; extra = 3; }
    else extra = 0;
    for (i = 1; i <= extra; i++)
    {
        if ((s[i] & 0xC0) != 0x80) break;
        code = (code << 6) | (s[i] & 0x3F);
    }
    if (extra != 0 && i > extra && code >= ((extra == 1) ? 0x80u : (extra == 2) ? 0x800u : 0x10000u))
    {
        *text += extra + 1;
        return code;
    }
    /* not UTF-8: a byte of Windows-1252 */
    *text += 1;
    switch (s[0])
    {
        case 0x91: return 0x2018;
        case 0x92: return 0x2019;
        case 0x93: return 0x201C;
        case 0x94: return 0x201D;
        case 0x96: return 0x2013;
        case 0x97: return 0x2014;
    }
    return s[0];
}

int Game_UiGlyph(uint32_t code, const uint8_t **bits)
{
    int letter = -1, index;

    if (code >= 0x410 && code < 0x450) letter = (int)(code - 0x410);
    else if (code == 0x401) letter = 64;
    else if (code == 0x451) letter = 65;
    if (letter >= 0)
    {
        if (font_cyr_index[letter] >= 0)
        {
            *bits = font_cyr_data + vkfont_chr_size * font_cyr_index[letter];
            return 128 + letter;
        }
        code = (unsigned char)font_cyr_latin[letter];
    }
    else if (code >= 0x80)
    {
        code = (uint32_t)plain_char(code);
    }
    if (code == 0 || code >= 0x80) return -1;
    index = vkfont_chr_index[code];
    if (index < 0) return -1;
    *bits = vkfont_chr_data + vkfont_chr_size * index;
    return (int)code;
}

int Game_UiTextDrawable(const char *text, size_t length)
{
    const char *end = text + length;

    while (text < end && *text != 0)
    {
        const uint8_t *bits;
        uint32_t code = Game_UiNextChar(&text);

        if (code < 0x20 || Game_UiGlyph(code, &bits) < 0) return 0;
    }
    return 1;
}

static SDL_Texture *glyph_texture(SDL_Renderer *renderer, uint32_t code)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    const uint8_t *src;
    int slot, x, y;

    if (renderer != glyph_renderer)
    {
        /* the textures of another renderer: that renderer destroys them */
        memset(glyph, 0, sizeof(glyph));
        glyph_renderer = renderer;
    }
    slot = Game_UiGlyph(code, &src);
    if (slot < 0)
    {
        /* a character the font has not got */
        if (code < 0x80) return NULL;
        slot = Game_UiGlyph('?', &src);
    }
    if (glyph[slot] != NULL) return glyph[slot];

    surface = SDL_CreateRGBSurface(0, vkfont_chr_width_pixels, vkfont_chr_height, 32,
                                   0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (surface == NULL) return NULL;
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
    glyph[slot] = texture;
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
    int count = 0;

    while (Game_UiNextChar(&text) != 0) count++;
    return count * char_advance(h);
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
    for (; *text != 0; x += advance)
    {
        uint32_t code = Game_UiNextChar(&text);
        SDL_Texture *texture;

        /* the keyboard's font shows its space key */
        if (code == ' ' || code == 0xA0) continue;
        texture = glyph_texture(renderer, code);
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
