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

/* The runtime of a Win32 port: the game directory (Game_gamedir.c), the disc,
 * CD audio, and the DOS-only hooks it does not reach.  Game_RuntimeInit and
 * Game_RuntimeDeinit are called from the game's main.c. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_sound.h"
#include "Game_defs.h"
#include "virtualfs.h"
#include "Game_cdrom.h"
#include "Game_file.h"
#include "Game_runtime.h"
#include "Game_music.h"
#include "Game_hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game opens ERROR.LOG only on its fatal-error path (Game_vfs.c calls this
 * then); there is no guest memory worth dumping in a Win32 port. */
void Game_DumpGuestMemory(void)
{
}

/* The DOS-only hooks that SR's llasm support links against.  A Win32 program
 * does not reach them: a software interrupt, a port or a segment-override
 * access in its code is a bug of the translation, so each one says so. */

void X86_InterruptProcedure(uint8_t interrupt_number, void *registers)
{
    (void)registers;
    fprintf(stderr, GAME_NAME ": unexpected software interrupt %02x\n", interrupt_number);
}

uint32_t X86_InPortProcedure(uint16_t port, uint32_t size)
{
    (void)size;
    fprintf(stderr, GAME_NAME ": unexpected read of port %04x\n", port);
    return 0;
}

void X86_OutPortProcedure(uint16_t port, uint32_t size, uint32_t value)
{
    (void)size;
    (void)value;
    fprintf(stderr, GAME_NAME ": unexpected write to port %04x\n", port);
}

uint32_t X86_ReadMemProcedure(uint32_t address, uint32_t size)
{
    (void)size;
    fprintf(stderr, GAME_NAME ": unexpected segment-override read at %08x\n", address);
    return 0;
}

void X86_WriteMemProcedure(uint32_t address, uint32_t size, uint32_t value)
{
    (void)size;
    (void)value;
    fprintf(stderr, GAME_NAME ": unexpected segment-override write at %08x\n", address);
}

/* The SDL_mixer device that CD audio (Game_music.c) plays through, in the format
 * audio/pc.c's Init_Audio chose.  The digital sound of a Win32 game goes through
 * its own DirectSound layer (WinApi-dsound.c).  A failure only silences the music. */
static int audio_open;

int Game_AudioInit(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, GAME_NAME ": no audio: %s\n", SDL_GetError());
        return -1;
    }
    if (Mix_OpenAudio(Game_AudioRate, (Uint16)Game_AudioFormat, (int)Game_AudioChannels, Game_AudioBufferSize) != 0) {
        fprintf(stderr, GAME_NAME ": no audio: %s\n", Mix_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    audio_open = 1;
    return 0;
}

void Game_AudioDeinit(void)
{
    if (!audio_open) return;
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_open = 0;
}

int Game_AudioIsOpen(void)
{
    return audio_open;
}

int Game_RuntimeInit(void)
{
    if (Game_FilesInit() != 0) return -1;
    /* no audio device only silences the music */
    Game_AudioInit();
    Game_CdInit(Game_DataRoot());
    Game_MusicInit();

    return 0;
}

void Game_RuntimeDeinit(void)
{
    /* A port's own Mix_Music (the MIDI of a Miles layer, say) is still loaded:
     * Mix_Quit unloads the MIDI backend (fluidsynth) and Mix_CloseAudio's later
     * halt of that music would call into the unloaded library. */
    if (Game_AudioIsOpen()) Mix_HaltMusic();
    Game_MusicDeinit();
    Game_CdDeinit();
    Game_AudioDeinit();
    Game_FilesDeinit();
}

#ifdef __cplusplus
}
#endif
