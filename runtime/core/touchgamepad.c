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
 * touchlayout_default.h.  gp_layout may depend on the game's state (a mod
 * that adds a button): the game calls TouchGamepad_Relayout when that state
 * changes.  A button of width 0 is hidden: not drawn, never touched.  A
 * button with the key TOUCHGAMEPAD_MOUSE_LEFT holds the mouse's left button
 * instead of a key.  A finger that comes down away from the buttons is the
 * pointer (TouchGamepad_Pointer): SDL makes a mouse of the first finger only,
 * wherever it is, and a mouse of the game that wants the touches away from the
 * buttons reads the pointer instead.
 *
 * A button with the key TOUCHGAMEPAD_KEYBOARD opens the keyboard when it is
 * let go: a PC keyboard (kb_keys) over the lower part of the screen, drawn
 * and touched instead of the layout's buttons.  Its keys press the keys of the
 * same places of a PC keyboard (the game makes them letters: EN/RU changes
 * only what they show); Shift, Ctrl and Alt stay down until the next key is
 * let go.  HIDE, or a touch that comes down above the keyboard and is lifted,
 * closes it.  While it is open no touch is the pointer or SDL's mouse.  The
 * desktop builds get no-ops. */

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

/* ---- the keyboard ---- */

/* the keys of the keyboard itself */
#define KB_HIDE ((SDL_Keycode)-4)
#define KB_LANG ((SDL_Keycode)-5)
/* an empty place in a row */
#define KB_GAP  SDLK_UNKNOWN

typedef struct {
    SDL_Keycode key;
    int units;              /* the width in quarters of a key; 0 ends the row */
    int arrow;
    const char *en, *ru;    /* the labels; ru NULL: the same as en */
    Uint8 r, g, b;
} KBKey;

#define KB_ROWS 6
/* a row is 15 keys (60 quarters) wide */
#define KB_ROW_UNITS 60
#define KB_END {KB_GAP, 0, 0, NULL, NULL, 0, 0, 0}
#define KB_K(key, en, ru) {key, 4, 0, en, ru, GP_GREY}
#define KB_SP(key, units, label, colour) {key, units, 0, label, NULL, colour}
#define KB_ARROW(key, arrow) {key, 4, arrow, NULL, NULL, GP_LIGHT}

