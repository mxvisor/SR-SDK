/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
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

#if !defined(_GAME_THREAD_H_INCLUDED_)
#define _GAME_THREAD_H_INCLUDED_

NORETURN void Game_StopMain(void);
int Game_MainThread(void *data);
int Game_TimerThread(void *data);
int Game_FlipThread(void *data);
/* Stage 58: the host's main thread (Game_Event_Loop) is the one that pumps SDL's events,
 * and it makes and changes the windows: a window of Windows gets its messages only on the
 * thread that made it, and on X11 and Wayland a window call waits for the server's answer,
 * which a pump on another thread would take.  Game_RunOnMain runs fn(arg) there and waits
 * for it; on the main thread itself, before the loop runs and after it has ended, it calls
 * fn at once.  On Android the guest's message functions pump instead (GAME_WINAPI_GUEST_PUMP):
 * SDL's pump parks the program on a pause and hands the GL context over on the thread that
 * draws.  The main thread reads the events either way. */
#include "Game_defs.h"
#include "Game_hooks.h"
#ifndef GAME_WINAPI_GUEST_PUMP
#if defined(__ANDROID__)
#define GAME_WINAPI_GUEST_PUMP 1
#else
#define GAME_WINAPI_GUEST_PUMP 0
#endif
#endif
void Game_RunOnMain(void (*fn)(void *arg), void *arg);
/* the pointer of the window, from any thread (Game_RunOnMain): SDL_ShowCursor,
 * SDL_SetCursor, SDL_SetRelativeMouseMode, SDL_WarpMouseInWindow */
int Game_ShowCursor(int toggle);
void Game_SetCursor(struct SDL_Cursor *cursor);
int Game_SetRelativeMouseMode(int enabled);
void Game_WarpMouse(struct SDL_Window *window, int x, int y);
/* SDL_ShowWindow and SDL_HideWindow from any thread (Game_RunOnMain) */
void Game_ShowWindow(struct SDL_Window *window);
void Game_HideWindow(struct SDL_Window *window);
/* The window of the picture, width x height, the picture's size at its aspect
 * (320x240 for 320x200 at 4:3): resizable, never smaller, the picture kept at
 * its aspect inside (Game_UiPictureRect); on the whole screen with
 * Display_Fullscreen. flags are SDL_CreateWindow's (SDL_WINDOW_HIDDEN, ...). */
struct SDL_Window *Game_WindowCreate(int width, int height, unsigned int flags);
/* a new mode of the game: the window gets its size, unless the player resized
 * it or it is on the whole screen; the minimum is the new size */
void Game_WindowFit(struct SDL_Window *window, int width, int height);
/* the window on the whole screen or not, for a game that switches it itself;
 * the setting Display_Fullscreen is not written (the panel's item writes it) */
void Game_WindowSetFullscreen(int on);
/* Game_OsdInit: the item Fullscreen of the port's panel (Game_osd.h) */
void Game_WindowOsdItems(void);

#endif /* _GAME_THREAD_H_INCLUDED_ */
