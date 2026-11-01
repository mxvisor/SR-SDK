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

/* The on-screen gamepad of the Android build: draws the buttons over the game
 * and turns touches into key presses (EmulateKey).  The engine is shared; a
 * layout, meaning the buttons, their keys and where they sit, is a header
 * that defines `gp_buttons[]`, `GP_NBUTTONS` and `gp_layout(W, H)`.  A game
 * with its own layout has game/Game_touchlayout.h; the others get
 * touchlayout_default.h.  The desktop builds get no-ops. */

#include <string.h>
#include "Game_defs.h"
#include "Game_vars.h"
#include "input.h"
#include "touchgamepad.h"
#include "VKfont2.h"

#if defined(__ANDROID__)

typedef struct {
    int x, y, w, h;
    SDL_Keycode key;
    int arrow;          /* 0 none, 1 up, 2 down, 3 left, 4 right */
    const char *label;  /* text on non-arrow buttons */
    Uint8 r, g, b;      /* the colour: use the GP_* ones below */
    Sint64 finger;      /* the SDL_FingerID holding it, -1 = free */
} GPButton;

static int gp_W, gp_H;
static SDL_Renderer *gp_renderer;

#define GP_GREY   150, 150, 160
#define GP_LIGHT  170, 170, 180
#define GP_ORANGE 190, 130, 60
#define GP_RED    200, 70,  70
#define GP_BLUE   80,  120, 200

#if defined(__has_include)
#if __has_include("Game_touchlayout.h")
#include "Game_touchlayout.h"
#define GP_HAVE_LAYOUT
#endif
#endif
#if !defined(GP_HAVE_LAYOUT)
#include "touchlayout_default.h"
#endif

static SDL_Texture *gp_glyph[128];
static int gp_stamp;

static void gp_ensure_layout(void)
{
    int W, H;

    if (SDL_GetRendererOutputSize(gp_renderer, &W, &H) != 0) return;
    if (W == gp_W && H == gp_H) return;
    gp_layout(W, H);
    gp_W = W;
    gp_H = H;
}

static SDL_Texture *gp_glyph_texture(int chr)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    const uint8_t *src;
    uint8_t *dst;
    int idx, x, y, byte, bit;

    if (chr < 0 || chr >= 128) return NULL;
    if (gp_glyph[chr] != NULL) return gp_glyph[chr];

    idx = vkfont_chr_index[chr];
    if (idx < 0) return NULL;

    surf = SDL_CreateRGBSurface(0, vkfont_chr_width_pixels, vkfont_chr_height,
                                32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (surf == NULL) return NULL;

    src = vkfont_chr_data + vkfont_chr_size * idx;
    for (y = 0; y < vkfont_chr_height; y++)
    {
        dst = (uint8_t *)surf->pixels + y * surf->pitch;
        for (byte = 0; byte < vkfont_chr_width_bytes; byte++)
        {
            for (bit = 0; bit < 8; bit++)
            {
                x = byte * 8 + bit;
                if (x >= vkfont_chr_width_pixels) break;
                if (src[byte] & (0x80 >> bit))
                {
                    ((uint32_t *)dst)[x] = 0xFFFFFFFF;
                }
                else
                {
                    ((uint32_t *)dst)[x] = 0x00000000;
                }
            }
        }
        src += vkfont_chr_width_bytes;
    }

    tex = SDL_CreateTextureFromSurface(gp_renderer, surf);
    SDL_FreeSurface(surf);
    if (tex != NULL) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    gp_glyph[chr] = tex;
    return tex;
}