/* the rows from the top; the Russian letters are those of the same places */
static const KBKey kb_keys[] = {
    KB_SP(SDLK_ESCAPE, 6, "ESC", GP_RED), KB_SP(KB_GAP, 2, NULL, GP_GREY),
    KB_K(SDLK_F1, "F1", NULL), KB_K(SDLK_F2, "F2", NULL), KB_K(SDLK_F3, "F3", NULL), KB_K(SDLK_F4, "F4", NULL),
    KB_SP(KB_GAP, 2, NULL, GP_GREY),
    KB_K(SDLK_F5, "F5", NULL), KB_K(SDLK_F6, "F6", NULL), KB_K(SDLK_F7, "F7", NULL), KB_K(SDLK_F8, "F8", NULL),
    KB_SP(KB_GAP, 2, NULL, GP_GREY),
    KB_K(SDLK_F9, "F9", NULL), KB_K(SDLK_F10, "F10", NULL), KB_K(SDLK_F11, "F11", NULL), KB_K(SDLK_F12, "F12", NULL),
    KB_END,
    KB_K(SDLK_BACKQUOTE, "`", "\xd0\x81"),
    KB_K(SDLK_1, "1", NULL), KB_K(SDLK_2, "2", NULL), KB_K(SDLK_3, "3", NULL), KB_K(SDLK_4, "4", NULL),
    KB_K(SDLK_5, "5", NULL), KB_K(SDLK_6, "6", NULL), KB_K(SDLK_7, "7", NULL), KB_K(SDLK_8, "8", NULL),
    KB_K(SDLK_9, "9", NULL), KB_K(SDLK_0, "0", NULL), KB_K(SDLK_MINUS, "-", NULL), KB_K(SDLK_EQUALS, "=", NULL),
    KB_SP(SDLK_BACKSPACE, 8, "BKSP", GP_BLUE),
    KB_END,
    KB_SP(SDLK_TAB, 6, "TAB", GP_BLUE),
    KB_K(SDLK_q, "Q", "\xd0\x99"), KB_K(SDLK_w, "W", "\xd0\xa6"), KB_K(SDLK_e, "E", "\xd0\xa3"),
    KB_K(SDLK_r, "R", "\xd0\x9a"), KB_K(SDLK_t, "T", "\xd0\x95"), KB_K(SDLK_y, "Y", "\xd0\x9d"),
    KB_K(SDLK_u, "U", "\xd0\x93"), KB_K(SDLK_i, "I", "\xd0\xa8"), KB_K(SDLK_o, "O", "\xd0\xa9"),
    KB_K(SDLK_p, "P", "\xd0\x97"), KB_K(SDLK_LEFTBRACKET, "[", "\xd0\xa5"), KB_K(SDLK_RIGHTBRACKET, "]", "\xd0\xaa"),
    KB_SP(SDLK_BACKSLASH, 6, "\\", GP_GREY),
    KB_END,
    KB_SP(KB_GAP, 7, NULL, GP_GREY),
    KB_K(SDLK_a, "A", "\xd0\xa4"), KB_K(SDLK_s, "S", "\xd0\xab"), KB_K(SDLK_d, "D", "\xd0\x92"),
    KB_K(SDLK_f, "F", "\xd0\x90"), KB_K(SDLK_g, "G", "\xd0\x9f"), KB_K(SDLK_h, "H", "\xd0\xa0"),
    KB_K(SDLK_j, "J", "\xd0\x9e"), KB_K(SDLK_k, "K", "\xd0\x9b"), KB_K(SDLK_l, "L", "\xd0\x94"),
    KB_K(SDLK_SEMICOLON, ";", "\xd0\x96"), KB_K(SDLK_QUOTE, "'", "\xd0\xad"),
    KB_SP(SDLK_RETURN, 9, "ENTER", GP_BLUE),
    KB_END,
    KB_SP(SDLK_LSHIFT, 9, "SHIFT", GP_BLUE),
    KB_K(SDLK_z, "Z", "\xd0\xaf"), KB_K(SDLK_x, "X", "\xd0\xa7"), KB_K(SDLK_c, "C", "\xd0\xa1"),
    KB_K(SDLK_v, "V", "\xd0\x9c"), KB_K(SDLK_b, "B", "\xd0\x98"), KB_K(SDLK_n, "N", "\xd0\xa2"),
    KB_K(SDLK_m, "M", "\xd0\xac"), KB_K(SDLK_COMMA, ",", "\xd0\x91"), KB_K(SDLK_PERIOD, ".", "\xd0\xae"),
    KB_K(SDLK_SLASH, "/", "."),
    KB_SP(KB_GAP, 3, NULL, GP_GREY), KB_ARROW(SDLK_UP, 1),
    KB_END,
    KB_SP(SDLK_LCTRL, 6, "CTRL", GP_BLUE), KB_SP(SDLK_LALT, 6, "ALT", GP_BLUE),
    {KB_LANG, 6, 0, "EN", "RU", GP_BLUE},
    KB_SP(SDLK_SPACE, 22, "SPACE", GP_GREY), KB_SP(KB_HIDE, 8, "HIDE", GP_ORANGE),
    KB_ARROW(SDLK_LEFT, 3), KB_ARROW(SDLK_DOWN, 2), KB_ARROW(SDLK_RIGHT, 4),
    KB_END,
};
#define KB_NKEYS ((int)(sizeof(kb_keys) / sizeof(kb_keys[0])))

static GPButton kb_buttons[KB_NKEYS];
static const KBKey *kb_key[KB_NKEYS];
/* Shift, Ctrl or Alt that waits for the next key */
static Uint8 kb_armed[KB_NKEYS];
static int kb_count, kb_ru;
/* the box around the keys */
static int kb_x0, kb_y0, kb_x1, kb_y1;
/* the finger that came down beside the keys: it closes the keyboard when it is lifted */
static Sint64 kb_close_finger = -1;
/* the keyboard is drawn and touched instead of the layout's buttons; the
 * main thread's, read by the presenter's */
static volatile int gp_keyboard;
/* what a button asked of the keyboard (-1: nothing): done once the event is */
static int gp_keyboard_wanted = -1;
/* SDL's buttons of a touch that came down on the keyboard, until they go up (a bit each) */
static int gp_mouse_eaten;

/* the buttons drawn and touched now */
static GPButton *gp_current(int *count)
{
    if (gp_keyboard)
    {
        *count = kb_count;
        return kb_buttons;
    }
    *count = GP_NBUTTONS;
    return gp_buttons;
}

static void kb_labels(void)
{
    int i;

    for (i = 0; i < kb_count; i++)
    {
        kb_buttons[i].label = (kb_ru && kb_key[i]->ru != NULL) ? kb_key[i]->ru : kb_key[i]->en;
    }
}

