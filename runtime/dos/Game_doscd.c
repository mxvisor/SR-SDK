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

/* MSCDEX (INT 2Fh AH=15h) on Game_cdrom.c's disc.  The device requests of
 * AX=1510h are the IOCTL input ones that read the table of contents and the
 * audio status, and PLAY/STOP/RESUME AUDIO, which go to Game_music.c.
 * Positions are in frames (sectors of 1/75 s): HSG counts them from the
 * start of the data, Red Book MSF from 150 frames before it. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_defs.h"
#include "Game_cdrom.h"
#include "Game_doscd.h"
#include "Game_dosmem.h"
#include "Game_music.h"
#include "Game_vfs.h"
#include "Game_hooks.h"

#define FLAG_CF 0x0001u

/* request header status: done, busy (audio playing), error */
#define STATUS_DONE 0x0100u
#define STATUS_BUSY 0x0200u
#define STATUS_ERROR 0x8000u
#define ERROR_UNKNOWN_UNIT 0x01u
#define ERROR_NOT_READY 0x02u
#define ERROR_UNKNOWN_COMMAND 0x03u
#define ERROR_GENERAL 0x0cu
#define ERROR_INVALID 0x0fu

/* <GAME>_TRACE_CD */
static int trace_cd(void)
{
    static int value = -1;
    if (value < 0) value = getenv(GAME_ENV("TRACE_CD")) != NULL;
    return value;
}