static void gp_draw_text(const char *str, int cx, int cy, int maxw, int maxh)
{
    float scale, gw, gh, total, cw, chh, x0, y0;
    int i, n;

    n = (int)strlen(str);
    if (n <= 0) return;

    gw = (float)vkfont_chr_width_pixels;
    gh = (float)vkfont_chr_height;
    scale = (float)maxh / gh;
    if (gw * scale * n > (float)maxw) scale = ((float)maxw / gw) / (float)n;
    if (scale <= 0.0f) return;

    cw = gw * scale;
    chh = gh * scale;
    total = cw * n;
    x0 = (float)cx - total * 0.5f;
    y0 = (float)cy - chh * 0.5f;

    for (i = 0; i < n; i++)
    {
        SDL_Texture *tex = gp_glyph_texture((unsigned char)str[i]);
        SDL_Rect dst;

        if (tex == NULL) continue;
        dst.x = (int)(x0 + cw * i);
        dst.y = (int)y0;
        dst.w = (int)(cw + 0.5f);
        dst.h = (int)(chh + 0.5f);
        SDL_RenderCopy(gp_renderer, tex, NULL, &dst);
    }
}

static void gp_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int tmp, y;

    /* sort the three vertices by y */
    if (y0 > y1) { tmp = x0; x0 = x1; x1 = tmp; tmp = y0; y0 = y1; y1 = tmp; }
    if (y1 > y2) { tmp = x1; x1 = x2; x2 = tmp; tmp = y1; y1 = y2; y2 = tmp; }
    if (y0 > y1) { tmp = x0; x0 = x1; x1 = tmp; tmp = y0; y0 = y1; y1 = tmp; }

    for (y = y0; y <= y2; y++)
    {
        float t = (y2 == y0) ? 0.0f : (float)(y - y0) / (float)(y2 - y0);
        float xa = x0 + (x2 - x0) * t;
        float xb;
        int xleft, xright;

        if (y < y1)
        {
            float s = (y1 == y0) ? 0.0f : (float)(y - y0) / (float)(y1 - y0);
            xb = x0 + (x1 - x0) * s;
        }
        else
        {
            float s = (y2 == y1) ? 0.0f : (float)(y - y1) / (float)(y2 - y1);
            xb = x1 + (x2 - x1) * s;
        }

        xleft = (int)(xa < xb ? xa : xb);
        xright = (int)(xa < xb ? xb : xa);
        SDL_RenderDrawLine(gp_renderer, xleft, y, xright, y);
    }
}

static void gp_draw_arrow(int dir, int x, int y, int w, int h)
{
    int pad = w / 5;
    int cx = x + w / 2, cy = y + h / 2;

    if (dir == 1)      gp_fill_triangle(cx,        y + pad,     x + pad,     y + h - pad, x + w - pad, y + h - pad);
    else if (dir == 2) gp_fill_triangle(cx,        y + h - pad, x + pad,     y + pad,     x + w - pad, y + pad);
    else if (dir == 3) gp_fill_triangle(x + pad,   cy,          x + w - pad, y + pad,     x + w - pad, y + h - pad);
    else if (dir == 4) gp_fill_triangle(x + w - pad, cy,        x + pad,     y + pad,     x + pad,     y + h - pad);
}

static void gp_draw_button(GPButton *btn)
{
    int active = (btn->finger >= 0);
    Uint8 r, g, b, a;
    SDL_Rect rect;

    r = btn->r; g = btn->g; b = btn->b;
    a = active ? 170 : 100;

    rect.x = btn->x;
    rect.y = btn->y;
    rect.w = btn->w;
    rect.h = btn->h;

    SDL_SetRenderDrawBlendMode(gp_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gp_renderer, r, g, b, a);
    SDL_RenderFillRect(gp_renderer, &rect);

    SDL_SetRenderDrawColor(gp_renderer, 255, 255, 255, active ? 230 : 140);
    SDL_RenderDrawRect(gp_renderer, &rect);

    if (btn->arrow)
    {
        SDL_SetRenderDrawColor(gp_renderer, 255, 255, 255, active ? 255 : 200);
        gp_draw_arrow(btn->arrow, btn->x, btn->y, btn->w, btn->h);
    }
    else if (btn->label != NULL)
    {
        SDL_SetRenderDrawColor(gp_renderer, 255, 255, 255, 255);
        gp_draw_text(btn->label, btn->x + btn->w / 2, btn->y + btn->h / 2,
                     btn->w * 4 / 5, btn->h * 3 / 4);
    }
}