/* the rows at the bottom of the screen, as wide as the screen (a key at most
 * 1.6 times as wide as it is high), together 62 % of its height */
static void kb_layout(int W, int H)
{
    const KBKey *k;
    int m = H / 40, kw, kh, gap, units = 0, y, n = 0;

    kh = (H * 62 / 100) / KB_ROWS;
    kw = (W - 2 * m) / (KB_ROW_UNITS / 4);
    if (kw > kh * 8 / 5) kw = kh * 8 / 5;
    gap = kh / 12;
    if (gap < 1) gap = 1;
    kb_x0 = (W - kw * (KB_ROW_UNITS / 4)) / 2;
    kb_x1 = kb_x0 + kw * (KB_ROW_UNITS / 4);
    kb_y1 = H - m;
    kb_y0 = kb_y1 - KB_ROWS * kh;

    y = kb_y0;
    for (k = kb_keys; k < kb_keys + KB_NKEYS; k++)
    {
        if (k->units == 0)
        {
            y += kh;
            units = 0;
            continue;
        }
        if (k->key != KB_GAP)
        {
            GPButton *b = &kb_buttons[n];
            int left = kb_x0 + units * kw / 4, right = kb_x0 + (units + k->units) * kw / 4;

            b->x = left + gap / 2;
            b->y = y + gap / 2;
            b->w = right - left - gap;
            b->h = kh - gap;
            b->key = k->key;
            b->arrow = k->arrow;
            b->r = k->r;
            b->g = k->g;
            b->b = k->b;
            b->finger = -1;
            kb_key[n] = k;
            kb_armed[n] = 0;
            n++;
        }
        units += k->units;
    }
    kb_count = n;
    kb_labels();
}

static int kb_modifier(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_LSHIFT: return KMOD_LSHIFT;
        case SDLK_LCTRL:  return KMOD_LCTRL;
        case SDLK_LALT:   return KMOD_LALT;
        default:          return 0;
    }
}

/* the modifiers that wait */
static int kb_mods(void)
{
    int i, mod = 0;

    for (i = 0; i < kb_count; i++)
    {
        if (kb_armed[i]) mod |= kb_modifier(kb_buttons[i].key);
    }
    return mod;
}

/* a modifier goes down or up: the game's key and SDL's state of them (that
 * TranslateMessage reads) */
static void kb_arm(int idx, int arm)
{
    const int bit = kb_modifier(kb_buttons[idx].key);

    if (kb_armed[idx] == arm) return;
    kb_armed[idx] = (Uint8)arm;
    SDL_SetModState(arm ? (SDL_Keymod)(SDL_GetModState() | bit) : (SDL_Keymod)(SDL_GetModState() & ~bit));
    EmulateKeyMod(arm ? SDL_KEYDOWN : SDL_KEYUP, kb_buttons[idx].key, kb_mods());
    gp_stamp++;
}

static void kb_disarm_all(void)
{
    int i;

    for (i = 0; i < kb_count; i++) kb_arm(i, 0);
}

/* the layout of what is shown now, for a screen of W x H */
static void gp_place_all(int W, int H)
{
    if (gp_keyboard)
    {
        kb_layout(W, H);
        gp_npads = 0;
    }
    else
    {
        gp_layout(W, H);
        gp_find_pads();
    }
}

static void gp_ensure_layout(void)
{
    int W, H;

    if (SDL_GetRendererOutputSize(gp_renderer, &W, &H) != 0) return;
    if (W == gp_W && H == gp_H) return;
    gp_place_all(W, H);
    gp_W = W;
    gp_H = H;
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
    int active = (btn->finger >= 0) ||
                 (btn >= kb_buttons && btn < kb_buttons + KB_NKEYS && kb_armed[btn - kb_buttons]);
    Uint8 r, g, b, a;
    SDL_Rect rect;

    if (btn->w <= 0 || btn->h <= 0) return;
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
    GPButton *set;
    int i, n;

    if (renderer == NULL) return;
    gp_renderer = renderer;
    gp_ensure_layout();

    set = gp_current(&n);
    for (i = 0; i < n; i++)
    {
        gp_draw_button(&set[i]);
    }
}

void TouchGamepad_Relayout(void)
{
    TouchGamepad_ReleaseAll();
    gp_W = 0;
    gp_H = 0;
    gp_stamp++;
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
    int i, n;
    const GPButton *set = gp_current(&n);

    for (i = 0; i < n; i++)
    {
        if (x >= set[i].x && x < set[i].x + set[i].w &&
            y >= set[i].y && y < set[i].y + set[i].h)
        {
            return i;
        }
    }
    return -1;
}

