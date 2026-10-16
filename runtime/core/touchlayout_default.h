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

/* The on-screen gamepad layout a game gets unless it has its own
 * Game_touchlayout.h (see touchgamepad.c).  Made for a tank game: drive and
 * turret on the left, forward/back/fire and the weapon keys on the right,
 * Tab/Enter/Esc on top. */

/* The keys the buttons send (a tank game's default keys):
 *   K_UP/DOWN/LEFT/RIGHT  arrows   drive
 *   K_T_LEFT / K_T_RIGHT  Z / C    turn the turret
 *   K_FIRE                X        fire
 *   K_WT_LEFT / K_WT_RIGHT Ctrl/Alt cycle the weapons
 * The buttons are arranged for two thumbs: drive left/right and the turret on
 * the left, forward/back/fire and the weapons on the right, Tab/Enter/Esc on
 * top. */

enum {
    GP_DRIVE_L,   /*  0 */
    GP_DRIVE_R,   /*  1 */
    GP_TURRET_L,  /*  2 */
    GP_TURRET_R,  /*  3 */
    GP_FWD,       /*  4 */
    GP_BACK,      /*  5 */
    GP_FIRE,      /*  6 */
    GP_WPN_L,     /*  7 */
    GP_WPN_R,     /*  8 */
    GP_TAB,       /*  9 */
    GP_ENTER,     /* 10 */
    GP_ESC,       /* 11 */
    GP_NBUTTONS
};

static GPButton gp_buttons[GP_NBUTTONS] = {
    /* drive */
    {0,0,0,0, SDLK_LEFT,    3, NULL,   GP_GREY, -1},
    {0,0,0,0, SDLK_RIGHT,   4, NULL,   GP_GREY, -1},
    /* turret */
    {0,0,0,0, SDLK_z,       3, NULL,   GP_ORANGE, -1},
    {0,0,0,0, SDLK_c,       4, NULL,   GP_ORANGE, -1},
    /* forward / back / fire */
    {0,0,0,0, SDLK_UP,      1, NULL,   GP_GREY, -1},
    {0,0,0,0, SDLK_DOWN,    2, NULL,   GP_GREY, -1},
    {0,0,0,0, SDLK_x,       0, "FIRE", GP_RED, -1},
    /* weapons */
    {0,0,0,0, SDLK_LCTRL,   0, "<",    GP_GREY, -1},
    {0,0,0,0, SDLK_LALT,    0, ">",    GP_GREY, -1},
    /* top */
    {0,0,0,0, SDLK_TAB,     0, "TAB",  GP_BLUE, -1},
    {0,0,0,0, SDLK_RETURN,  0, "\r",   GP_BLUE, -1},
    {0,0,0,0, SDLK_ESCAPE,  0, "ESC",  GP_BLUE, -1},
};

static void gp_layout(int W, int H)
{
    int S = (W < H ? W : H) / 5;
    int gap, m, padx, pady;

    if (S < 44) S = 44;
    gap = S / 5;
    if (gap < 8) gap = 8;
    m = S / 3;

    /* top row: Tab, Enter, Esc, centred */
    {
        int total = 3 * S + 2 * gap;
        int x0 = (W - total) / 2;
        gp_buttons[GP_TAB].w = gp_buttons[GP_TAB].h = S;
        gp_buttons[GP_TAB].x = x0;
        gp_buttons[GP_TAB].y = m;
        gp_buttons[GP_ENTER].w = gp_buttons[GP_ENTER].h = S;
        gp_buttons[GP_ENTER].x = x0 + S + gap;
        gp_buttons[GP_ENTER].y = m;
        gp_buttons[GP_ESC].w = gp_buttons[GP_ESC].h = S;
        gp_buttons[GP_ESC].x = x0 + 2 * (S + gap);
        gp_buttons[GP_ESC].y = m;
    }

    /* left: turret row above the drive row */
    padx = m;
    pady = H - m - (2 * S + gap);
    gp_buttons[GP_TURRET_L].w = gp_buttons[GP_TURRET_L].h = S;
    gp_buttons[GP_TURRET_L].x = padx;
    gp_buttons[GP_TURRET_L].y = pady;
    gp_buttons[GP_TURRET_R].w = gp_buttons[GP_TURRET_R].h = S;
    gp_buttons[GP_TURRET_R].x = padx + S + gap;
    gp_buttons[GP_TURRET_R].y = pady;
    gp_buttons[GP_DRIVE_L].w = gp_buttons[GP_DRIVE_L].h = S;
    gp_buttons[GP_DRIVE_L].x = padx;
    gp_buttons[GP_DRIVE_L].y = pady + S + gap;
    gp_buttons[GP_DRIVE_R].w = gp_buttons[GP_DRIVE_R].h = S;
    gp_buttons[GP_DRIVE_R].x = padx + S + gap;
    gp_buttons[GP_DRIVE_R].y = pady + S + gap;

    /* right: weapons above, then forward/back with fire tall to their right */
    padx = W - m - (2 * S + gap);
    pady = H - m - (3 * S + 2 * gap);
    gp_buttons[GP_WPN_L].w = gp_buttons[GP_WPN_L].h = S;
    gp_buttons[GP_WPN_L].x = padx;
    gp_buttons[GP_WPN_L].y = pady;
    gp_buttons[GP_WPN_R].w = gp_buttons[GP_WPN_R].h = S;
    gp_buttons[GP_WPN_R].x = padx + S + gap;
    gp_buttons[GP_WPN_R].y = pady;
    gp_buttons[GP_FWD].w = gp_buttons[GP_FWD].h = S;
    gp_buttons[GP_FWD].x = padx;
    gp_buttons[GP_FWD].y = pady + S + gap;
    gp_buttons[GP_BACK].w = gp_buttons[GP_BACK].h = S;
    gp_buttons[GP_BACK].x = padx;
    gp_buttons[GP_BACK].y = pady + 2 * (S + gap);
    gp_buttons[GP_FIRE].w = S;
    gp_buttons[GP_FIRE].h = 2 * S + gap;
    gp_buttons[GP_FIRE].x = padx + S + gap;
    gp_buttons[GP_FIRE].y = pady + S + gap;
}

