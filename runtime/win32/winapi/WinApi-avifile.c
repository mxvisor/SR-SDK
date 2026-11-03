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

/*
 *  AVIFIL32 streams: a reader for AVI files. It hands the frames out as they are
 *  stored; compressed video is decoded by the caller through the compression
 *  manager (WinApi-msvfw.c).
 */

#include "WinApi-game.h"
#include "Game_hooks.h"
#include "CLIB.h"
#include "Game_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AVIERR_UNSUPPORTED     0x80044065u
#define AVIERR_BADFORMAT       0x80044066u
#define AVIERR_MEMORY          0x80044067u
#define AVIERR_BADPARAM        0x8004406Au
#define AVIERR_BADHANDLE       0x8004406Cu
#define AVIERR_FILEREAD        0x8004406Du
#define AVIERR_FILEOPEN        0x8004406Fu
#define AVIERR_NODATA          0x80044073u
#define AVIERR_BUFFERTOOSMALL  0x80044074u

#define AVI_MAX_STREAMS 16
#define AVI_HANDLE_BASE 0x41560000u

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

struct avi_chunk
{
    uint32_t offset;                    /* of the data in the file image */
    uint32_t size;
    uint32_t first_sample;              /* audio: samples before this chunk */
};

struct avi_stream
{
    int used;
    uint8_t *data;                      /* the whole file */
    uint32_t strh[14];                  /* AVISTREAMHEADER after the chunk header, 56 bytes */
    uint8_t *format;                    /* strf */
    uint32_t format_size;
    struct avi_chunk *chunks;
    uint32_t chunk_count;
    uint32_t total_samples;
};

struct avi_header
{
    uint32_t strh[14];
    uint8_t *format;
    uint32_t format_size;
};

static struct avi_stream avi_streams[AVI_MAX_STREAMS];

