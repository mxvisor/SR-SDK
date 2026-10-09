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

#ifdef __cplusplus
extern "C" {
#endif

/* The first thing main() does on Android: stdout and stderr go to logcat
 * (tag GAME_NAME), and the directories android/GameActivity.java unpacked
 * the game into become GAME_ENV("DATA_DIR") and GAME_ENV("WRITE_ROOT"). */
void Game_AndroidSetup(void);

#ifdef __cplusplus
}
#endif

#endif

#endif /* _GAME_ANDROID_H_INCLUDED_ */
