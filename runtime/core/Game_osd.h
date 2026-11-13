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

/* The port's on-screen display: a panel over the picture, the only hot key of the
 * port (Display_OSD_Key, F12 by default; on Android also the system's Back).
 *
 * The panel shows the game's picture and the output, the window and the
 * gamepad, and below them the port's settings: an item says what it switches
 * and what it is now ("Filter: crt").  While the panel is open it takes every
 * key, click, touch and gamepad button, the game goes on without them: the
 * arrows (the wheel, the pointer) choose an item, Enter, Space, the left and
 * right arrows, a click or a touch switch it to its next value, or open the
 * list of its values under it (Up and Down, Enter to choose, Esc to close the
 * list), Esc or the panel's key close it.  The rows do not all fit: the panel
 * scrolls (with the chosen row, or a finger drawn over it).  On a touch screen
 * the rows are about 7 mm high, for a finger.  An item with a key of the settings file is written
 * there at once (Game_ConfigSave), so the next start has it too.
 *
 * The items are registered once the settings are read: the runtime's own (the
 * filter of Game_gl.h, the whole screen, FPS: the frames per second in the upper
 * right corner, Display_FPS), then a game's in GAME_OSD_ITEMS() of
 * its Game_hooks.h. */

#if !defined(_GAME_OSD_H_INCLUDED_)
#define _GAME_OSD_H_INCLUDED_

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* what the item switches: "Filter" */
    const char *label;
    /* non-zero when it can be switched now, else it is grey (NULL: always) */
    int (*active)(void);
    /* to the next value (NULL: the item is only shown) */
    void (*press)(void);
    /* what it is now: "crt"; also the value saved under config (NULL: nothing to show) */
    const char *(*state)(void);
    /* the key of the settings file it is saved as (NULL: not saved) */
    const char *config;
    /* of an item of many values in an order (sizes): to the previous (-1) or the
     * next (+1) value, stopping at the ends; Left and Right, while press goes
     * round (NULL: Left and Right press) */
    void (*step)(int dir);
    /* of an item of a list of values (the filters, the sizes): how many there
     * are, the name of one (as state shows it), and to one of them; Enter, a
     * click, a touch or the pad's A open the list under the item, and a value of
     * it is chosen there (NULL, all three: press, as before) */
    int (*count)(void);
    const char *(*value)(int index);
    void (*choose)(int index);
} Game_OsdItem;

/* registers an item; the entry must stay */
void Game_OsdAddItem(const Game_OsdItem *entry);

/* opens or closes the panel (the display key, Back on Android) */
void Game_OsdToggle(void);
/* the panel is open: it takes the input */
int Game_OsdOpen(void);
/* after the settings: the runtime's items, then GAME_OSD_ITEMS() */
void Game_OsdInit(void);
/* Display_OSD_Key and Display_FPS of the settings file: non-zero if it was one */
int Game_OsdConfigKey(const char *key, const char *value);
/* an SDL event before the game sees it: non-zero if the panel took it */
int Game_OsdEvent(const SDL_Event *event);
/* for the input that reads the events by itself (DirectInput's event watches):
 * non-zero for the panel's key and, while it is open, for every press and move */
int Game_OsdHides(const SDL_Event *event);
/* for the input that reads the state of the keys: non-zero while the panel is
 * open and for a key pressed for the panel until it is released */
int Game_OsdKeyHidden(SDL_Scancode scancode);

/* the presenters, once per frame: the game's picture and how it is shown
 * ("SDL", "OpenGL") */
void Game_OsdFrame(int width, int height, const char *output);
/* a message for a moment in the corner of the window */
void Game_OsdMessage(const char *text);

/* for Game_ui.c: something to draw, its stamp, the drawing */
int Game_OsdVisible(void);
int Game_OsdStamp(void);
void Game_OsdDraw(SDL_Renderer *renderer, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_OSD_H_INCLUDED_ */
