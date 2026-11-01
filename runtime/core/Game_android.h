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

#ifndef _GAME_ANDROID_H_INCLUDED_
#define _GAME_ANDROID_H_INCLUDED_

#if defined(__ANDROID__)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The first thing main() does on Android: stdout and stderr go to logcat
 * (tag GAME_NAME), and the directories android/GameActivity.java unpacked
 * the game into become GAME_ENV("DATA_DIR") and GAME_ENV("WRITE_ROOT"). */
void Game_AndroidSetup(void);

/* The block android/gameloader.c reserved for the guest's memory. */
int Game_AndroidGuestRegion(uint64_t *base, uint64_t *size);

/* Whether the guest's 32-bit code pointers reach the host image: llasm keeps
 * them as (address - pointer_offset) in 32 bits, unsigned.  Notes the image,
 * the offset and the distance in startup.txt and the memory map in maps.txt,
 * both in Android/data/<package>/files/.  Returns 0 if the image is in reach. */
int Game_AndroidCheckImageReach(uint64_t offset, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif

#endif /* _GAME_ANDROID_H_INCLUDED_ */
