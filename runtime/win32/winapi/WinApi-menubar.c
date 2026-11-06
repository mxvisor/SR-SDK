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

/* The menu bar of the window, drawn by the port over the picture (Game_ui.h):
 * the window has no frame, so the menu the game set (SetMenu, WinApi-window.c)
 * shows only when it is opened: by a click or a touch in the strip at the top
 * of the window, a touch not on a button of the touch gamepad (not by Alt or
 * F10, as in Windows: a game may have them among its keys; the port's only
 * key is its panel's).  While it is open it
 * takes every key, click and touch, and the message pump of the game waits
 * (find_event, WinApi-user32.c): the game stands still, as in the menu loop of
 * Windows.  A chosen item posts WM_COMMAND with its command to the window, as
 * Windows does. */

#include <stdint.h>
#include <string.h>
#include <SDL.h>
#include "Game_ui.h"
#include "touchgamepad.h"
#include "WinApi-user32.h"
#include "WinApi-ui.h"

#define MF_GRAYED_c      0x0001
#define MF_DISABLED_c    0x0002
#define MF_CHECKED_c     0x0008
#define MF_POPUP_c       0x0010
#define MF_SEPARATOR_c   0x0800
#define MF_HELP_c        0x4000

/* the guest's handle of the main window (WinApi-user32.c) */
#define MAIN_WINDOW 1u

#define MAX_DEPTH 6
#define MAX_ITEMS 64

typedef struct {
    int x, y, w, h;
} box;

static struct {
    int open;               /* the bar is on the screen */
    int depth;              /* the popups open under the bar */
    int bar_sel;            /* the item of the bar */
    uint32_t popup[MAX_DEPTH];
    int sel[MAX_DEPTH];     /* the item of each popup, -1 none */
    int finger;             /* a touch is on the menu */
} mb;

/* the layout for a screen of W x H pixels */
static struct {
    int W, H, th, bar_h, row_h, sep_h, pad;
    int bar_count;
    box bar[MAX_ITEMS];
    box popup[MAX_DEPTH];
    int item_y[MAX_DEPTH][MAX_ITEMS];
    int item_h[MAX_DEPTH][MAX_ITEMS];
} lo;

static uint32_t bar_menu(void)
{
    return GetMenu_c(MAIN_WINDOW);
}

static int item_count(uint32_t menu)
{
    uint32_t count = GetMenuItemCount_c(menu);

    if (count == 0xffffffffu) return 0;
    return (count > MAX_ITEMS) ? MAX_ITEMS : (int)count;
}

static int item(uint32_t menu, int pos, uint32_t *flags, uint32_t *id, const char **text)
{
    uint32_t f = 0, i = 0;
    const char *t = NULL;

    if (!Winapi_MenuItem(menu, (unsigned int)pos, &f, &i, &t)) return 0;
    if (flags != NULL) *flags = f;
    if (id != NULL) *id = i;
    if (text != NULL) *text = t;
    return 1;
}

/* the text of an item without the '&' of its mnemonic, and its accelerator (after a tab) */
static void split_text(const char *text, char *label, char *accel, size_t size)
{
    size_t n = 0;

    accel[0] = 0;
    if (text == NULL) text = "";
    for (; *text != 0 && n + 1 < size; text++)
    {
        if (*text == '\t')
        {
            size_t m = 0;
            for (text++; *text != 0 && m + 1 < size; text++) if (*text != '&') accel[m++] = *text;
            accel[m] = 0;
            break;
        }
        if (*text == '&')
        {
            if (text[1] != '&') continue;
            text++;
        }
        label[n++] = *text;
    }
    label[n] = 0;
}

static int selectable(uint32_t flags)
{
    return !(flags & MF_SEPARATOR_c);
}

static int enabled(uint32_t flags)
{
    return !(flags & (MF_GRAYED_c | MF_DISABLED_c | MF_SEPARATOR_c));
}

