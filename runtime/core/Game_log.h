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

#ifndef _GAME_LOG_H_INCLUDED_
#define _GAME_LOG_H_INCLUDED_

/* A Windows program starts without a console.  Game_LogOpen gives its stdout
 * and stderr a file (GAME_LOG_FILE of Game_hooks.h, "game.log" by default) in
 * the write root, unless the caller gave it somewhere to write.  Game_LogShow
 * shows the last lines of that file in a message box, for a program that
 * stopped on an error.  Game_CrashHandlerInstall makes an exception nobody
 * handles write what is known of it (address, registers, stack, the guest's
 * registers and stack and the guest address of the access) to stderr and end
 * the process; on Linux and Android it is a signal (SEGV, BUS, ILL, FPE) with
 * the registers, the module and offset of the code, and on Android also
 * crash-native.txt in Android/data/<package>/files/.  Elsewhere the other
 * three do nothing. */

#if defined(_WIN32)
void Game_LogOpen(void);
int Game_LogHasStdout(void);    /* the log file is where stdout goes */
void Game_LogShow(void);
#else
static inline void Game_LogOpen(void) {}
static inline int Game_LogHasStdout(void) { return 0; }
static inline void Game_LogShow(void) {}
#endif
void Game_CrashHandlerInstall(void);

#endif /* _GAME_LOG_H_INCLUDED_ */
