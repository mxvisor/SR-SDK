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

/* The port's own screen interface over the game's picture: the touch gamepad
 * and one client (the window menu, a dialog), and the on-screen display.  The presenters (display/pc.c,
 * gl/Game_gl.c, DirectDraw, a game's own) call Game_UiDraw after the picture;
 * the client draws with an SDL_Renderer the size of the window. */

#if !defined(_GAME_UI_H_INCLUDED_)
#define _GAME_UI_H_INCLUDED_

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*draw)(SDL_Renderer *renderer, int width, int height);
    int (*visible)(void);
} Game_UiClient;

void Game_UiSetClient(const Game_UiClient *client);
/* the touch gamepad, the on-screen display, then the client */
void Game_UiDraw(SDL_Renderer *renderer);
/* changes whenever what Game_UiDraw draws changes */
int Game_UiStamp(void);
void Game_UiChanged(void);
/* the client has something on the screen */
int Game_UiVisible(void);
/* the client or the on-screen display (Game_osd.h) has something on the screen */
int Game_UiActive(void);
/* the renderer of the glyphs is gone (its textures with it) */
void Game_UiForgetRenderer(void);
/* the size Game_UiDraw had last: events in window pixels are scaled to it */
void Game_UiSize(int *width, int *height);
/* a point of an event in the pixels of a window to those of Game_UiSize */
void Game_UiWindowPoint(Uint32 window_id, int *x, int *y);
/* where a width x height frame is shown in a window_w x window_h window: the largest
 * rectangle of the frame's aspect, centred (4:3 for every mode on the touch-gamepad
 * builds); Game_GlPresentTexture shows it there */
void Game_UiPictureRect(int window_w, int window_h, int width, int height, SDL_Rect *rect);

/* The presenter that shows the last picture again: a client that holds the game
 * (a modal menu) redraws the screen through it */
void Game_UiSetRepaint(void (*repaint)(void));
void Game_UiRepaint(void);

/* Text in the bitmap font of VKfont2.h (ASCII; Latin-1 letters lose their
 * accents), h pixels high */
int Game_UiTextWidth(const char *text, int h);
void Game_UiText(SDL_Renderer *renderer, const char *text, int x, int y, int h);
void Game_UiFillRect(SDL_Renderer *renderer, int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_UI_H_INCLUDED_ */
