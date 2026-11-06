/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer (main.c)
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

/* The variables of Game_vars.h and Game_CleanState, which the game thread
 * calls before it runs the game. */

#include <SDL.h>

#include "Game_defs.h"

#define DEFINE_VARIABLES
    #include "Game_vars.h"
#undef DEFINE_VARIABLES

#include "Game_memory.h"
#include "main.h"

/* The state the game thread starts with (Game_MainThread): the video bank at
 * the start of the video memory and the event queues empty. */
void Game_CleanState(int imm)
{
    (void)imm;

    Game_ScreenWindow = Game_FrameBuffer;
    Game_ScreenWindowNum = 0;

    Game_MQueueWrite = 0;
    Game_MQueueRead = 0;
    Game_KQueueWrite = 0;
    Game_KQueueRead = 0;
}
