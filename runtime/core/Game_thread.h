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
/* SDL_ShowWindow and SDL_HideWindow for a thread that is not the event loop's */
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
/* Game_OsdInit: the item Fullscreen of the port's panel (Game_osd.h) */
void Game_WindowOsdItems(void);

#endif /* _GAME_THREAD_H_INCLUDED_ */
