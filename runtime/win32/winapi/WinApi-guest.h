/*
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

/* Calling back into the recompiled guest from a Win32 callback. */
#if !defined(_WINAPI_GUEST_H_INCLUDED_)
#define _WINAPI_GUEST_H_INCLUDED_

#include "platform.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL Winapi_CallGuest(uint32_t func, unsigned int nargs, const uint32_t *args);

/* A host pointer into the guest's memory mapping <-> the guest value of the
 * same byte (a buffer a guest callback writes to).  A callback the guest passes is a host code
 * pointer and goes to Winapi_CallGuest as it is. */
uint32_t Winapi_HostToGuestPtr(void *p);
void *Winapi_GuestToHostPtr(uint32_t p);

/* Debug helper: read a 32-bit guest global. */
uint32_t Winapi_ReadGuest32(uint32_t addr);

/* Debug helper: current guest stack pointer. */
uint32_t Winapi_GuestEsp(void);

/* Queue an activation change for the recompiled program's message loop.
 * A Windows program learns that its window became (active != 0) or stopped
 * being active through WM_ACTIVATEAPP; the port turns this into an
 * SDL_WINDOWEVENT_FOCUS_GAINED/LOST, which the message functions translate.
 * It goes through the priority slot because main.c's event loop also drains
 * the SDL queue. */
void Winapi_PostActivateApp(int active);

#ifdef __cplusplus
}
#endif

#endif
