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

#ifndef _GAME_DOSAIL2_H_INCLUDED_
#define _GAME_DOSAIL2_H_INCLUDED_

#include <stdint.h>

/* The drivers of the Miles AIL/32 2.x a DOS4GW game was linked with, replaced
 * natively (Game_dosail2.c over core/Game_ail.c).  The game's
 * external_procedures.sci routes AIL_register_driver and the dispatcher of the
 * driver functions to SR_AIL2_register_driver and SR_AIL2_call_driver
 * (llasm/ail2.llinc, in the DOS runtime's Game-llasm.llasm); they call these. */

#ifdef __cplusplus
extern "C" {
#endif

/* AIL_register_driver: the guest address of the driver the game loaded; the
 * handle, -1 if the driver is of no kind here or turned off */
uint32_t Game_AIL2_RegisterDriver(uint32_t image);
/* the driver function `function`; `arguments` is the guest address of the
 * caller's arguments, the driver's handle first */
uint32_t Game_AIL2_CallDriver(uint32_t function, uint32_t arguments);

#ifdef __cplusplus
}
#endif

#endif