static int gp_button_with_finger(Sint64 finger)
{
    int i, n;
    const GPButton *set = gp_current(&n);

    for (i = 0; i < n; i++)
    {
        if (set[i].finger == finger) return i;
    }
    return -1;
}

/* the main thread's (TouchGamepad_Event), read by the game's: the mouse's
 * buttons of the on-screen buttons, and the pointer under gp_pointer_lock */
static SDL_atomic_t gp_mouse_buttons;
static SDL_SpinLock gp_pointer_lock;
static Sint64 gp_pointer_finger = -1;
static float gp_pointer_x, gp_pointer_y;
static unsigned int gp_pointer_touches;

/* idx: a button of what is shown now (gp_current) */
static void gp_press(int idx, Sint64 finger)
{
    int n;
    GPButton *b = &gp_current(&n)[idx];

    b->finger = finger;
    gp_stamp++;
    if (b->key == TOUCHGAMEPAD_MOUSE_LEFT) SDL_AtomicAdd(&gp_mouse_buttons, 1);
    else if (b->key < 0) {}     /* the port's own: when it is let go */
    else if (gp_keyboard && kb_modifier(b->key)) kb_arm(idx, !kb_armed[idx]);
    else if (gp_keyboard) EmulateKeyMod(SDL_KEYDOWN, b->key, kb_mods());
    else EmulateKey(SDL_KEYDOWN, b->key);
}

static void gp_release(int idx)
{
    int n;
    GPButton *b = &gp_current(&n)[idx];

    b->finger = -1;
    gp_stamp++;
    if (b->key == TOUCHGAMEPAD_MOUSE_LEFT) SDL_AtomicAdd(&gp_mouse_buttons, -1);
    else if (b->key == TOUCHGAMEPAD_KEYBOARD) gp_keyboard_wanted = 1;
    else if (b->key == KB_HIDE) gp_keyboard_wanted = 0;
    else if (b->key == KB_LANG)
    {
        kb_ru = !kb_ru;
        kb_labels();
    }
    else if (b->key < 0) {}
    else if (!gp_keyboard) EmulateKey(SDL_KEYUP, b->key);
    else if (!kb_modifier(b->key))
    {
        /* the key, then the modifiers that waited for it */
        EmulateKeyMod(SDL_KEYUP, b->key, kb_mods());
        kb_disarm_all();
    }
}

/* the pointer: a finger that came down away from the buttons, if none holds it */
static void gp_pointer_down(Sint64 finger, float x, float y)
{
    SDL_AtomicLock(&gp_pointer_lock);
    if (gp_pointer_finger < 0)
    {
        gp_pointer_finger = finger;
        gp_pointer_touches++;
        gp_pointer_x = x;
        gp_pointer_y = y;
    }
    SDL_AtomicUnlock(&gp_pointer_lock);
}

/* the motion (down) or the lift of a finger: the pointer's moves it */
static void gp_pointer_move(Sint64 finger, float x, float y, int down)
{
    SDL_AtomicLock(&gp_pointer_lock);
    if (finger == gp_pointer_finger)
    {
        gp_pointer_x = x;
        gp_pointer_y = y;
        if (!down) gp_pointer_finger = -1;
    }
    SDL_AtomicUnlock(&gp_pointer_lock);
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

/* open or close the keyboard as a button asked: what is held is let go, the
 * other buttons are placed */
static void gp_keyboard_follow(void)
{
    const int want = gp_keyboard_wanted;

    gp_keyboard_wanted = -1;
    if (want < 0 || want == gp_keyboard) return;
    TouchGamepad_ReleaseAll();
    gp_keyboard = want;
    if (gp_W > 0 && gp_H > 0) gp_place_all(gp_W, gp_H);
    gp_stamp++;
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
        case SDL_MOUSEMOTION:
            /* SDL's mouse of a touch: not the game's while the keyboard is open */
            return event->motion.which == SDL_TOUCH_MOUSEID && gp_keyboard;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            const int bit = (event->button.button >= 1 && event->button.button <= 8) ? 1 << (event->button.button - 1) : 0;

            if (event->button.which != SDL_TOUCH_MOUSEID || bit == 0) return 0;
            if (event->type == SDL_MOUSEBUTTONDOWN)
            {
                if (!gp_keyboard) return 0;
                gp_mouse_eaten |= bit;
                return 1;
            }
            if (!(gp_mouse_eaten & bit)) return 0;
            gp_mouse_eaten &= ~bit;
            return 1;
        }
        default:
            return 0;
    }

    x = (int)(event->tfinger.x * gp_W);
    y = (int)(event->tfinger.y * gp_H);
    finger = event->tfinger.fingerId;

    if (gp_keyboard)
    {
        /* a finger keeps its key until it is lifted; one that comes down
         * beside the keys (not between them) closes the keyboard when lifted */
        if (event->type == SDL_FINGERDOWN)
        {
            idx = gp_button_at(x, y);
            if (idx >= 0) { if (kb_buttons[idx].finger < 0) gp_press(idx, finger); }
            else if (x < kb_x0 || x >= kb_x1 || y < kb_y0 || y >= kb_y1) kb_close_finger = finger;
        }
        else if (event->type == SDL_FINGERUP)
        {
            cur = gp_button_with_finger(finger);
            if (cur >= 0) gp_release(cur);
            if (finger == kb_close_finger)
            {
                kb_close_finger = -1;
                gp_keyboard_wanted = 0;
            }
        }
        gp_keyboard_follow();
        return 1;
    }

    pad = gp_pad_with_finger(finger);

    /* the pointer's finger moves the pointer, wherever it goes (and presses the
     * buttons it slides onto, as any finger) */
    if (event->type != SDL_FINGERDOWN) gp_pointer_move(finger, event->tfinger.x, event->tfinger.y, event->type != SDL_FINGERUP);

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
        if (idx >= 0) { if (gp_buttons[idx].finger < 0) gp_press(idx, finger); }
        else gp_pointer_down(finger, event->tfinger.x, event->tfinger.y);
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

    gp_keyboard_follow();
    return 1;
}