/* <GAME>_DEBUG_AVI=1 logs the calls and their results */
static int avi_debug(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = (getenv(GAME_ENV("DEBUG_AVI")) != NULL);
    return enabled;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static struct avi_stream *avi_lookup(uint32_t handle)
{
    uint32_t index = handle - AVI_HANDLE_BASE;
    if (handle < AVI_HANDLE_BASE || index >= AVI_MAX_STREAMS || !avi_streams[index].used) return NULL;
    return &avi_streams[index];
}

/* The part of the file the reader walks: the header lists and the 'movi' list. */
static int avi_scan(uint8_t *data, uint32_t size, struct avi_header *headers, struct avi_chunk **chunks, uint32_t *chunk_counts,
                    uint32_t *stream_count)
{
    uint32_t pos, end, streams = 0;
    uint32_t capacity[AVI_MAX_STREAMS];
    int i;

    if (size < 12 || rd32(data) != FOURCC('R', 'I', 'F', 'F') || rd32(data + 8) != FOURCC('A', 'V', 'I', ' ')) return 0;
    end = rd32(data + 4) + 8;
    if (end > size) end = size;

    for (i = 0; i < AVI_MAX_STREAMS; i++)
    {
        chunks[i] = NULL;
        chunk_counts[i] = 0;
        capacity[i] = 0;
    }

    pos = 12;
    while (pos + 8 <= end)
    {
        uint32_t id = rd32(data + pos);
        uint32_t len = rd32(data + pos + 4);

        if (id == FOURCC('L', 'I', 'S', 'T') && pos + 12 <= end)
        {
            uint32_t list = rd32(data + pos + 8);
            if (list == FOURCC('h', 'd', 'r', 'l') || list == FOURCC('s', 't', 'r', 'l') || list == FOURCC('r', 'e', 'c', ' ') ||
                list == FOURCC('m', 'o', 'v', 'i'))
            {
                if (list == FOURCC('s', 't', 'r', 'l'))
                {
                    if (streams >= AVI_MAX_STREAMS) return 0;
                    headers[streams].format = NULL;
                    headers[streams].format_size = 0;
                    memset(headers[streams].strh, 0, sizeof(headers[streams].strh));
                    streams++;
                }
                pos += 12;                          /* descend */
                continue;
            }
        }
        else if (id == FOURCC('s', 't', 'r', 'h') && streams > 0 && len >= 56 && pos + 8 + len <= end)
        {
            memcpy(headers[streams - 1].strh, data + pos + 8, 56);
        }
        else if (id == FOURCC('s', 't', 'r', 'f') && streams > 0 && pos + 8 + len <= end)
        {
            headers[streams - 1].format = data + pos + 8;
            headers[streams - 1].format_size = len;
        }
        else if ((id & 0xff) >= '0' && (id & 0xff) <= '9' && ((id >> 8) & 0xff) >= '0' && ((id >> 8) & 0xff) <= '9' &&
                 pos + 8 + len <= end)
        {
            /* a data chunk: two decimal digits of the stream number, then the kind ('db', 'dc', 'wb', ...) */
            uint32_t stream = ((id & 0xff) - '0') * 10 + (((id >> 8) & 0xff) - '0');
            if (stream < AVI_MAX_STREAMS)
            {
                if (chunk_counts[stream] == capacity[stream])
                {
                    uint32_t new_capacity = capacity[stream] ? capacity[stream] * 2 : 256;
                    struct avi_chunk *grown = (struct avi_chunk *)realloc(chunks[stream], new_capacity * sizeof(struct avi_chunk));
                    if (grown == NULL) return 0;
                    chunks[stream] = grown;
                    capacity[stream] = new_capacity;
                }
                chunks[stream][chunk_counts[stream]].offset = pos + 8;
                chunks[stream][chunk_counts[stream]].size = len;
                chunk_counts[stream]++;
            }
        }

        pos += 8 + len + (len & 1);
    }

    *stream_count = streams;
    return 1;
}

uint32_t CCALL AVIFileInit_c(void)
{
    return 0;
}

uint32_t CCALL AVIFileExit_c(void)
{
    return 0;
}

/* a0 = PAVISTREAM *ppavi, a1 = file name, a2 = stream type (0 = any), a3 = index of the stream of that type,
 * a4 = open mode, a5 = handler CLSID */
uint32_t CCALL AVIStreamOpenFromFileA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    char path[8192];
    FILE *file;
    long file_size;
    uint8_t *data;
    struct avi_header headers[AVI_MAX_STREAMS];
    struct avi_chunk *chunks[AVI_MAX_STREAMS];
    uint32_t chunk_counts[AVI_MAX_STREAMS];
    uint32_t streams = 0, wanted = 0, seen = 0, i, slot;
    int found = -1;
    struct avi_stream *stream;
    (void)a4; (void)a5;

    if (avi_debug()) fprintf(stderr, "[AVIStreamOpenFromFileA %s type=%08x index=%u]\n", a1 ? (const char *)GUEST_MEM(a1) : "(null)", a2, a3);
    if (a0 == 0 || a1 == 0) return AVIERR_BADPARAM;
    *(uint32_t *)GUEST_MEM(a0) = 0;

    if (!CLIB_FindFile((const char *)GUEST_MEM(a1), path)) return AVIERR_FILEOPEN;
    file = fopen(path, "rb");
    if (file == NULL) return AVIERR_FILEOPEN;
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 12) { fclose(file); return AVIERR_BADFORMAT; }
    data = (uint8_t *)malloc((size_t)file_size);
    if (data == NULL || fread(data, 1, (size_t)file_size, file) != (size_t)file_size)
    {
        free(data);
        fclose(file);
        return AVIERR_FILEREAD;
    }
    fclose(file);

    if (!avi_scan(data, (uint32_t)file_size, headers, chunks, chunk_counts, &streams))
    {
        for (i = 0; i < AVI_MAX_STREAMS; i++) free(chunks[i]);
        free(data);
        return AVIERR_BADFORMAT;
    }

    wanted = a3;
    for (i = 0; i < streams; i++)
    {
        if (a2 == 0 || headers[i].strh[0] == a2)
        {
            if (seen++ == wanted) { found = (int)i; break; }
        }
    }

    for (slot = 0; slot < AVI_MAX_STREAMS; slot++) if (!avi_streams[slot].used) break;
    if (found < 0 || slot == AVI_MAX_STREAMS || headers[found].format == NULL)
    {
        for (i = 0; i < AVI_MAX_STREAMS; i++) free(chunks[i]);
        free(data);
        return (found < 0) ? AVIERR_NODATA : AVIERR_MEMORY;
    }

    stream = &avi_streams[slot];
    memset(stream, 0, sizeof(*stream));
    stream->used = 1;
    stream->data = data;
    memcpy(stream->strh, headers[found].strh, sizeof(stream->strh));
    stream->format = headers[found].format;
    stream->format_size = headers[found].format_size;
    stream->chunks = chunks[found];
    stream->chunk_count = chunk_counts[found];
    chunks[found] = NULL;
    for (i = 0; i < AVI_MAX_STREAMS; i++) free(chunks[i]);

    /* audio: positions are in samples (one sample is sample_size bytes) */
    {
        uint32_t sample_size = stream->strh[11];       /* dwSampleSize */
        uint32_t samples = 0;
        if (sample_size == 0) sample_size = 1;
        for (i = 0; i < stream->chunk_count; i++)
        {
            stream->chunks[i].first_sample = samples;
            samples += stream->chunks[i].size / sample_size;
        }
        stream->total_samples = (stream->strh[0] == FOURCC('v', 'i', 'd', 's')) ? stream->chunk_count : samples;
    }

    if (avi_debug()) fprintf(stderr, "[AVI stream %u: type=%08x chunks=%u length=%u]\n", slot, stream->strh[0], stream->chunk_count, stream->total_samples);
    *(uint32_t *)GUEST_MEM(a0) = AVI_HANDLE_BASE + slot;
    return 0;
}