/* the popup of level k: under its item of the bar, or beside its item of the popup above */
static void layout_popup(int k)
{
    uint32_t menu = mb.popup[k];
    int count = item_count(menu), i, label_w = 0, accel_w = 0, y, w, h, x0, y0;
    char label[256], accel[64];

    for (i = 0; i < count; i++)
    {
        uint32_t flags;
        const char *text;
        int lw, aw;

        if (!item(menu, i, &flags, NULL, &text) || (flags & MF_SEPARATOR_c)) continue;
        split_text(text, label, accel, sizeof(accel));
        lw = Game_UiTextWidth(label, lo.th);
        aw = Game_UiTextWidth(accel, lo.th);
        if (lw > label_w) label_w = lw;
        if (aw > accel_w) accel_w = aw;
    }
    /* the check column, the label, the accelerator, the arrow of a popup */
    w = lo.row_h + label_w + (accel_w ? 2 * lo.th + accel_w : 0) + lo.row_h;
    y = lo.pad / 2;
    for (i = 0; i < count; i++)
    {
        uint32_t flags = 0;

        item(menu, i, &flags, NULL, NULL);
        lo.item_y[k][i] = y;
        lo.item_h[k][i] = (flags & MF_SEPARATOR_c) ? lo.sep_h : lo.row_h;
        y += lo.item_h[k][i];
    }
    h = y + lo.pad / 2;

    if (k == 0)
    {
        x0 = lo.bar[mb.bar_sel].x;
        y0 = lo.bar_h;
    }
    else
    {
        x0 = lo.popup[k - 1].x + lo.popup[k - 1].w - lo.pad / 2;
        y0 = lo.popup[k - 1].y + lo.item_y[k - 1][mb.sel[k - 1]] - lo.pad / 2;
        /* no room on the right: on the left of the popup above */
        if (x0 + w > lo.W) x0 = lo.popup[k - 1].x - w + lo.pad / 2;
    }
    if (x0 + w > lo.W) x0 = lo.W - w;
    if (x0 < 0) x0 = 0;
    if (y0 + h > lo.H) y0 = lo.H - h;
    if (y0 < 0) y0 = 0;
    lo.popup[k].x = x0;
    lo.popup[k].y = y0;
    lo.popup[k].w = w;
    lo.popup[k].h = h;
}

static void layout(int W, int H)
{
    uint32_t menu = bar_menu();
    int count = item_count(menu), i, x, right = -1, k;
    char label[256], accel[64];

    lo.W = W;
    lo.H = H;
    lo.th = H / 28;
    if (lo.th < 12) lo.th = 12;
    if (lo.th > 40) lo.th = 40;
    lo.bar_h = lo.th * 16 / 10;
    lo.row_h = lo.th * 16 / 10;
    lo.sep_h = lo.th / 2;
    lo.pad = lo.th;

    x = lo.pad / 2;
    for (i = 0; i < count; i++)
    {
        uint32_t flags = 0;
        const char *text = NULL;

        item(menu, i, &flags, NULL, &text);
        split_text(text, label, accel, sizeof(accel));
        lo.bar[i].x = x;
        lo.bar[i].y = 0;
        lo.bar[i].w = Game_UiTextWidth(label, lo.th) + 2 * lo.pad;
        lo.bar[i].h = lo.bar_h;
        x += lo.bar[i].w;
        if ((flags & MF_HELP_c) && right < 0) right = i;
    }
    lo.bar_count = count;
    /* MF_HELP: that item and the ones after it at the right end of the bar */
    if (right >= 0 && x < W)
    {
        int shift = W - lo.pad / 2 - x;
        for (i = right; i < count; i++) lo.bar[i].x += shift;
    }

    for (k = 0; k < mb.depth; k++) layout_popup(k);
}

static void changed(void)
{
    Game_UiChanged();
    Game_UiRepaint();
}

static void close_menu(void)
{
    mb.open = 0;
    mb.depth = 0;
    mb.finger = 0;
    changed();
}

/* the popup of the bar's item i (none if the item is a command or the menu is gone) */
static void open_bar_item(int i, int with_popup)
{
    uint32_t flags = 0, id = 0;

    mb.bar_sel = i;
    mb.depth = 0;
    if (with_popup && item(bar_menu(), i, &flags, &id, NULL) && (flags & MF_POPUP_c) && enabled(flags))
    {
        mb.popup[0] = id;
        mb.sel[0] = -1;
        mb.depth = 1;
    }
}

static void choose(uint32_t id)
{
    close_menu();
    Winapi_PostCommand(id);
}

