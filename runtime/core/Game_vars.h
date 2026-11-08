/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
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

#if !defined(_GAME_VARS_H_INCLUDED_)
#define _GAME_VARS_H_INCLUDED_

#include <errno.h>
#include <stdio.h>
#include "Game_defs.h"

#if defined(DEFINE_VARIABLES)
    #define EXTERNAL_VARIABLE
    #ifdef __cplusplus
        #define EXTERNAL_CVAR_BGN extern "C" {
        #define EXTERNAL_CVAR_END }
    #else
        #define EXTERNAL_CVAR_BGN
        #define EXTERNAL_CVAR_END
    #endif
#else
    #define EXTERNAL_VARIABLE extern
    #ifdef __cplusplus
        #define EXTERNAL_CVAR_BGN extern "C"
    #else
        #define EXTERNAL_CVAR_BGN extern
    #endif
    #define EXTERNAL_CVAR_END
#endif

EXTERNAL_CVAR_BGN uint32_t X86_InterruptFlag;		/* interrupt flag indicator */ EXTERNAL_CVAR_END

EXTERNAL_VARIABLE uint8_t *Game_FrameBuffer;		/* video memory (all) */
EXTERNAL_CVAR_BGN PTR32(uint8_t) Game_ScreenWindow;	/* video bank (64KiB) */ EXTERNAL_CVAR_END
EXTERNAL_VARIABLE uint32_t Game_ScreenWindowNum;	/* screen number in video bank */

// global display variables
EXTERNAL_VARIABLE uint32_t Display_Bitsperpixel;	/* display bits per pixel */
EXTERNAL_VARIABLE uint32_t Display_MouseLocked;		/* is mouse locked in the window ? */
EXTERNAL_VARIABLE uint32_t Render_Width;			/* render width */
EXTERNAL_VARIABLE uint32_t Render_Height;			/* render height */
EXTERNAL_VARIABLE Game_Flip_Procedure Display_Flip_Procedure;	/* flip procedure */

EXTERNAL_VARIABLE SDL_Window *Game_Window;
EXTERNAL_VARIABLE void *Game_TextureData;

// global audio variables
EXTERNAL_CVAR_BGN uint32_t Game_Sound;				/* is sound enabled ? */ EXTERNAL_CVAR_END
EXTERNAL_CVAR_BGN uint32_t Game_Music;				/* is music enabled ? */ EXTERNAL_CVAR_END
EXTERNAL_VARIABLE unsigned int Game_AudioFormat;	/* audio format */
EXTERNAL_VARIABLE unsigned int Game_AudioChannels;	/* number of audio channels */
EXTERNAL_VARIABLE int Game_AudioRate;				/* audio rate (Hz) */
EXTERNAL_VARIABLE int Game_AudioBufferSize;			/* audio buffer size */

// 3d engine variables
EXTERNAL_VARIABLE int Game_3DWidth;             /* Display_3D_Resolution=WxH: the 3D drawn at that */
EXTERNAL_VARIABLE int Game_3DHeight;            /* resolution (Glide); 0 = the screen's */
EXTERNAL_VARIABLE char Game_Shader[16];         /* Display_Shader=none|smooth|pixelart|scanlines|crt: the 2D filter (Game_gl.h) */
EXTERNAL_VARIABLE uint32_t Game_Fullscreen;     /* Display_Fullscreen=on|off: the window on the whole screen (Game_thread.h) */
EXTERNAL_VARIABLE uint32_t Game_AdditionalResolutions; /* Display_Additional_Resolutions=on|off: DirectDraw lists modes above 800x600 */

EXTERNAL_VARIABLE volatile uint32_t Game_TimerRunning;	/* is timer interrupt running ? */
EXTERNAL_CVAR_BGN volatile uint32_t Game_TimerTick;	/* Timer tick counter */ EXTERNAL_CVAR_END
EXTERNAL_CVAR_BGN volatile uint32_t Game_TimerRun;	/* Timer number of runs counter */ EXTERNAL_CVAR_END
EXTERNAL_CVAR_BGN volatile uint32_t Game_VSyncTick;	/* VSync tick counter */ EXTERNAL_CVAR_END
EXTERNAL_VARIABLE volatile uint32_t Thread_Exited;	/* did main thread exit ? */
EXTERNAL_VARIABLE volatile uint32_t Thread_Exit;	/* should thread exit ? */

EXTERNAL_VARIABLE SDL_sem *Game_FlipSem;

EXTERNAL_VARIABLE SDL_Event Game_EventMQueue[GAME_MQUEUE_LENGTH];	/* game mouse event queue */
EXTERNAL_VARIABLE SDL_Event Game_EventKQueue[GAME_KQUEUE_LENGTH];	/* game keyboard event queue */
EXTERNAL_VARIABLE volatile uint32_t Game_MQueueWrite;				/* index of first empty event */
EXTERNAL_VARIABLE volatile uint32_t Game_MQueueRead;				/* index of first non-empty event */
EXTERNAL_VARIABLE volatile uint32_t Game_KQueueWrite;				/* index of first empty event */
EXTERNAL_VARIABLE volatile uint32_t Game_KQueueRead;				/* index of first non-empty event */

EXTERNAL_VARIABLE int Game_ExitCode;				/* exit code */

//senquack - multiple config files now supported:
EXTERNAL_VARIABLE char Game_ConfigFilename[4096];		/* Config file being used */

#undef EXTERNAL_VARIABLE
#undef EXTERNAL_CVAR_BGN
#undef EXTERNAL_CVAR_END

extern int CCALL Game_Main_Asm(int argc, char *argv[]);
extern NORETURN void CCALL Game_StopMain_Asm(void);

extern uint32_t CCALL Game_MouseMove(uint32_t state, uint32_t x, uint32_t y);
extern uint32_t CCALL Game_MouseButton(uint32_t state, uint32_t action);
extern void CCALL Game_RunTimer_Asm(void);

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t mouse_pos[2];
extern PTR32(uint8_t) screen_window_ptr[]; // if this is defined as screen_window_ptr[4], then clang thinks it's aligned to 16 bytes (it's not) and generates movdqa instruction which crashes
extern uint8_t keyboard_keys[0x80];

extern int32_t errno_val;

#ifdef __cplusplus
}
#endif

#endif /* _GAME_VARS_H_INCLUDED_ */
