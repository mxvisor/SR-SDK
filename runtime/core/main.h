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

#if !defined(_MAIN_H_INCLUDED_)
#define _MAIN_H_INCLUDED_

#if (defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__))
void Game_InitThreadConcurency(void);
void Game_CloseThreadConcurency(void);
#endif

void Game_CleanState(int imm);

/* Game_eventloop.c */
extern int Game_QuitRequested;
void Game_Event_Loop(void);
/* queues a key event for the guest (Game_EventKQueue) unless it is a repeat
 * (the keys of the port, Game_osd.h, are taken before); 1 if queued.  Called by the
 * event loop, and on win32 by the winapi message pump, which then owns input. */
int Game_QueueKeyboardEvent(const SDL_Event *event);
/* the window of the port and the picture it shows (display/pc.c, or the game's own):
 * the window, its OpenGL and Game_TextureData, after Init_Display2 has set
 * Render_Width and the rest (0 on success), and one frame, which
 * the event loop asks for */
int Game_Display_Create(void);
void Game_Display_Destroy(void);
void Game_Display_Flip(void);

#endif /* _MAIN_H_INCLUDED_ */