void TouchGamepad_Draw(SDL_Renderer *renderer)
{
    int i;

    if (renderer == NULL) return;
    gp_renderer = renderer;
    gp_ensure_layout();

    for (i = 0; i < GP_NBUTTONS; i++)
    {
        gp_draw_button(&gp_buttons[i]);
    }
}

void TouchGamepad_ForgetRenderer(void)
{
    memset(gp_glyph, 0, sizeof(gp_glyph));
    gp_renderer = NULL;
}

int TouchGamepad_Stamp(void)
{
    return gp_stamp;
}

static int gp_button_at(int x, int y)
{
    int i;

    for (i = 0; i < GP_NBUTTONS; i++)
    {
        if (x >= gp_buttons[i].x && x < gp_buttons[i].x + gp_buttons[i].w &&
            y >= gp_buttons[i].y && y < gp_buttons[i].y + gp_buttons[i].h)
        {
            return i;
        }
    }
    return -1;
}

static int gp_button_with_finger(Sint64 finger)
{
    int i;

    for (i = 0; i < GP_NBUTTONS; i++)
    {
        if (gp_buttons[i].finger == finger) return i;
    }
    return -1;
}

static void gp_press(int idx, Sint64 finger)
{
    gp_buttons[idx].finger = finger;
    gp_stamp++;
    EmulateKey(SDL_KEYDOWN, gp_buttons[idx].key);
}

static void gp_release(int idx)
{
    gp_buttons[idx].finger = -1;
    gp_stamp++;
    EmulateKey(SDL_KEYUP, gp_buttons[idx].key);
}

int TouchGamepad_Event(SDL_Event *event)
{
    int x, y, idx, cur;
    Sint64 finger;

    switch (event->type)
    {
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            break;
        default:
            return 0;
    }

    x = (int)(event->tfinger.x * gp_W);
    y = (int)(event->tfinger.y * gp_H);
    finger = event->tfinger.fingerId;

    if (event->type == SDL_FINGERDOWN)
    {
        idx = gp_button_at(x, y);
        if (idx >= 0 && gp_buttons[idx].finger < 0) gp_press(idx, finger);
    }
    else if (event->type == SDL_FINGERUP)
    {
        cur = gp_button_with_finger(finger);
        if (cur >= 0) gp_release(cur);
    }
    else /* SDL_FINGERMOTION */
    {
        cur = gp_button_with_finger(finger);
        if (cur >= 0)
        {
            if (!(x >= gp_buttons[cur].x && x < gp_buttons[cur].x + gp_buttons[cur].w &&
                  y >= gp_buttons[cur].y && y < gp_buttons[cur].y + gp_buttons[cur].h))
            {
                gp_release(cur);
                cur = -1;
            }
        }
        if (cur < 0)
        {
            idx = gp_button_at(x, y);
            if (idx >= 0 && gp_buttons[idx].finger < 0) gp_press(idx, finger);
        }
    }

    return 1;
}

int TouchGamepad_ButtonAt(float x, float y)
{
    return gp_button_at((int)(x * gp_W), (int)(y * gp_H)) >= 0;
}

int TouchGamepad_HeldKey(int index, SDL_Keycode *key)
{
    if (index < 0 || index >= GP_NBUTTONS) return 0;
    *key = (gp_buttons[index].finger >= 0) ? gp_buttons[index].key : SDLK_UNKNOWN;
    return 1;
}

#else /* !__ANDROID__ */

void TouchGamepad_ForgetRenderer(void)
{
}

int TouchGamepad_Stamp(void)
{
    return 0;
}

void TouchGamepad_Draw(SDL_Renderer *renderer)
{
    (void)renderer;
}

int TouchGamepad_Event(SDL_Event *event)
{
    (void)event;
    return 0;
}

int TouchGamepad_ButtonAt(float x, float y)
{
    (void)x;
    (void)y;
    return 0;
}

int TouchGamepad_HeldKey(int index, SDL_Keycode *key)
{
    (void)index;
    (void)key;
    return 0;
}

#endif /* __ANDROID__ */
