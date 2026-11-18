/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
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

#if !defined(_WINAPI_WINMM_H_INCLUDED_)
#define _WINAPI_WINMM_H_INCLUDED_

#include <stdint.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t wPeriodMin; uint32_t wPeriodMax; } TIMECAPS_c;

uint32_t CCALL GetTickCount_c(void);
uint32_t CCALL timeGetTime_c(void);
uint32_t CCALL timeSetEvent_c(uint32_t delay, uint32_t resolution, void *lpTimeProc, void *dwUser, uint32_t fuEvent);
uint32_t CCALL timeKillEvent_c(uint32_t id);
uint32_t CCALL timeBeginPeriod_c(uint32_t period_ms);
uint32_t CCALL timeEndPeriod_c(uint32_t period_ms);
uint32_t CCALL timeGetDevCaps_c(void *ptc, uint32_t cbtc);

uint32_t CCALL joyGetNumDevs_c(void);
uint32_t CCALL joyGetDevCapsA_c(uint32_t id, uint32_t pjc, uint32_t cbjc);
uint32_t CCALL joyGetPosEx_c(uint32_t id, uint32_t pji);
uint32_t CCALL joyGetPos_c(uint32_t id, uint32_t pji);
uint32_t CCALL joyConfigChanged_c(uint32_t flags);
uint32_t CCALL joySetCapture_c(uint32_t hwnd, uint32_t id, uint32_t period, uint32_t changed);
uint32_t CCALL joyReleaseCapture_c(uint32_t id);
uint32_t CCALL sndPlaySoundA_c(uint32_t lpszSound, uint32_t fuSound);
uint32_t CCALL midiOutGetNumDevs_c(void);
uint32_t CCALL waveOutGetNumDevs_c(void);
uint32_t CCALL acmMetrics_c(uint32_t hao, uint32_t uMetric, uint32_t pMetric);

#ifdef __cplusplus
}
#endif

#endif

