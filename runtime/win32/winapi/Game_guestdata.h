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

/* The guest's data at the addresses of the original (Game_guestdata.c);
 * guest_data_in_memory=yes in game.cfg */

#if !defined(_GAME_GUESTDATA_H_INCLUDED_)
#define _GAME_GUESTDATA_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

/* how much of the guest's memory from pointer_offset up the data needs */
unsigned int Game_GuestLowMemory(void);
/* maps the guest's low memory and copies the data of the game into it */
void Game_GuestDataStart(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_GUESTDATA_H_INCLUDED_ */
