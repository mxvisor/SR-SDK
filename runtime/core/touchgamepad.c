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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "Game_defs.h"
#include "Game_vars.h"
#include "input.h"
#include "touchgamepad.h"
#include "Game_ui.h"

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

static SDL_Texture *gp_glyph[GAME_UI_GLYPHS];
static int gp_stamp;

/* A pad is a cross of four arrow buttons (up, down, left, right around an empty
 * centre), found in the layout by its geometry.  One finger on the pad steers in
 * eight directions: it holds the arrow buttons of the direction it points to,
 * two of them on a diagonal.  Arrows that are not in a cross stay plain buttons. */
#define GP_MAXPADS 2
enum { GP_PAD_UP, GP_PAD_DOWN, GP_PAD_LEFT, GP_PAD_RIGHT };

typedef struct {
    int btn[4];                 /* indexes into gp_buttons */
    int x, y, w, h;             /* the box around the four */
    Sint64 finger;              /* the SDL_FingerID steering it, -1 = free */
} GPPad;

static GPPad gp_pad[GP_MAXPADS];
static int gp_npads;

static int gp_cx(const GPButton *b) { return b->x + b->w / 2; }
static int gp_cy(const GPButton *b) { return b->y + b->h / 2; }

static int gp_find_arrow(int arrow, int taken_from)
{
    int i;

    for (i = taken_from; i < GP_NBUTTONS; i++)
    {
        if (gp_buttons[i].arrow == arrow && gp_buttons[i].w > 0) return i;
    }
    return -1;
}

static void gp_find_pads(void)
{
    GPPad found[GP_MAXPADS];
    int n = 0, u, d, l, r;

    for (u = gp_find_arrow(1, 0); u >= 0 && n < GP_MAXPADS; u = gp_find_arrow(1, u + 1))
    {
        const GPButton *bu = &gp_buttons[u];

        for (d = gp_find_arrow(2, 0); d >= 0; d = gp_find_arrow(2, d + 1))
        {
            const GPButton *bd = &gp_buttons[d];
            int midy;

            if (abs(gp_cx(bd) - gp_cx(bu)) >= bu->w / 2 || gp_cy(bd) <= gp_cy(bu)) continue;
            midy = (gp_cy(bu) + gp_cy(bd)) / 2;

            for (l = gp_find_arrow(3, 0); l >= 0; l = gp_find_arrow(3, l + 1))
            {
                const GPButton *bl = &gp_buttons[l];

                if (abs(gp_cy(bl) - midy) >= bu->h / 2 || gp_cx(bl) >= gp_cx(bu) || gp_cx(bu) - gp_cx(bl) >= 2 * bu->w) continue;

                for (r = gp_find_arrow(4, 0); r >= 0; r = gp_find_arrow(4, r + 1))
                {
                    const GPButton *br = &gp_buttons[r];
                    int x0, y0, x1, y1;

                    if (abs(gp_cy(br) - midy) >= bu->h / 2 || gp_cx(br) <= gp_cx(bu) || gp_cx(br) - gp_cx(bu) >= 2 * bu->w) continue;

                    x0 = bl->x;                 x1 = br->x + br->w;
                    y0 = bu->y;                 y1 = bd->y + bd->h;
                    found[n].btn[GP_PAD_UP] = u;
                    found[n].btn[GP_PAD_DOWN] = d;
                    found[n].btn[GP_PAD_LEFT] = l;
                    found[n].btn[GP_PAD_RIGHT] = r;
                    found[n].x = x0;  found[n].y = y0;
                    found[n].w = x1 - x0;  found[n].h = y1 - y0;
                    found[n].finger = -1;
                    n++;
                    goto next_up;
                }
            }
        }
        next_up: ;
    }

    /* the finger of a pad that was already there goes on steering it */
    for (d = 0; d < n; d++)
    {
        for (u = 0; u < gp_npads; u++)
        {
            if (gp_pad[u].btn[GP_PAD_UP] == found[d].btn[GP_PAD_UP]) found[d].finger = gp_pad[u].finger;
        }
    }
    memcpy(gp_pad, found, sizeof(GPPad) * n);
    gp_npads = n;
}

static void gp_ensure_layout(void)
{
    int W, H;

    if (SDL_GetRendererOutputSize(gp_renderer, &W, &H) != 0) return;
    if (W == gp_W && H == gp_H) return;
    gp_layout(W, H);
    gp_W = W;
    gp_H = H;
    gp_find_pads();
}

