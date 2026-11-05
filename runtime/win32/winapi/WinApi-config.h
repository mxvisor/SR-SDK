/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer (Game-Config.h)
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

#if !defined(_GAME_CONFIG_H_INCLUDED_)
#define _GAME_CONFIG_H_INCLUDED_

#ifdef GAME_CONFIG_DEFINE_VARIABLES
#define GAME_CONFIG_STORAGE
#else
#define GAME_CONFIG_STORAGE extern
#endif

GAME_CONFIG_STORAGE int Display_VSync;          /* vsync enabled ? */
GAME_CONFIG_STORAGE int Display_DelayAfterFlip; /* delay after image flip in miliseconds */

GAME_CONFIG_STORAGE int Audio_SampleRate;       /* audio sample rate (in Hz) */
GAME_CONFIG_STORAGE int Audio_BufferSize;       /* audio buffer size (in samples) */
GAME_CONFIG_STORAGE int Audio_ResamplingQuality;/* resampling quality
                                                       0: normal quality
                                                       1: higher quality */

GAME_CONFIG_STORAGE int Input_GameController;   /* use gamepad or joystick as mouse/keyboard ? */

GAME_CONFIG_STORAGE int Controller_Deadzone;    /* controller deadzone */

GAME_CONFIG_STORAGE int Keys_SwitchWSAD;        /* switch WSAD keys for arrow keys ? */
GAME_CONFIG_STORAGE int Keys_SwitchArrowKeys;   /* switch arrow keys for WSAD keys ? */

GAME_CONFIG_STORAGE int CPU_SleepMode;          /* cpu sleep mode - 0 = standard; 1 = reduced; 2 = no sleep */

#endif