/* Another part of the port takes the touches (the window menu, a dialog): what
 * is held is let go, its fingers will not come back here */
void TouchGamepad_ReleaseAll(void)
{
    int i, n;
    const GPButton *set = gp_current(&n);
    const int wanted = gp_keyboard_wanted;

    for (i = 0; i < gp_npads; i++) gp_pad[i].finger = -1;
    for (i = 0; i < n; i++)
    {
        if (set[i].finger >= 0) gp_release(i);
    }
    if (gp_keyboard) kb_disarm_all();
    kb_close_finger = -1;
    /* letting go is not asking for the keyboard (KBD, HIDE) */
    gp_keyboard_wanted = wanted;
    SDL_AtomicLock(&gp_pointer_lock);
    gp_pointer_finger = -1;
    SDL_AtomicUnlock(&gp_pointer_lock);
}

int TouchGamepad_MouseButtons(void)
{
    return SDL_AtomicGet(&gp_mouse_buttons) > 0 ? 1 : 0;
}

unsigned int TouchGamepad_Pointer(float *x, float *y, int *down)
{
    unsigned int touches;

    SDL_AtomicLock(&gp_pointer_lock);
    *x = gp_pointer_x;
    *y = gp_pointer_y;
    *down = gp_pointer_finger >= 0;
    touches = gp_pointer_touches;
    SDL_AtomicUnlock(&gp_pointer_lock);
    return touches;
}

int TouchGamepad_KeyboardShown(void)
{
    return gp_keyboard;
}

int TouchGamepad_ButtonAt(float x, float y)
{
    if (gp_keyboard) return 1;
    return gp_button_at((int)(x * gp_W), (int)(y * gp_H)) >= 0 || gp_pad_at((int)(x * gp_W), (int)(y * gp_H)) >= 0;
}

int TouchGamepad_HeldKey(int index, SDL_Keycode *key)
{
    int n, held;
    const GPButton *set = gp_current(&n);

    if (index < 0 || index >= n) return 0;
    held = set[index].finger >= 0 || (set == kb_buttons && kb_armed[index]);
    /* the keys below 0 are the port's (the mouse, the keyboard's own) */
    *key = (held && set[index].key > 0) ? set[index].key : SDLK_UNKNOWN;
    return 1;
}

#else /* !__ANDROID__ */

void TouchGamepad_Relayout(void)
{
}

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

int TouchGamepad_KeyboardShown(void)
{
    return 0;
}

int TouchGamepad_ButtonAt(float x, float y)
{
    (void)x;
    (void)y;
    return 0;
}

int TouchGamepad_MouseButtons(void)
{
    return 0;
}

unsigned int TouchGamepad_Pointer(float *x, float *y, int *down)
{
    *x = *y = 0;
    *down = 0;
    return 0;
}

int TouchGamepad_HeldKey(int index, SDL_Keycode *key)
{
    (void)index;
    (void)key;
    return 0;
}

#endif /* __ANDROID__ */
