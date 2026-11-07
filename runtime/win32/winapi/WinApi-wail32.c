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

/* The Miles Sound System for Win32 (WAIL32.DLL, AIL 3.03) for a game with
 * GAME_WINAPI_WAIL32 1 in Game_hooks.h: the stdcall entry points over the
 * SDK's AIL (core/Game_ail.c).  A sample is a WAV image in the guest's memory
 * (AIL_set_sample_file) or raw PCM of a type (AIL_set_sample_address and
 * AIL_set_sample_type); a sequence is XMIDI (AIL_init_sequence).  A game that
 * loads the DLL with LoadLibrary finds the functions with GetProcAddress
 * (GAME_EXTRA_PROCS), one that imports them through its import shim
 * (game/import_args.tsv). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_hooks.h"

#if GAME_WINAPI_WAIL32

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "Game_ail.h"
#include "Game_vfs.h"
#include "WinApi.h"
#include "WinApi-wail32.h"

#define WAIL_DIG_DRIVER     0x57440000u
#define WAIL_MDI_DRIVER     0x574d0000u
#define WAIL_SAMPLE_BASE    0x57530000u
#define WAIL_SEQUENCE_BASE  0x57510000u

static uint32_t last_error;     /* the guest's string of AIL_last_error */

/* a handle of the game: base + 16 * the number in the pool */
static int pool_index(uint32_t handle, uint32_t base, int count)
{
    uint32_t i;

    if (handle < base || ((handle - base) & 15)) return -1;
    i = (handle - base) / 16;
    return (i < (uint32_t)count) ? (int)i : -1;
}

#define SAMPLE(S)   pool_index((S), WAIL_SAMPLE_BASE, GAME_AIL_SAMPLES)
#define SEQUENCE(S) pool_index((S), WAIL_SEQUENCE_BASE, GAME_AIL_SEQUENCES)

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL AIL_startup_c(void)
{
    return 1;
}

uint32_t CCALL AIL_shutdown_c(void)
{
    Game_AilShutdown();
    return 0;
}

uint32_t CCALL AIL_set_preference_c(uint32_t number, uint32_t value)
{
    return (uint32_t)Game_AilSetPreference(number, (int32_t)value);
}

/* the driver of the wave device: the port's mixer, whatever the format asked;
 * the format's rate is the one of the samples AIL_init_sample sets up */
uint32_t CCALL AIL_waveOutOpen_c(uint32_t drvr, uint32_t lphWaveOut, uint32_t wDeviceID, uint32_t lpFormat)
{
    (void)wDeviceID;
    if (lpFormat != 0)
    {
        int32_t rate;

        memcpy(&rate, (const uint8_t *)GUEST_MEM(lpFormat) + 4, 4);    /* WAVEFORMAT.nSamplesPerSec */
        Game_AilSetDriverRate(rate);
    }
    if (drvr != 0) *(uint32_t *)GUEST_MEM(drvr) = WAIL_DIG_DRIVER;
    if (lphWaveOut != 0) *(uint32_t *)GUEST_MEM(lphWaveOut) = 0;
    return 0;
}

uint32_t CCALL AIL_waveOutClose_c(uint32_t dig)
{
    (void)dig;
    return 0;
}

/* the MIDI driver: the sequences play through Game_midi */
uint32_t CCALL AIL_midiOutOpen_c(uint32_t mdi, uint32_t lphMidiOut, uint32_t dwDeviceID)
{
    (void)dwDeviceID;
    if (mdi != 0) *(uint32_t *)GUEST_MEM(mdi) = WAIL_MDI_DRIVER;
    if (lphMidiOut != 0) *(uint32_t *)GUEST_MEM(lphMidiOut) = 0;
    return 0;
}

uint32_t CCALL AIL_midiOutClose_c(uint32_t mdi)
{
    (void)mdi;
    return 0;
}

/* ---- samples ---- */

uint32_t CCALL AIL_allocate_sample_handle_c(uint32_t dig)
{
    int i = Game_AilAllocateSample();

    (void)dig;
    return (i < 0) ? 0 : WAIL_SAMPLE_BASE + (uint32_t)i * 16;
}