uint32_t CCALL AVIStreamInfoA_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    struct avi_stream *stream = avi_lookup(a0);
    uint8_t info[140];
    uint32_t *words = (uint32_t *)info;

    if (stream == NULL) return AVIERR_BADHANDLE;
    if (a1 == 0 || a2 < sizeof(info)) return AVIERR_BADPARAM;

    memset(info, 0, sizeof(info));
    words[0] = stream->strh[0];                         /* fccType */
    words[1] = stream->strh[1];                         /* fccHandler */
    words[2] = stream->strh[2];                         /* dwFlags */
    words[3] = 0;                                       /* dwCaps */
    words[4] = stream->strh[3];                         /* wPriority, wLanguage */
    words[5] = stream->strh[5];                         /* dwScale */
    words[6] = stream->strh[6];                         /* dwRate */
    words[7] = stream->strh[7];                         /* dwStart */
    words[8] = stream->total_samples;                   /* dwLength */
    words[9] = stream->strh[4];                         /* dwInitialFrames */
    words[10] = stream->strh[9];                        /* dwSuggestedBufferSize */
    words[11] = stream->strh[10];                       /* dwQuality */
    words[12] = stream->strh[11];                       /* dwSampleSize */
    /* rcFrame: four 16-bit values in the stream header, four 32-bit ones in AVISTREAMINFO */
    words[13] = (uint32_t)(int32_t)(int16_t)(stream->strh[12] & 0xffff);
    words[14] = (uint32_t)(int32_t)(int16_t)(stream->strh[12] >> 16);
    words[15] = (uint32_t)(int32_t)(int16_t)(stream->strh[13] & 0xffff);
    words[16] = (uint32_t)(int32_t)(int16_t)(stream->strh[13] >> 16);
    if (avi_debug()) fprintf(stderr, "[AVIStreamInfo %x: type=%08x scale=%u rate=%u start=%u length=%u initial=%u sugbuf=%u samplesize=%u]\n", a0, words[0], words[5], words[6], words[7], words[8], words[9], words[10], words[12]);
    memcpy(GUEST_MEM(a1), info, sizeof(info));
    return 0;
}

uint32_t CCALL AVIStreamLength_c(uint32_t a0)
{
    struct avi_stream *stream = avi_lookup(a0);
    if (stream == NULL) return (uint32_t)-1;
    if (avi_debug()) fprintf(stderr, "[AVIStreamLength %x -> %u]\n", a0, stream->total_samples);
    return stream->total_samples;
}