/* the item of the bar, or of the deepest popup, is activated */
static void activate(int k, int i)
{
    uint32_t flags = 0, id = 0;

    if (k < 0)
    {
        if (!item(bar_menu(), i, &flags, &id, NULL)) return;
        if (flags & MF_POPUP_c) open_bar_item(i, 1);
        else if (enabled(flags)) choose(id);
        return;
    }
    if (!item(mb.popup[k], i, &flags, &id, NULL) || !enabled(flags)) return;
    mb.depth = k + 1;
    mb.sel[k] = i;
    if (flags & MF_POPUP_c)
    {
        if (k + 1 < MAX_DEPTH)
        {
            mb.popup[k + 1] = id;
            mb.sel[k + 1] = -1;
            mb.depth = k + 2;
        }
        return;
    }
    choose(id);
}

static void move_sel(int k, int step)
{
    uint32_t menu = mb.popup[k];
    int count = item_count(menu), i = mb.sel[k], n;

    for (n = 0; n < count; n++)
    {
        uint32_t flags = 0;

        i = (i < 0) ? (step > 0 ? 0 : count - 1) : (i + step + count) % count;
        if (item(menu, i, &flags, NULL, NULL) && selectable(flags))
        {
            mb.sel[k] = i;
            return;
        }
    }
}

static void move_bar(int step)
{
    int count = item_count(bar_menu()), popup = (mb.depth > 0);

    if (count == 0) return;
    open_bar_item((mb.bar_sel + step + count) % count, popup);
    if (popup && mb.depth > 0) move_sel(0, 1);
}

