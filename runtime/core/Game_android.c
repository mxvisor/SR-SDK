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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <SDL.h>

#include "Game_hooks.h"

/* stdout/stderr of an Android app go nowhere; pump them into logcat so
   `adb logcat -s "<GAME_NAME>"` shows what the port prints. */
static void *log_pump(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char buf[512];
    ssize_t got;

    while ((got = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        buf[got] = '\0';
        __android_log_write(ANDROID_LOG_INFO, GAME_NAME, buf);
    }
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

#endif
