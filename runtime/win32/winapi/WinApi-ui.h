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

#if !defined(_WINAPI_UI_H_INCLUDED_)
#define _WINAPI_UI_H_INCLUDED_

#include <stdint.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The port's own windows over the picture: the window menu (WinApi-menubar.c)
 * and the dialogs (WinApi-dialog.c).  The message pump (find_event,
 * WinApi-user32.c) gives them every SDL event first; while one is open the
 * pump waits instead of letting the game run. */
int Winapi_MenuBarEvent(const SDL_Event *event);
int Winapi_MenuBarOpen(void);
int Winapi_DialogEvent(const SDL_Event *event);
int Winapi_DialogOpen(void);
/* a message to a control of the open dialog (SendMessageA): 1 and its result, or 0 */
int Winapi_DialogMessage(uint32_t hWnd, uint32_t Msg, uint32_t wParam, uint32_t lParam, uint32_t *result);

/* MessageBoxA as a dialog of the port: the button pressed (IDOK, IDYES, ...) */
uint32_t Winapi_MessageBoxDialog(const char *text, const char *caption, uint32_t type);

/* posts WM_COMMAND id to the main window (WinApi-user32.c) */
void Winapi_PostCommand(uint32_t id);
/* waits for and hands out SDL events until done() (WinApi-user32.c) */
void Winapi_ModalLoop(int (*done)(void));

#ifdef __cplusplus
}
#endif

#endif /* _WINAPI_UI_H_INCLUDED_ */