static uint16_t load16(const uint8_t *pointer)
{
    uint16_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static uint32_t load32(const uint8_t *pointer)
{
    uint32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void store16(uint8_t *pointer, uint16_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

static void store32(uint8_t *pointer, uint32_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

/* HSG frames <-> Red Book MSF, packed frame, second, minute from the low byte */
static uint32_t cd_msf(int32_t frames)
{
    frames += 150;
    return (uint32_t)(frames % 75) | ((uint32_t)((frames / 75) % 60) << 8) | ((uint32_t)(frames / 4500) << 16);
}

static int32_t cd_msf_to_frames(uint32_t msf)
{
    return (int32_t)(((msf >> 16) & 0xffu) * 4500u + ((msf >> 8) & 0xffu) * 75u + (msf & 0xffu)) - 150;
}

/* IOCTL input (command 03h): the control block's first byte says what. */
static uint16_t ioctl_input(uint8_t *control)
{
    int first, last, track, paused, audio;
    int32_t leadout, frame, at;
    uint32_t left, msf;

    switch (control[0]) {
    case 0x00:
        /* address of the device header: there is no driver in memory */
        store32(control + 1, 0);
        return 0;
    case 0x04:
        /* audio channel info: input channel and volume of outputs 0-3 */
        for (int output = 0; output < 4; output++) {
            control[1 + 2 * output] = (uint8_t)output;
            control[2 + 2 * output] = 0xff;
        }
        return 0;
    case 0x06:
        /* device status: HSG and Red Book addressing */
        store32(control + 1, 0x00000200u);
        return 0;
    case 0x08:
        /* volume size, in frames */
        if (Game_CdTocRange(&first, &last, &leadout) != 0) return ERROR_NOT_READY;
        store32(control + 1, (uint32_t)leadout);
        return 0;
    case 0x09:
        /* media changed: 1 = no */
        control[1] = 1;
        return 0;
    case 0x0a:
        /* disc info: first and last track, the lead-out */
        if (Game_CdTocRange(&first, &last, &leadout) != 0) return ERROR_NOT_READY;
        control[1] = (uint8_t)first;
        control[2] = (uint8_t)last;
        store32(control + 3, cd_msf(leadout));
        return 0;
    case 0x0b:
        /* track info: its start and its control byte (40h = data) */
        if (Game_CdTrackInfo(control[1], &at, &audio) != 0) return ERROR_INVALID;
        store32(control + 2, cd_msf(at));
        control[6] = audio ? 0x00 : 0x40;
        return 0;
    case 0x0c:
        /* audio Q channel: control/ADR, track, index, the position in the
         * track and on the disc (minute, second, frame, from the first byte) */
        control[1] = 0x10;
        if (!Game_MusicStatus(&track, &frame, &left, &paused) || Game_CdTrackInfo(track, &at, &audio) != 0) {
            memset(control + 2, 0, 10);
            return 0;
        }
        control[2] = (uint8_t)track;
        control[3] = 1;
        msf = cd_msf(frame - at - 150);
        control[4] = (uint8_t)(msf >> 16); control[5] = (uint8_t)(msf >> 8); control[6] = (uint8_t)msf; control[7] = 0;
        msf = cd_msf(frame);
        control[8] = (uint8_t)(msf >> 16); control[9] = (uint8_t)(msf >> 8); control[10] = (uint8_t)msf; control[11] = 0;
        return 0;
    case 0x0f: {
        /* audio status: bit 0 = paused; the start and the end of the play */
        const int active = Game_MusicStatus(&track, &frame, &left, &paused);
        store16(control + 1, (active && paused) ? 1u : 0u);
        store32(control + 3, active ? cd_msf(frame) : 0);
        store32(control + 7, active ? cd_msf(frame + (int32_t)left) : 0);
        return 0;
    }
    default:
        fprintf(stderr, GAME_NAME ": unimplemented MSCDEX IOCTL input %02x\n", control[0]);
        return ERROR_UNKNOWN_COMMAND;
    }
}

/* PLAY AUDIO (84h): +0Dh addressing mode (0 HSG, 1 Red Book), +0Eh the
 * first frame, +12h the number of frames.  The play starts in the audio
 * track that holds the first frame. */
static uint16_t play_audio(const uint8_t *request)
{
    int32_t start = (int32_t)load32(request + 0x0e);
    const uint32_t frames = load32(request + 0x12);
    int first, last, track = 0, audio;
    int32_t leadout, at;

    if (request[0x0d] == 1) start = cd_msf_to_frames((uint32_t)start);
    if (Game_CdTocRange(&first, &last, &leadout) == 0) {
        for (int candidate = first; candidate <= last; candidate++) {
            if (Game_CdTrackInfo(candidate, &at, &audio) == 0 && audio && at <= start) track = candidate;
        }
    }
    if (trace_cd()) fprintf(stderr, "CD play mode=%u start=%d frames=%u: track %d\n", request[0x0d], start, frames, track);
    if (track == 0) return ERROR_GENERAL;
    Game_CdTrackInfo(track, &at, &audio);
    Game_MusicPlay(track, (uint32_t)(start - at), frames);
    return 0;
}

/* A device request (AX=1510h): ES:BX is its header, +2 the command, +3 the
 * status; IOCTL's control block is the far pointer at +0Eh. */
static void device_request(uint8_t *request)
{
    const uint8_t command = request[2];
    uint16_t error = 0;
    int track, paused;
    int32_t frame;
    uint32_t left;

    switch (command) {
    case 0x03: {
        uint8_t *control = Game_DosMemPtr(((uint32_t)load16(request + 0x10) << 4) + load16(request + 0x0e), 12);
        if (trace_cd()) fprintf(stderr, "CD ioctl %02x\n", control != NULL ? control[0] : 0xff);
        error = control != NULL ? ioctl_input(control) : ERROR_GENERAL;
        break;
    }
    case 0x0c: /* IOCTL output (eject, lock, reset, audio channel control,
                * close): accepted, nothing changes */
    case 0x0d: /* device open */
    case 0x0e: /* device close */
        break;
    case 0x84:
        error = play_audio(request);
        break;
    case 0x85:
        /* STOP AUDIO: the first one pauses, a second one ends the play */
        if (trace_cd()) fprintf(stderr, "CD stop\n");
        Game_MusicStop();
        break;
    case 0x88:
        if (trace_cd()) fprintf(stderr, "CD resume\n");
        Game_MusicResume();
        break;
    default:
        fprintf(stderr, GAME_NAME ": unimplemented MSCDEX request %02x\n", command);
        error = ERROR_UNKNOWN_COMMAND;
        break;
    }
    uint16_t status = STATUS_DONE;
    if (error != 0) status |= STATUS_ERROR | error;
    else if (Game_MusicStatus(&track, &frame, &left, &paused) && !paused) status |= STATUS_BUSY;
    store16(request + 3, status);
}

static uint16_t regs_cx(const Game_DosRegs *regs)
{
    return (uint16_t)regs->ecx;
}

static void set_ax(Game_DosRegs *regs, uint16_t value)
{
    regs->eax = (regs->eax & 0xffff0000u) | value;
}

static void set_bx(Game_DosRegs *regs, uint16_t value)
{
    regs->ebx = (regs->ebx & 0xffff0000u) | value;
}

static void set_cx(Game_DosRegs *regs, uint16_t value)
{
    regs->ecx = (regs->ecx & 0xffff0000u) | value;
}

int Game_DosCdCall(Game_DosRegs *regs, uint16_t es)
{
    const uint16_t function = (uint16_t)regs->eax;
    const uint32_t buffer = ((uint32_t)es << 4) + (regs->ebx & 0xffffu);
    uint8_t *pointer;

    if ((function >> 8) != 0x15) return 0;
    if (trace_cd()) fprintf(stderr, "CD int2f %04x bx=%04x cx=%04x es=%04x\n", function, (unsigned)(uint16_t)regs->ebx, regs_cx(regs), es);
    regs->eflags &= ~FLAG_CF;
    switch (function) {
    case 0x1500:
        /* installation check: the number of CD drives and the first one */
        set_bx(regs, 1);
        set_cx(regs, GAME_CD_DRIVE_INDEX);
        break;
    case 0x1501:
        /* drive device list: subunit and device header for each drive */
        pointer = Game_DosMemPtr(buffer, 5);
        if (pointer != NULL) memset(pointer, 0, 5);
        break;
    case 0x1502: /* copyright file name */
    case 0x1503: /* abstract file name */
    case 0x1504: /* bibliographic file name */
        pointer = Game_DosMemPtr(buffer, 1);
        if (pointer != NULL) pointer[0] = 0;
        break;
    case 0x150b:
        /* drive check: AX nonzero for a CD drive (5AD4h, as MSCDEX gives it),
         * BX = ADADh (MSCDEX is there) */
        set_ax(regs, regs_cx(regs) == GAME_CD_DRIVE_INDEX ? 0x5ad4u : 0);
        set_bx(regs, 0xadadu);
        break;
    case 0x150c:
        /* version 2.10 */
        set_bx(regs, 0x020au);
        break;
    case 0x150d:
        /* drive letters */
        pointer = Game_DosMemPtr(buffer, 1);
        if (pointer != NULL) pointer[0] = GAME_CD_DRIVE_INDEX;
        break;
    case 0x1510:
        /* device request: CX = drive */
        pointer = Game_DosMemPtr(buffer, 0x16);
        if (pointer == NULL) {
            regs->eflags |= FLAG_CF;
            break;
        }
        if (regs_cx(regs) != GAME_CD_DRIVE_INDEX) {
            store16(pointer + 3, STATUS_DONE | STATUS_ERROR | ERROR_UNKNOWN_UNIT);
            break;
        }
        device_request(pointer);
        break;
    default:
        fprintf(stderr, GAME_NAME ": unimplemented MSCDEX function %04x\n", function);
        set_ax(regs, 1);   /* invalid function */
        regs->eflags |= FLAG_CF;
        break;
    }
    return 1;
}