static SDL_Texture *gp_glyph_texture(uint32_t code)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    const uint8_t *src;
    int slot, x, y;

    slot = Game_UiGlyph(code, &src);
    if (slot < 0) return NULL;
    if (gp_glyph[slot] != NULL) return gp_glyph[slot];

    surf = SDL_CreateRGBSurface(0, GAME_UI_GLYPH_W, GAME_UI_GLYPH_H,
                                32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (surf == NULL) return NULL;

    for (y = 0; y < GAME_UI_GLYPH_H; y++)
    {
        uint32_t *dst = (uint32_t *)((uint8_t *)surf->pixels + y * surf->pitch);

        for (x = 0; x < GAME_UI_GLYPH_W; x++)
        {
            dst[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 0xFFFFFFFFu : 0;
        }
        src += GAME_UI_GLYPH_PITCH;
    }

    tex = SDL_CreateTextureFromSurface(gp_renderer, surf);
    SDL_FreeSurface(surf);
    if (tex != NULL) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    gp_glyph[slot] = tex;
    return tex;
}

static void gp_draw_text(const char *str, int cx, int cy, int maxw, int maxh)
{
    float scale, gw, gh, total, cw, chh, x0, y0;
    int i, n;

    {
        const char *p = str;

        for (n = 0; Game_UiNextChar(&p) != 0; n++) {}
    }
    if (n <= 0) return;

    gw = (float)GAME_UI_GLYPH_W;
    gh = (float)GAME_UI_GLYPH_H;
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
        SDL_Texture *tex = gp_glyph_texture(Game_UiNextChar(&str));
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

/* Rounded corners: a quarter circle of radius r per corner, drawn row by row.
 * inset(row) is how far the row starts from the left edge of the rectangle. */
static int gp_corner_radius(const SDL_Rect *rect)
{
    int r = (rect->w < rect->h ? rect->w : rect->h) / 4;

    return (r < 2) ? 0 : r;
}

static int gp_row_inset(int r, int h, int row)
{
    float c;

    if (row >= h - r) row = h - 1 - row;
    if (row < 0 || row >= r) return 0;
    c = (float)(r - row) - 0.5f;
    return r - (int)(sqrtf((float)r * (float)r - c * c) + 0.5f);
}

static void gp_fill_round_rect(const SDL_Rect *rect)
{
    int r = gp_corner_radius(rect), row, ins;

    for (row = 0; row < rect->h; row++)
    {
        ins = gp_row_inset(r, rect->h, row);
        SDL_RenderDrawLine(gp_renderer, rect->x + ins, rect->y + row, rect->x + rect->w - 1 - ins, rect->y + row);
    }
}

static void gp_outline_round_rect(const SDL_Rect *rect)
{
    int r = gp_corner_radius(rect), h = rect->h, row, ins, prev, next, reach;

    for (row = 0; row < h; row++)
    {
        ins = gp_row_inset(r, h, row);
        if (row == 0 || row == h - 1)
        {
            SDL_RenderDrawLine(gp_renderer, rect->x + ins, rect->y + row, rect->x + rect->w - 1 - ins, rect->y + row);
            continue;
        }
        /* the side of this row reaches over to the next row's side, so the arc has no gaps */
        prev = gp_row_inset(r, h, row - 1);
        next = gp_row_inset(r, h, row + 1);
        reach = (prev > next ? prev : next) - 1;
        if (reach < ins) reach = ins;
        SDL_RenderDrawLine(gp_renderer, rect->x + ins, rect->y + row, rect->x + reach, rect->y + row);
        SDL_RenderDrawLine(gp_renderer, rect->x + rect->w - 1 - reach, rect->y + row, rect->x + rect->w - 1 - ins, rect->y + row);
    }
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
    gp_fill_round_rect(&rect);

    SDL_SetRenderDrawColor(gp_renderer, 255, 255, 255, active ? 230 : 140);
    gp_outline_round_rect(&rect);

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

static int gp_in_pad(const GPPad *pad, int x, int y, int margin)
{
    return x >= pad->x - margin && x < pad->x + pad->w + margin &&
           y >= pad->y - margin && y < pad->y + pad->h + margin;
}

static int gp_pad_at(int x, int y)
{
    int i;

    for (i = 0; i < gp_npads; i++)
    {
        if (gp_in_pad(&gp_pad[i], x, y, 0)) return i;
    }
    return -1;
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

/* The arrows a finger at (x, y) asks of the pad: the 360 degrees around its centre
 * are eight sectors of 45 degrees, the diagonal ones hold two arrows; the middle
 * of the cross (half a button) holds none.  press = -1: nothing (the finger left). */
static void gp_pad_steer(GPPad *pad, int x, int y, int press)
{
    const GPButton *bu = &gp_buttons[pad->btn[GP_PAD_UP]];
    int dx = x - (pad->x + pad->w / 2), dy = y - (pad->y + pad->h / 2);
    int ax = abs(dx), ay = abs(dy), want[4] = {0, 0, 0, 0}, i, half = bu->w / 2;

    if (press >= 0 && (int64_t)dx * dx + (int64_t)dy * dy >= (int64_t)half * half)
    {
        /* tan(22.5 degrees) = 0.414: below it the other axis does not count */
        int vertical = (ay * 1000 >= ax * 414), horizontal = (ax * 1000 >= ay * 414);

        if (vertical)   want[dy < 0 ? GP_PAD_UP : GP_PAD_DOWN] = 1;
        if (horizontal) want[dx < 0 ? GP_PAD_LEFT : GP_PAD_RIGHT] = 1;
    }

    /* press what is new before releasing what is gone: no gap between two directions */
    for (i = 0; i < 4; i++)
    {
        if (want[i] && gp_buttons[pad->btn[i]].finger < 0) gp_press(pad->btn[i], pad->finger);
    }
    for (i = 0; i < 4; i++)
    {
        if (!want[i] && gp_buttons[pad->btn[i]].finger == pad->finger) gp_release(pad->btn[i]);
    }
}

static int gp_pad_with_finger(Sint64 finger)
{
    int i;

    for (i = 0; i < gp_npads; i++)
    {
        if (gp_pad[i].finger == finger) return i;
    }
    return -1;
}

int TouchGamepad_Event(SDL_Event *event)
{
    int x, y, idx, cur, pad;
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

    pad = gp_pad_with_finger(finger);

    if (event->type == SDL_FINGERUP)
    {
        if (pad >= 0)
        {
            gp_pad_steer(&gp_pad[pad], x, y, -1);
            gp_pad[pad].finger = -1;
        }
        cur = gp_button_with_finger(finger);
        if (cur >= 0) gp_release(cur);
    }
    else if (pad >= 0)
    {
        /* a finger steering a pad: it keeps the pad a little beyond its edge, then lets go
         * and may press the buttons it slides onto */
        if (gp_in_pad(&gp_pad[pad], x, y, gp_buttons[gp_pad[pad].btn[GP_PAD_UP]].w / 2))
        {
            gp_pad_steer(&gp_pad[pad], x, y, 1);
        }
        else
        {
            gp_pad_steer(&gp_pad[pad], x, y, -1);
            gp_pad[pad].finger = -1;
            idx = gp_button_at(x, y);
            if (idx >= 0 && gp_buttons[idx].finger < 0) gp_press(idx, finger);
        }
    }
    else if ((idx = gp_pad_at(x, y)) >= 0 && gp_pad[idx].finger < 0 && gp_button_with_finger(finger) < 0)
    {
        gp_pad[idx].finger = finger;
        gp_pad_steer(&gp_pad[idx], x, y, 1);
    }
    else if (event->type == SDL_FINGERDOWN)
    {
        idx = gp_button_at(x, y);
        if (idx >= 0 && gp_buttons[idx].finger < 0) gp_press(idx, finger);
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

/* Another part of the port takes the touches (the window menu, a dialog): what
 * is held is let go, its fingers will not come back here */
void TouchGamepad_ReleaseAll(void)
{
    int i;

    for (i = 0; i < gp_npads; i++) gp_pad[i].finger = -1;
    for (i = 0; i < GP_NBUTTONS; i++)
    {
        if (gp_buttons[i].finger >= 0) gp_release(i);
    }
}

int TouchGamepad_ButtonAt(float x, float y)
{
    return gp_button_at((int)(x * gp_W), (int)(y * gp_H)) >= 0 || gp_pad_at((int)(x * gp_W), (int)(y * gp_H)) >= 0;
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

void TouchGamepad_ReleaseAll(void)
{
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
