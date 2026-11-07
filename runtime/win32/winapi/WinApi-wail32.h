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


/* The Miles Sound System of WAIL32.DLL (GAME_WINAPI_WAIL32 1): WinApi-wail32.c. */

#if !defined(_WINAPI_WAIL32_H_INCLUDED_)
#define _WINAPI_WAIL32_H_INCLUDED_

#include <stdint.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL AIL_startup_c(void);
uint32_t CCALL AIL_shutdown_c(void);
uint32_t CCALL AIL_set_preference_c(uint32_t number, uint32_t value);
uint32_t CCALL AIL_waveOutOpen_c(uint32_t drvr, uint32_t lphWaveOut, uint32_t wDeviceID, uint32_t lpFormat);
uint32_t CCALL AIL_waveOutClose_c(uint32_t dig);
uint32_t CCALL AIL_midiOutOpen_c(uint32_t mdi, uint32_t lphMidiOut, uint32_t dwDeviceID);
uint32_t CCALL AIL_midiOutClose_c(uint32_t mdi);
uint32_t CCALL AIL_allocate_sample_handle_c(uint32_t dig);
uint32_t CCALL AIL_release_sample_handle_c(uint32_t S);
uint32_t CCALL AIL_init_sample_c(uint32_t S);
uint32_t CCALL AIL_set_sample_file_c(uint32_t S, uint32_t file_image, uint32_t block);
uint32_t CCALL AIL_set_sample_address_c(uint32_t S, uint32_t start, uint32_t length);
uint32_t CCALL AIL_set_sample_type_c(uint32_t S, uint32_t format, uint32_t flags);
uint32_t CCALL AIL_set_sample_playback_rate_c(uint32_t S, uint32_t rate);
uint32_t CCALL AIL_set_sample_volume_c(uint32_t S, uint32_t volume);
uint32_t CCALL AIL_set_sample_pan_c(uint32_t S, uint32_t pan);
uint32_t CCALL AIL_set_sample_loop_count_c(uint32_t S, uint32_t count);
uint32_t CCALL AIL_start_sample_c(uint32_t S);
uint32_t CCALL AIL_stop_sample_c(uint32_t S);
uint32_t CCALL AIL_resume_sample_c(uint32_t S);
uint32_t CCALL AIL_end_sample_c(uint32_t S);
uint32_t CCALL AIL_sample_status_c(uint32_t S);
uint32_t CCALL AIL_allocate_sequence_handle_c(uint32_t mdi);
uint32_t CCALL AIL_release_sequence_handle_c(uint32_t S);
uint32_t CCALL AIL_init_sequence_c(uint32_t S, uint32_t start, uint32_t sequence_num);
uint32_t CCALL AIL_start_sequence_c(uint32_t S);
uint32_t CCALL AIL_stop_sequence_c(uint32_t S);
uint32_t CCALL AIL_resume_sequence_c(uint32_t S);
uint32_t CCALL AIL_end_sequence_c(uint32_t S);
uint32_t CCALL AIL_sequence_status_c(uint32_t S);
uint32_t CCALL AIL_serve_c(void);
uint32_t CCALL AIL_last_error_c(void);
uint32_t CCALL MEM_alloc_lock_c(uint32_t size);
uint32_t CCALL MEM_free_lock_c(uint32_t ptr);
uint32_t CCALL AIL_copy_to_MEM16_c(uint32_t dest, uint32_t src, uint32_t size);
uint32_t CCALL FILE_read_c(uint32_t filename, uint32_t dest);

#ifdef __cplusplus
}
#endif

#endif