uint32_t CCALL AIL_release_sample_handle_c(uint32_t S)
{
    Game_AilReleaseSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_init_sample_c(uint32_t S)
{
    Game_AilInitSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_set_sample_file_c(uint32_t S, uint32_t file_image, uint32_t block)
{
    (void)block;
    if (file_image == 0) return 0;
    return (uint32_t)Game_AilSetSampleWav(SAMPLE(S), GUEST_MEM(file_image));
}

uint32_t CCALL AIL_set_sample_address_c(uint32_t S, uint32_t start, uint32_t length)
{
    Game_AilSetSampleAddress(SAMPLE(S), (start != 0) ? GUEST_MEM(start) : NULL, length);
    return 0;
}

uint32_t CCALL AIL_set_sample_type_c(uint32_t S, uint32_t format, uint32_t flags)
{
    Game_AilSetSampleType(SAMPLE(S), (int32_t)format, flags);
    return 0;
}

uint32_t CCALL AIL_set_sample_playback_rate_c(uint32_t S, uint32_t rate)
{
    Game_AilSetSampleRate(SAMPLE(S), (int32_t)rate);
    return 0;
}

uint32_t CCALL AIL_set_sample_volume_c(uint32_t S, uint32_t volume)
{
    Game_AilSetSampleVolume(SAMPLE(S), (int32_t)volume);
    return 0;
}

uint32_t CCALL AIL_set_sample_pan_c(uint32_t S, uint32_t pan)
{
    Game_AilSetSamplePan(SAMPLE(S), (int32_t)pan);
    return 0;
}

uint32_t CCALL AIL_set_sample_loop_count_c(uint32_t S, uint32_t count)
{
    Game_AilSetSampleLoopCount(SAMPLE(S), (int32_t)count);
    return 0;
}

uint32_t CCALL AIL_start_sample_c(uint32_t S)
{
    Game_AilStartSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_stop_sample_c(uint32_t S)
{
    Game_AilStopSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_resume_sample_c(uint32_t S)
{
    Game_AilResumeSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_end_sample_c(uint32_t S)
{
    Game_AilEndSample(SAMPLE(S));
    return 0;
}

uint32_t CCALL AIL_sample_status_c(uint32_t S)
{
    return Game_AilSampleStatus(SAMPLE(S));
}

/* ---- sequences ---- */

uint32_t CCALL AIL_allocate_sequence_handle_c(uint32_t mdi)
{
    int i = Game_AilAllocateSequence();

    (void)mdi;
    return (i < 0) ? 0 : WAIL_SEQUENCE_BASE + (uint32_t)i * 16;
}

uint32_t CCALL AIL_release_sequence_handle_c(uint32_t S)
{
    Game_AilReleaseSequence(SEQUENCE(S));
    return 0;
}

uint32_t CCALL AIL_init_sequence_c(uint32_t S, uint32_t start, uint32_t sequence_num)
{
    if (start == 0) return 0;
    return (uint32_t)Game_AilInitSequence(SEQUENCE(S), GUEST_MEM(start), sequence_num);
}

uint32_t CCALL AIL_start_sequence_c(uint32_t S)
{
    Game_AilStartSequence(SEQUENCE(S));
    return 0;
}

uint32_t CCALL AIL_stop_sequence_c(uint32_t S)
{
    Game_AilStopSequence(SEQUENCE(S));
    return 0;
}

uint32_t CCALL AIL_resume_sequence_c(uint32_t S)
{
    Game_AilResumeSequence(SEQUENCE(S));
    return 0;
}

uint32_t CCALL AIL_end_sequence_c(uint32_t S)
{
    Game_AilEndSequence(SEQUENCE(S));
    return 0;
}

uint32_t CCALL AIL_sequence_status_c(uint32_t S)
{
    return Game_AilSequenceStatus(SEQUENCE(S));
}

/* ---- the rest ---- */

/* the mixer serves itself */
uint32_t CCALL AIL_serve_c(void)
{
    return 0;
}

uint32_t CCALL AIL_last_error_c(void)
{
    if (last_error == 0)
    {
        char *text = (char *)x86_malloc(1);
        if (text != NULL) text[0] = 0;
        last_error = GUEST_ADDR(text);
    }
    return last_error;
}

/* memory the sounds are copied into: the guest's, zeroed */
uint32_t CCALL MEM_alloc_lock_c(uint32_t size)
{
    void *block = x86_calloc(1, size != 0 ? size : 1);

    return (block != NULL) ? GUEST_ADDR(block) : 0;
}

uint32_t CCALL MEM_free_lock_c(uint32_t ptr)
{
    if (ptr != 0) x86_free(GUEST_MEM(ptr));
    return 0;
}

uint32_t CCALL AIL_copy_to_MEM16_c(uint32_t dest, uint32_t src, uint32_t size)
{
    if (dest != 0 && src != 0) memmove(GUEST_MEM(dest), GUEST_MEM(src), size);
    return 0;
}

/* a file of the game into memory: into dest, or a block of its own for dest 0 */
uint32_t CCALL FILE_read_c(uint32_t filename, uint32_t dest)
{
    char path[1024];
    FILE *file;
    long size;
    void *out;

    if (filename == 0 || !Game_ResolveDataPath((const char *)GUEST_MEM(filename), path, sizeof(path))) return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    out = (dest != 0) ? GUEST_MEM(dest) : x86_malloc((unsigned int)(size > 0 ? size : 1));
    if (out == NULL || size < 0 || fread(out, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    return GUEST_ADDR(out);
}

#ifdef __cplusplus
}
#endif

#endif /* GAME_WINAPI_WAIL32 */
