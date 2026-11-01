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

/* The on-screen gamepad layout a game gets unless it has its own
 * game/Game_touchlayout.h (see touchgamepad.c): a D-pad on the left, four
 * action buttons and Esc on the right, Y and N (many games ask before they
 * quit) in the top left corner. */

/* The keys the buttons send: the arrows steer
 * and accelerate/brake, Space is the handbrake, Enter/Esc are the menus. */

#define GP_NBUTTONS 11

static GPButton gp_buttons[GP_NBUTTONS] = {
    {0,0,0,0, SDLK_UP,      1, NULL,   GP_LIGHT, -1},
    {0,0,0,0, SDLK_DOWN,    2, NULL,   GP_LIGHT, -1},
    {0,0,0,0, SDLK_LEFT,    3, NULL,   GP_LIGHT, -1},
    {0,0,0,0, SDLK_RIGHT,   4, NULL,   GP_LIGHT, -1},
    {0,0,0,0, SDLK_SPACE,   0, "SPC",  GP_GREY, -1},   /* handbrake */
    {0,0,0,0, SDLK_RETURN,  0, "\r",   GP_GREY, -1},   /* enter */
    {0,0,0,0, SDLK_ESCAPE,  0, "ESC",  GP_RED, -1},   /* menu */
    {0,0,0,0, SDLK_LCTRL,   0, "CTRL", GP_GREY, -1},
    {0,0,0,0, SDLK_ESCAPE,  0, "ESC",  GP_RED, -1},
    {0,0,0,0, SDLK_y,       0, "Y",    GP_GREY, -1},
    {0,0,0,0, SDLK_n,       0, "N",    GP_GREY, -1},
};

static void gp_layout(int W, int H)
{
    int S = (W < H ? W : H) / 4;
    int gap, m, padx, pady;

    if (S < 48) S = 48;
    gap = S / 5;
    if (gap < 8) gap = 8;
    m = S / 3;

    /* D-pad: a cross of four cells, centre cell empty */
    padx = m;
    pady = H - m - (3 * S + 2 * gap);
    gp_buttons[0].w = gp_buttons[0].h = S;  /* up */
    gp_buttons[0].x = padx + S + gap;
    gp_buttons[0].y = pady;
    gp_buttons[1].w = gp_buttons[1].h = S;  /* down */
    gp_buttons[1].x = padx + S + gap;
    gp_buttons[1].y = pady + 2 * (S + gap);
    gp_buttons[2].w = gp_buttons[2].h = S;  /* left */
    gp_buttons[2].x = padx;
    gp_buttons[2].y = pady + S + gap;
    gp_buttons[3].w = gp_buttons[3].h = S;  /* right */
    gp_buttons[3].x = padx + 2 * (S + gap);
    gp_buttons[3].y = pady + S + gap;

    /* action buttons: a 2x2 block at the bottom right */
    padx = W - m - (2 * S + gap);
    pady = H - m - (2 * S + gap);
    gp_buttons[4].w = gp_buttons[4].h = S;
    gp_buttons[4].x = padx;
    gp_buttons[4].y = pady;
    gp_buttons[5].w = gp_buttons[5].h = S;
    gp_buttons[5].x = padx + S + gap;
    gp_buttons[5].y = pady;
    gp_buttons[6].w = gp_buttons[6].h = S;
    gp_buttons[6].x = padx;
    gp_buttons[6].y = pady + S + gap;
    gp_buttons[7].w = gp_buttons[7].h = S;
    gp_buttons[7].x = padx + S + gap;
    gp_buttons[7].y = pady + S + gap;

    /* Esc at the top right */
    gp_buttons[8].w = S;
    gp_buttons[8].h = (S * 2) / 3;
    gp_buttons[8].x = W - m - S;
    gp_buttons[8].y = m;

    /* Y and N above the left of the D-pad */
    gp_buttons[9].w = gp_buttons[10].w = S;
    gp_buttons[9].h = gp_buttons[10].h = (S * 2) / 5;
    gp_buttons[9].x = gp_buttons[10].x = m;
    gp_buttons[9].y = m;
    gp_buttons[10].y = m + (S * 2) / 5 + gap;
}