static void key(SDL_Keycode sym)
{
    int k = mb.depth - 1;
    uint32_t flags = 0;

    switch (sym)
    {
        case SDLK_ESCAPE:
            if (mb.depth > 0) mb.depth--;
            else close_menu();
            break;
        case SDLK_LEFT:
            if (mb.depth > 1) mb.depth--;
            else move_bar(-1);
            break;
        case SDLK_RIGHT:
            if (k >= 0 && mb.sel[k] >= 0 && item(mb.popup[k], mb.sel[k], &flags, NULL, NULL) && (flags & MF_POPUP_c))
            {
                activate(k, mb.sel[k]);
                if (mb.depth > k + 1) move_sel(k + 1, 1);
            }
            else move_bar(1);
            break;
        case SDLK_DOWN:
        case SDLK_UP:
            if (k < 0)
            {
                open_bar_item(mb.bar_sel, 1);
                if (mb.depth > 0) move_sel(0, 1);
            }
            else move_sel(k, sym == SDLK_DOWN ? 1 : -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (k < 0)
            {
                activate(-1, mb.bar_sel);
                if (mb.open && mb.depth > 0) move_sel(0, 1);
            }
            else if (mb.sel[k] >= 0)
            {
                activate(k, mb.sel[k]);
                if (mb.open && mb.depth > k + 1) move_sel(k + 1, 1);
            }
            break;
    }
}

/* where a point of the screen falls: a popup level and its item, the bar (k = -1),
 * or nothing (0) */
static int hit(int x, int y, int *k, int *i)
{
    int level, n;

    for (level = mb.depth - 1; level >= 0; level--)
    {
        const box *p = &lo.popup[level];
        int count = item_count(mb.popup[level]);

        if (x < p->x || x >= p->x + p->w || y < p->y || y >= p->y + p->h) continue;
        *k = level;
        *i = -1;
        for (n = 0; n < count; n++)
        {
            int top = p->y + lo.item_y[level][n];
            if (y >= top && y < top + lo.item_h[level][n]) *i = n;
        }
        return 1;
    }
    if (y >= 0 && y < lo.bar_h)
    {
        *k = -1;
        *i = -1;
        for (n = 0; n < lo.bar_count; n++)
        {
            if (x >= lo.bar[n].x && x < lo.bar[n].x + lo.bar[n].w) *i = n;
        }
        return 1;
    }
    return 0;
}

/* a pointer (mouse or finger) moves over the menu: what it is over is highlighted,
 * a popup under it opens */
static void pointer_move(int x, int y)
{
    int k, i;
    uint32_t flags = 0;

    if (!hit(x, y, &k, &i) || i < 0) return;
    if (k < 0)
    {
        if (mb.depth > 0 && i != mb.bar_sel) open_bar_item(i, 1);
        else if (mb.depth == 0) mb.bar_sel = i;
        return;
    }
    mb.depth = k + 1;
    if (!item(mb.popup[k], i, &flags, NULL, NULL) || !selectable(flags))
    {
        mb.sel[k] = -1;
        return;
    }
    mb.sel[k] = i;
    if ((flags & MF_POPUP_c) && enabled(flags)) activate(k, i);
}

static void pointer_down(int x, int y)
{
    int k, i;

    if (!hit(x, y, &k, &i))
    {
        close_menu();
        return;
    }
    if (k < 0 && i >= 0)
    {
        if (mb.depth > 0 && i == mb.bar_sel) mb.depth = 0;
        else activate(-1, i);
    }
    else if (k >= 0) pointer_move(x, y);
}

static void pointer_up(int x, int y)
{
    int k, i;
    uint32_t flags = 0;

    if (!hit(x, y, &k, &i) || k < 0 || i < 0) return;
    if (item(mb.popup[k], i, &flags, NULL, NULL) && !(flags & MF_POPUP_c)) activate(k, i);
}

static void draw(SDL_Renderer *renderer, int W, int H);
static int visible(void);
static const Game_UiClient menubar_client = { draw, visible };

static void open_menu(void)
{
    int W, H;

    Game_UiSetClient(&menubar_client);
    TouchGamepad_ReleaseAll();
    Game_UiSize(&W, &H);
    mb.open = 1;
    mb.depth = 0;
    mb.bar_sel = 0;
    mb.finger = 0;
    layout(W, H);
}

int Winapi_MenuBarOpen(void)
{
    return mb.open;
}

/* An event of SDL before the game sees it: 1 if the menu took it */
int Winapi_MenuBarEvent(const SDL_Event *event)
{
    int x, y;

    if (!mb.open)
    {
        if (bar_menu() == 0) return 0;
        switch (event->type)
        {
            case SDL_MOUSEBUTTONDOWN:
            {
                int W, H;

                if (event->button.which == SDL_TOUCH_MOUSEID) return 0;
                Game_UiSize(&W, &H);
                layout(W, H);
                x = event->button.x;
                y = event->button.y;
                Game_UiWindowPoint(event->button.windowID, &x, &y);
                if (y < 0 || y >= lo.bar_h) return 0;
                open_menu();
                pointer_down(x, y);
                changed();
                return 1;
            }
            case SDL_FINGERDOWN:
            {
                int W, H;

                if (TouchGamepad_ButtonAt(event->tfinger.x, event->tfinger.y)) return 0;
                Game_UiSize(&W, &H);
                layout(W, H);
                x = (int)(event->tfinger.x * W);
                y = (int)(event->tfinger.y * H);
                if (y < 0 || y >= lo.bar_h) return 0;
                open_menu();
                mb.finger = 1;
                pointer_down(x, y);
                changed();
                return 1;
            }
        }
        return 0;
    }

    if (bar_menu() == 0)
    {
        close_menu();
        return 0;
    }
    layout(lo.W, lo.H);

    switch (event->type)
    {
        case SDL_KEYDOWN:
            key(event->key.keysym.sym);
            break;
        case SDL_KEYUP:
            break;
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING:
        case SDL_MOUSEWHEEL:
            break;
        case SDL_MOUSEMOTION:
            if (event->motion.which == SDL_TOUCH_MOUSEID) return 1;
            x = event->motion.x;
            y = event->motion.y;
            Game_UiWindowPoint(event->motion.windowID, &x, &y);
            pointer_move(x, y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event->button.which == SDL_TOUCH_MOUSEID) return 1;
            x = event->button.x;
            y = event->button.y;
            Game_UiWindowPoint(event->button.windowID, &x, &y);
            if (event->type == SDL_MOUSEBUTTONDOWN) pointer_down(x, y);
            else pointer_up(x, y);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            x = (int)(event->tfinger.x * lo.W);
            y = (int)(event->tfinger.y * lo.H);
            if (event->type == SDL_FINGERDOWN)
            {
                mb.finger = 1;
                pointer_down(x, y);
            }
            else if (event->type == SDL_FINGERMOTION)
            {
                if (mb.finger) pointer_move(x, y);
            }
            else if (mb.finger)
            {
                mb.finger = 0;
                pointer_up(x, y);
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            break;
        default:
            return 0;
    }
    if (mb.open) changed();
    return 1;
}

/* ---- drawing ---- */

static void color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

static void draw_check(SDL_Renderer *renderer, int x, int y, int s)
{
    int t, w = (s / 8 > 1) ? s / 8 : 1;

    for (t = 0; t < w; t++)
    {
        SDL_RenderDrawLine(renderer, x + s / 5, y + s / 2 + t, x + s * 2 / 5, y + s * 3 / 4 + t);
        SDL_RenderDrawLine(renderer, x + s * 2 / 5, y + s * 3 / 4 + t, x + s * 4 / 5, y + s / 4 + t);
    }
}

static void draw_arrow(SDL_Renderer *renderer, int x, int y, int s)
{
    int row;

    for (row = 0; row <= s / 2; row++)
    {
        int len = (row <= s / 4) ? row : s / 2 - row;
        SDL_RenderDrawLine(renderer, x + s * 3 / 8, y + s / 4 + row, x + s * 3 / 8 + len, y + s / 4 + row);
    }
}

static void draw_popup(SDL_Renderer *renderer, int k)
{
    const box *p = &lo.popup[k];
    uint32_t menu = mb.popup[k];
    int count = item_count(menu), i;
    char label[256], accel[64];
    SDL_Rect border;

    color(renderer, 36, 36, 46, 240);
    Game_UiFillRect(renderer, p->x, p->y, p->w, p->h);
    color(renderer, 120, 120, 140, 255);
    border.x = p->x; border.y = p->y; border.w = p->w; border.h = p->h;
    SDL_RenderDrawRect(renderer, &border);

    for (i = 0; i < count; i++)
    {
        uint32_t flags = 0;
        const char *text = NULL;
        int y = p->y + lo.item_y[k][i], ty;

        item(menu, i, &flags, NULL, &text);
        if (flags & MF_SEPARATOR_c)
        {
            color(renderer, 100, 100, 115, 255);
            SDL_RenderDrawLine(renderer, p->x + lo.pad / 2, y + lo.sep_h / 2, p->x + p->w - 1 - lo.pad / 2, y + lo.sep_h / 2);
            continue;
        }
        if (i == mb.sel[k])
        {
            color(renderer, 70, 110, 190, 255);
            Game_UiFillRect(renderer, p->x + 2, y, p->w - 4, lo.row_h);
        }
        if (enabled(flags)) color(renderer, 255, 255, 255, 255);
        else color(renderer, 125, 125, 135, 255);
        split_text(text, label, accel, sizeof(accel));
        ty = y + (lo.row_h - lo.th) / 2;
        if (flags & MF_CHECKED_c) draw_check(renderer, p->x + (lo.row_h - lo.th) / 2, ty, lo.th);
        Game_UiText(renderer, label, p->x + lo.row_h, ty, lo.th);
        if (accel[0]) Game_UiText(renderer, accel, p->x + p->w - lo.row_h - Game_UiTextWidth(accel, lo.th), ty, lo.th);
        if (flags & MF_POPUP_c) draw_arrow(renderer, p->x + p->w - lo.row_h + (lo.row_h - lo.th) / 2, ty, lo.th);
    }
}

static void draw(SDL_Renderer *renderer, int W, int H)
{
    uint32_t menu = bar_menu();
    char label[256], accel[64];
    int i, k;

    layout(W, H);
    color(renderer, 28, 28, 36, 235);
    Game_UiFillRect(renderer, 0, 0, W, lo.bar_h);
    for (i = 0; i < lo.bar_count; i++)
    {
        uint32_t flags = 0;
        const char *text = NULL;

        item(menu, i, &flags, NULL, &text);
        if (i == mb.bar_sel)
        {
            color(renderer, 70, 110, 190, 255);
            Game_UiFillRect(renderer, lo.bar[i].x, 0, lo.bar[i].w, lo.bar_h);
        }
        if (enabled(flags)) color(renderer, 255, 255, 255, 255);
        else color(renderer, 125, 125, 135, 255);
        split_text(text, label, accel, sizeof(accel));
        Game_UiText(renderer, label, lo.bar[i].x + lo.pad, (lo.bar_h - lo.th) / 2, lo.th);
    }
    for (k = 0; k < mb.depth; k++) draw_popup(renderer, k);
}

static int visible(void)
{
    return mb.open;
}
