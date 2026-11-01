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

#include "Game_android.h"

#if defined(__ANDROID__)

#include <inttypes.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <SDL.h>

#include "Game_hooks.h"

/* stdout/stderr of an Android app go nowhere; pump them into logcat (so
   `adb logcat -s "<GAME_NAME>"` shows what the port prints) and into
   Android/data/<package>/files/game.log, which needs no adb. */
static void *log_pump(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[512];
    char path[4096];
    const char *external;
    FILE *file = NULL;
    ssize_t got;

    external = SDL_AndroidGetExternalStoragePath();
    if (external != NULL)
    {
        snprintf(path, sizeof(path), "%s/game.log", external);
        file = fopen(path, "a");
    }

    while ((got = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        buf[got] = '\0';
        __android_log_write(ANDROID_LOG_INFO, GAME_NAME, buf);
        if (file != NULL)
        {
            fputs(buf, file);
            fflush(file);
        }
    }
    if (file != NULL) fclose(file);
    return NULL;
}

/* The paths GameActivity created are handed to the runtime through the same
   environment the desktop uses. */
void Game_AndroidSetup(void)
{
    int fds[2];
    const char *base;
    char path[4096];
    pthread_t thread;

    if (pipe(fds) == 0)
    {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        if (pthread_create(&thread, NULL, log_pump, (void *)(intptr_t)fds[0]) == 0)
        {
            pthread_detach(thread);
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    base = SDL_AndroidGetInternalStoragePath();
    if (base != NULL)
    {
        snprintf(path, sizeof(path), "%s/game", base);
        setenv(GAME_ENV("DATA_DIR"), path, 1);
        snprintf(path, sizeof(path), "%s/write", base);
        setenv(GAME_ENV("WRITE_ROOT"), path, 1);
    }
}

struct image_range
{
    uintptr_t inside, start, end;
};

static int find_image(struct dl_phdr_info *info, size_t size, void *data)
{
    struct image_range *image = (struct image_range *)data;
    uintptr_t start = UINTPTR_MAX, end = 0;
    int index;

    (void)size;
    for (index = 0; index < info->dlpi_phnum; index++)
    {
        const ElfW(Phdr) *header = &info->dlpi_phdr[index];
        uintptr_t first = info->dlpi_addr + header->p_vaddr;

        if (header->p_type != PT_LOAD) continue;
        if (first < start) start = first;
        if (first + header->p_memsz > end) end = first + header->p_memsz;
    }
    if (image->inside < start || image->inside >= end) return 0;
    image->start = start;
    image->end = end;
    return 1;
}

static FILE *open_external(const char *name, const char *mode)
{
    char path[4096];
    const char *external = SDL_AndroidGetExternalStoragePath();

    if (external == NULL) return NULL;
    snprintf(path, sizeof(path), "%s/%s", external, name);
    return fopen(path, mode);
}

/* What a report from a device needs, without another run. */
static void save_memory_map(void)
{
    char buf[4096];
    size_t got;
    FILE *maps = fopen("/proc/self/maps", "r");
    FILE *file = open_external("maps.txt", "w");

    if (maps != NULL && file != NULL)
    {
        while ((got = fread(buf, 1, sizeof(buf), maps)) > 0) fwrite(buf, 1, got, file);
    }
    if (file != NULL) fclose(file);
    if (maps != NULL) fclose(maps);
}

static int get_image(struct image_range *image)
{
    image->inside = (uintptr_t)&get_image;
    if (dl_iterate_phdr(find_image, image)) return 1;
    fprintf(stderr, "Could not find the image of the program\n");
    return 0;
}

/* android/gameloader.c reserved it, right below the image. */
int Game_AndroidGuestRegion(uint64_t *base, uint64_t *size)
{
    const char *value = getenv("SR_SDK_GUEST_REGION");

    if (value == NULL) return 0;
    *base = strtoull(value, NULL, 16);
    *size = UINT64_C(0x80000000);
    return 1;
}

int Game_AndroidCheckImageReach(uint64_t offset, uint64_t size)
{
    struct image_range image;
    char line[256];
    int reach;
    FILE *file;

    if (!get_image(&image)) return 1;

    reach = image.start >= offset && image.end - offset <= UINT64_C(0x100000000);
    snprintf(line, sizeof(line),
             "native image 0x%" PRIxPTR "-0x%" PRIxPTR " pointer_offset 0x%" PRIx64 " size 0x%" PRIx64 " distance %" PRId64 " %s\n",
             image.start, image.end, offset, size, (int64_t)(image.start - offset),
             reach ? "in reach" : "OUT OF REACH of 32-bit code pointers");

    fputs(line, stderr);
    file = open_external("startup.txt", "a");
    if (file != NULL)
    {
        fputs(line, file);
        fclose(file);
    }
    save_memory_map();
    return reach ? 0 : 1;
}

#endif