/* a1 = position, a2 = format buffer (NULL asks for the size), a3 = LONG *size */
uint32_t CCALL AVIStreamReadFormat_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    struct avi_stream *stream = avi_lookup(a0);
    uint32_t *size;
    (void)a1;

    if (stream == NULL) return AVIERR_BADHANDLE;
    if (a3 == 0) return AVIERR_BADPARAM;
    size = (uint32_t *)GUEST_MEM(a3);
    if (a2 != 0)
    {
        uint32_t copy = (*size < stream->format_size) ? *size : stream->format_size;
        memcpy(GUEST_MEM(a2), stream->format, copy);
        if (*size < stream->format_size)
        {
            *size = stream->format_size;
            return AVIERR_BUFFERTOOSMALL;
        }
    }
    *size = stream->format_size;
    return 0;
}

/* a1 = first sample, a2 = number of samples (-1: as many as fit), a3 = buffer (NULL: only report sizes),
 * a4 = buffer size, a5 = LONG *bytes, a6 = LONG *samples */
uint32_t CCALL AVIStreamRead_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    struct avi_stream *stream = avi_lookup(a0);
    int32_t start = (int32_t)a1;
    int32_t count = (int32_t)a2;
    uint32_t bytes = 0, samples = 0;

    if (stream == NULL) return AVIERR_BADHANDLE;
    if (start < 0 || (uint32_t)start >= stream->total_samples)
    {
        if (a5 != 0) *(uint32_t *)GUEST_MEM(a5) = 0;
        if (a6 != 0) *(uint32_t *)GUEST_MEM(a6) = 0;
        return 0;
    }

    if (stream->strh[0] == FOURCC('v', 'i', 'd', 's'))
    {
        /* one chunk per frame */
        const struct avi_chunk *chunk = &stream->chunks[start];
        bytes = chunk->size;
        samples = 1;
        if (a3 != 0)
        {
            if (a4 < bytes)
            {
                if (a5 != 0) *(uint32_t *)GUEST_MEM(a5) = bytes;
                return AVIERR_BUFFERTOOSMALL;
            }
            memcpy(GUEST_MEM(a3), stream->data + chunk->offset, bytes);
        }
    }
    else
    {
        uint32_t sample_size = stream->strh[11] ? stream->strh[11] : 1;
        uint32_t available = stream->total_samples - (uint32_t)start;
        uint32_t wanted = (count < 0) ? available : (uint32_t)count;
        uint32_t chunk_index = 0, lo = 0, hi = stream->chunk_count;
        uint8_t *out = (a3 != 0) ? (uint8_t *)GUEST_MEM(a3) : NULL;

        if (wanted > available) wanted = available;
        if (out != NULL && count < 0 && a4 / sample_size < wanted) wanted = a4 / sample_size;
        if (out != NULL && a4 / sample_size < wanted) wanted = a4 / sample_size;

        /* the last chunk that starts at or before the position */
        while (lo < hi)
        {
            uint32_t mid = (lo + hi) / 2;
            if (stream->chunks[mid].first_sample <= (uint32_t)start) { chunk_index = mid; lo = mid + 1; }
            else hi = mid;
        }

        samples = wanted;
        bytes = wanted * sample_size;
        if (out != NULL)
        {
            uint32_t remaining = wanted;
            uint32_t sample = (uint32_t)start;
            while (remaining != 0 && chunk_index < stream->chunk_count)
            {
                const struct avi_chunk *chunk = &stream->chunks[chunk_index];
                uint32_t within = sample - chunk->first_sample;
                uint32_t in_chunk = chunk->size / sample_size - within;
                uint32_t take = (in_chunk < remaining) ? in_chunk : remaining;
                memcpy(out, stream->data + chunk->offset + within * sample_size, take * sample_size);
                out += take * sample_size;
                sample += take;
                remaining -= take;
                chunk_index++;
            }
        }
    }

    if (avi_debug()) fprintf(stderr, "[AVIStreamRead %x start=%d count=%d buf=%x cb=%u -> bytes=%u samples=%u]\n", a0, start, count, a3, a4, bytes, samples);
    if (a5 != 0) *(uint32_t *)GUEST_MEM(a5) = bytes;
    if (a6 != 0) *(uint32_t *)GUEST_MEM(a6) = samples;
    return 0;
}

uint32_t CCALL AVIStreamRelease_c(uint32_t a0)
{
    struct avi_stream *stream = avi_lookup(a0);

    if (stream == NULL) return 0;
    free(stream->data);
    free(stream->chunks);
    memset(stream, 0, sizeof(*stream));
    return 0;
}
