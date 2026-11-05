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

/* The port's hot keys and its on-screen display.
 *
 * A key of the port (a toggle of a mod, the next filter) is registered here
 * once the settings are read: the game does it in GAME_OSD_KEYS() of its
 * Game_hooks.h, the runtime for its own.  The key event goes to the key's
 * function and not to the game (the game may still see the key through
 * DirectInput); a key that two take belongs to the first.  The display key
 * (Display_OSD_Key, F12 by default) shows and hides a panel over the picture:
 * the game's picture and the output, the window, FPS, the gamepad and the
 * keys with what they switch now.  A key's press shows what it switched for
 * a moment in the corner (Game_OsdMessage). */

#if !defined(_GAME_OSD_H_INCLUDED_)
#define _GAME_OSD_H_INCLUDED_

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* what the key does, shown in the panel: "Filter" */
    const char *label;
    /* non-zero when the key works now, else the game gets it (NULL: always) */
    int (*active)(void);
    /* on its press (NULL: the key is only listed, its owner takes it) */
    void (*press)(void);
    /* what it switched to: "crt" (NULL: nothing to show) */
    const char *(*state)(void);
} Game_OsdKey;

/* a key of the settings file: <SDL key name> or off (SDL_SCANCODE_UNKNOWN);
 * 0 when the name is unknown, *key is left as it was */
int Game_OsdParseKey(const char *value, SDL_Scancode *key);
/* registers a key (SDL_SCANCODE_UNKNOWN: none); the entry must stay */
void Game_OsdAddKey(SDL_Scancode key, const Game_OsdKey *entry);

/* shows or hides the panel (the display key, the OSD button of the touch gamepad) */
void Game_OsdToggle(void);
/* after the settings: the display key, then GAME_OSD_KEYS() */
void Game_OsdInit(void);
/* Display_OSD_Key of the settings file: non-zero if it was that */
int Game_OsdConfigKey(const char *key, const char *value);
/* a key event (KEYDOWN, KEYUP): non-zero if a key of the port took it */
int Game_OsdKeyEvent(const SDL_KeyboardEvent *key);

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
